/*
 *  linux/fs/ext2/file.c
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  from
 *
 *  linux/fs/minix/file.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  ext2 fs regular file handling primitives
 *
 *  64-bit file support on 64-bit platforms by Jakub Jelinek
 * 	(jj@sunsite.ms.mff.cuni.cz)
 *
 *  ext3301 improvements added by Chris Ponticello 
 *  	(christopher.ponticello@uqconnect.edu.au), October 2013 

 */

#include <linux/time.h>
#include <linux/pagemap.h>
#include <linux/quotaops.h>
#include <linux/kernel.h>
#include "ext2.h"
#include "xattr.h"
#include "acl.h"

/*
 * Called when filp is released. This happens when all file descriptors
 * for a single struct file are closed. Note that different open() calls
 * for the same file yield different struct file structures.
 */
static int ext2_release_file (struct inode * inode, struct file * filp)
{
	if (filp->f_mode & FMODE_WRITE) {
		mutex_lock(&EXT2_I(inode)->truncate_mutex);
		ext2_discard_reservation(inode);
		mutex_unlock(&EXT2_I(inode)->truncate_mutex);
	}
	return 0;
}

int ext2_fsync(struct file *file, loff_t start, loff_t end, int datasync)
{
	int ret;
	struct super_block *sb = file->f_mapping->host->i_sb;
	struct address_space *mapping = sb->s_bdev->bd_inode->i_mapping;

	ret = generic_file_fsync(file, start, end, datasync);
	if (ret == -EIO || test_and_clear_bit(AS_EIO, &mapping->flags)) {
		/* We don't really know where the IO error happened... */
		ext2_error(sb, __func__,
			   "detected IO error when writing metadata buffers");
		ret = -EIO;
	}
	return ret;
}

// --------------------------------------------------------------------

/*
 * ext3301 read_immediate: immediate file equivalent to the standard 
 * 	file read function. Treats the pointer block as the file payload.
 * Note that the read length may be a full block size, even though the
 * 	file is obviously smaller.
 */
ssize_t ext3301_read_immediate(struct file * filp, char __user * buf, 
		size_t len, loff_t * ppos) {
	ssize_t read = len;
	struct inode * i = FILP_INODE(filp);
	char * data = INODE_PAYLOAD(i) + *ppos;

	dbg_im(KERN_WARNING "read_im start: ppos=%d len=%d size=%d flag=%u\n",
		(int)(*ppos),(int)len,(int)i->i_size, filp->f_flags);

//	read = do_sync_read(filp, buf, len, ppos);	
	
	//Mutex-lock the inode
	INODE_LOCK(i);

	//Limit the read area to the filesize
	if (*ppos+len > FILP_FSIZE(filp))
		read = FILP_FSIZE(filp) - *ppos;

	//Read the immediate payload area into the buffer
	copy_to_user((void *)buf, (const void *)data, (unsigned long)read);
	*ppos += read;

	//Unlock
	INODE_UNLOCK(i);

	dbg_im(KERN_WARNING "end: ppos=%d len=%d size=%d fpos=%d ret=%d\n",
		(int)(*ppos), (int)len, (int)i->i_size, (int)filp->f_pos, 
		(int)read);

	return read;
}

/*
 * ext3301 write_immediate: immediate file equivalent to the standard 
 * 	file write function. Treats the pointer block as the file payload.
 * No need to verify the write area; ext3301_write will have changed the
 * 	file to a regular file if we're writing too much for an immediate file.
 */
ssize_t ext3301_write_immediate(struct file * filp, char __user * buf, 
		size_t len, loff_t * ppos) {
	ssize_t write = len;
	struct inode * i = FILP_INODE(filp);
	char * data;

	//Check where to write from. Append-mode writes are relative to the end
	if (filp->f_flags & O_APPEND) {
		dbg_im(KERN_WARNING "F-APPEND\n");
		*ppos += i->i_size;
	}
	data = INODE_PAYLOAD(i) + *ppos;

	dbg_im(KERN_WARNING "write_im start: ppos=%d len=%d size=%d flag=%u\n",
		(int)(*ppos),(int)len,(int)i->i_size, filp->f_flags);

//	write = do_sync_write(filp, buf, len, ppos);	
	
	//Mutex-lock the inode
	INODE_LOCK(i);

	//TEMPORARY: limit writes to immediate file, until ext3301_im2reg works
	if (*ppos+len > EXT3301_IM_SIZE(i)) {
		dbg_im(KERN_DEBUG "Immediate file write: Truncating to 60 bytes\n");
		write = EXT3301_IM_SIZE(i)- *ppos;
		copy_from_user(	(void *)data, (const void *)buf, 
						(unsigned long)(write));
		*ppos += len;
		if (*ppos > i->i_size)
			i->i_size = *ppos;
		i->i_version++;
		i->i_mtime = i->i_ctime = CURRENT_TIME;
		mark_inode_dirty(i);
		INODE_UNLOCK(i);
		return len; // didn't actually write len bytes, 
					// but the caller must think we did
	}

	//write the buffer to the immediate payload
	copy_from_user((void *)data, (const void *)buf, (unsigned long)write);
	*ppos += write;

	//Update the inode (time, filesize etc)
	if (*ppos > i->i_size)
		i->i_size = *ppos;
	i->i_version++;
	i->i_mtime = i->i_ctime = CURRENT_TIME;
	mark_inode_dirty(i);

	//Unlock
	INODE_UNLOCK(i);

	dbg_im(KERN_WARNING "end: ppos=%d len=%d size=%d fpos=%d ret=%d\n",
		(int)(*ppos), (int)len, (int)i->i_size, (int)filp->f_pos, 
		(int)write);

	return write;
}

/*
 * ext3301 immediate to regular: convert the file type.
 * 	filesize should be > EXT3301_IMMEDIATE_MAX_SIZE.
 *	Returns 0 on success, -EIO on failure.
 */
ssize_t ext3301_im2reg(struct file * filp) {
	ssize_t ret = 0;

	//Lock the inode
	//Copy the payload (block pointer area) into a kernel buffer
	//Set the file mode
	//Allocate/write to the first block (see example code)
	//Unlock the inode
	//??? Return

	return ret;
}

/*
 * ext3301 regular to immediate: convert the file type.
 * 	filesize should be <= EXT3301_IMMEDIATE_MAX_SIZE.
 *	Returns 0 on success, -EIO on failure.
 */
ssize_t ext3301_reg2im(struct file * filp) {
	ssize_t ret = 0;

	//Lock the inode
	//Copy the payload (first block) into a kernel buffer
	//??? Free the first block
	//??? Zero the block count/fsize
	//Set the file mode
	//Write the kernel buffer into the block pointer area
	//??? Set the fsize
	//Unlock the inode
	//??? Return

	return ret;
}

// --------------------------------------------------------------------

/* 
 * ext3301 read: wrapper for the standard file read function.
 *  modifications: handling encryption and immediate files.
 *  original: do_sync_read
 */
ssize_t ext3301_read(struct file * filp, char __user * buf, size_t len, 
		loff_t * ppos) {
	struct inode * i = FILP_INODE(filp);
	ssize_t ret = 0;

	dbg(KERN_DEBUG "Read: '%s'\n", FILP_NAME(filp));

	//Check if the file is immediate (requires special read behaviour)
	if (INODE_MODE(i)==DT_IM) {
		dbg_im(KERN_DEBUG "- Read-immediate\n");
		ret = ext3301_read_immediate(filp, buf, len, ppos);	

	} else {
		dbg_im(KERN_DEBUG "- Read-regular\n");
		dbg_im(KERN_WARNING "rdreg start: ppos=%d len=%d size=%d fpos=%d\n",
			(int)(*ppos), (int)len, (int)i->i_size, (int)filp->f_pos);
		ret = do_sync_read(filp, buf, len, ppos);	
		dbg_im(KERN_WARNING "end: ppos=%d len=%d size=%d fpos=%d ret=%d\n",
			(int)(*ppos), (int)len, (int)i->i_size, (int)filp->f_pos, 
			(int)ret);
	}

	//Check if the file is in the encryption tree
	if (ext3301_isencrypted(filp->f_path.dentry)) {
		//Decrypt the data which was read
		dbg_cr(KERN_DEBUG "- Encrypting data (%d bytes)\n", (int)len);
		if (ext3301_cryptbuf(buf, len) < 0)
			return -EIO;
	}

	return ret; 
}

/*
 * ext3301 write: wrapper for the standard file write function.
 *  modifications: handling encryption and immediate files.
 *  original: do_sync_write
 */
ssize_t ext3301_write(struct file * filp, char __user * buf, size_t len, 
		loff_t * ppos) {
	ssize_t ret = 0;
	struct inode * i = FILP_INODE(filp);

	dbg_im(KERN_DEBUG "Write: '%s'\n", FILP_NAME(filp));

	//Encryption: Check if the file is in the encryption tree 
	if (ext3301_isencrypted(filp->f_path.dentry)) {
		//Encrypt the data being written
		dbg_cr(KERN_DEBUG "- Encrypting data (%d bytes)\n", (int)len);
		if (ext3301_cryptbuf(buf, len) < 0)
			return -EIO;
	}

	//Immediate file only: Check if it needs to grow into a regular file
	if (INODE_MODE(i)==DT_IM && (*ppos+len > EXT3301_IM_SIZE(i))) {
		dbg_im(KERN_DEBUG "- Im-->Reg conversion\n");
		if (ext3301_im2reg(filp) < 0)
			return -EIO;
	}

	//Write to file (immediate and regular files have different methods)
	if (INODE_MODE(i)==DT_IM) {
		dbg_im(KERN_DEBUG "- Write-immediate\n");
		ret = ext3301_write_immediate(filp, buf, len, ppos);	

	} else {
		dbg_im(KERN_DEBUG "- Write-regular\n");
		dbg_im(KERN_WARNING "wrreg start: ppos=%d len=%d size=%d fpos=%d\n",
			(int)(*ppos), (int)len, (int)i->i_size, (int)filp->f_pos);
		ret = do_sync_write(filp, buf, len, ppos);	
		dbg_im(KERN_WARNING "end: ppos=%d len=%d size=%d fpos=%d ret=%d\n",
			(int)(*ppos), (int)len, (int)i->i_size, (int)filp->f_pos, 
			(int)ret);
	}

	//Regular file only: Check if it's small enough to convert to immediate
	if (INODE_MODE(i)==DT_REG && (FILP_FSIZE(filp)<=EXT3301_IM_SIZE(i))) {
		dbg_im(KERN_DEBUG "- Reg-->Im conversion\n");
		if (ext3301_reg2im(filp) < 0)
			return -EIO;
	}
	
	return ret; 
}

// --------------------------------------------------------------------

/*
 * We have mostly NULL's here: the current defaults are ok for
 * the ext2 filesystem.
 */
const struct file_operations ext2_file_operations = {
	.llseek		= generic_file_llseek,
	.read		= ext3301_read, //do_sync_read
	.write		= ext3301_write, //do_sync_write
	.aio_read	= generic_file_aio_read,
	.aio_write	= generic_file_aio_write,
	.unlocked_ioctl = ext2_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= ext2_compat_ioctl,
#endif
	.mmap		= generic_file_mmap,
	.open		= dquot_file_open,
	.release	= ext2_release_file,
	.fsync		= ext2_fsync,
	.splice_read	= generic_file_splice_read,
	.splice_write	= generic_file_splice_write,
};

#ifdef CONFIG_EXT2_FS_XIP
const struct file_operations ext2_xip_file_operations = {
	.llseek		= generic_file_llseek,
	.read		= xip_file_read,
	.write		= xip_file_write,
	.unlocked_ioctl = ext2_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= ext2_compat_ioctl,
#endif
	.mmap		= xip_file_mmap,
	.open		= dquot_file_open,
	.release	= ext2_release_file,
	.fsync		= ext2_fsync,
};
#endif

const struct inode_operations ext2_file_inode_operations = {
#ifdef CONFIG_EXT2_FS_XATTR
	.setxattr	= generic_setxattr,
	.getxattr	= generic_getxattr,
	.listxattr	= ext2_listxattr,
	.removexattr	= generic_removexattr,
#endif
	.setattr	= ext2_setattr,
	.get_acl	= ext2_get_acl,
	.fiemap		= ext2_fiemap,
};
