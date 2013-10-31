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
#include <linux/buffer_head.h>
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

	//Mutex-lock the inode
	INODE_LOCK(i);

	//Limit the read area to the filesize
	if (*ppos+len > INODE_ISIZE(i))
		read = INODE_ISIZE(i) - *ppos;

	//Read the immediate payload area into the buffer
	copy_to_user((void *)buf, (const void *)data, (unsigned long)read);
	*ppos += read;

	//Unlock
	INODE_UNLOCK(i);

	return read;
}

/*
 * ext3301 write_immediate: immediate file equivalent to the standard 
 * 	file write function. Treats the pointer block as the file payload.
 * Shouldn't need to verify write region; ext3301_write changes the
 * 	file to a regular file if we're writing too much for an immediate file.
 */
ssize_t ext3301_write_immediate(struct file * filp, char __user * buf, 
		size_t len, loff_t * ppos) {
	ssize_t write = len;
	struct inode * i = FILP_INODE(filp);
	char * data = INODE_PAYLOAD(i) + *ppos;

	// verify the write region
	if (*ppos+len > EXT3301_IM_SIZE(i)) {
		printk(KERN_DEBUG "IM file bad state, size>capacity, ino: %lu\n",
			INODE_INO(i));
		return -EIO;
	}

	//Mutex-lock the inode
	INODE_LOCK(i);

	//write the buffer to the immediate payload
	copy_from_user((void *)data, (const void *)buf, (unsigned long)write);
	*ppos += write;

	//Update the inode (time, filesize etc)
	i->i_size = *ppos;
	i->i_version++;
	i->i_mtime = i->i_ctime = CURRENT_TIME;
	mark_inode_dirty(i);

	//Unlock
	INODE_UNLOCK(i);

	return write;
}

/*
 * ext3301 immediate to regular: convert the file type.
 * 	filesize should be > EXT3301_IM_SIZE.
 *	Returns 0 on success, <0 on failure.
 */
ssize_t ext3301_im2reg(struct file * filp) {
	char * data;
	struct buffer_head * bh, search_bh;
	int err = 0;
	long block_offset = 0;
	struct inode * i = FILP_INODE(filp);
	ssize_t l = INODE_ISIZE(i);
	unsigned long blocksize = INODE_BLKSIZE(i);

	dbg_im(KERN_DEBUG "- im2reg l=%d\n", (int)INODE_ISIZE(i));

	// verify the immediate file size
	if (l > EXT3301_IM_SIZE(i)) {
		printk(KERN_DEBUG "IM file bad state, size>capacity, ino: %lu\n",
			INODE_INO(i));
		return -EIO;
	}

	// prepare a kernel buffer to store the file contents
	data = kmalloc((size_t)l, GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	 
	// Lock the inode
	INODE_LOCK(i);

	// Set the file type to regular
	INODE_MODE(i) = MODE_SET_REG(INODE_MODE(i));

	// Special case: file length is zero, nothing else to do
	if (l==0)
		goto out;

	// Read the payload (block pointer area) into a buffer
	memcpy((void *)data, (const void *)INODE_PAYLOAD(i), (size_t)l);

	// Zero the block pointer area (otherwise get_block will treat our old
	// 	immediate data as block pointers, and follow them...)
	memset((void *)INODE_PAYLOAD(i), 0, (size_t)EXT3301_IM_SIZE(i));

	// Use a buffer head to find the block number (of the first data block)
	//	'true' option: allocate new blocks with ext2_get_block 
	search_bh.b_state = 0;
	search_bh.b_size = blocksize;
	err = ext2_get_block(i, block_offset, &search_bh, true);
	if (err < 0) {
		dbg_im(KERN_DEBUG "- ext2_get_block() failed\n");
		goto out;
	}
	
	// Retrieve and lock a paged buffer head to the block number
	bh = sb_getblk(INODE_SUPER(i), search_bh.b_blocknr);
	if (bh==NULL) {
		dbg_im(KERN_DEBUG "- sb_getblk() failed\n");
		err = -EIO;
		goto out;
	}
	lock_buffer(bh);

	// Write (directly) to the buffer
	memcpy((void *)(bh->b_data), (const void *)data, (size_t)l);

	// Flush the paged buffer to disk: mark as dirty, unlock, sync, release.
	flush_dcache_page(bh->b_page);
	set_buffer_uptodate(bh);
	mark_buffer_dirty(bh);
	unlock_buffer(bh);
	sync_dirty_buffer(bh);
	brelse(bh);

out:
	// Finished - unlock the inode and mark it as dirty.
	// 	Note we haven't updated the ctime, filesize or anything else.
	// 	The subsequent write operation will do this
	mark_inode_dirty(i);
	INODE_UNLOCK(i);

	kfree(data);
	return err;
}

/*
 * ext3301 regular to immediate: convert the file type.
 *
 * filesize should be <= EXT3301_IM_SIZE.
 * There should only be one block in the file, if it is small enough to
 * 	become an immediate file.
 * Returns 0 on success, <0 on failure.
 */
ssize_t ext3301_reg2im(struct file * filp) {
	char * data;
	struct buffer_head * bh, search_bh;
	int err = 0;
	long block_offset = 0;
	struct inode * i = FILP_INODE(filp);
	ssize_t l = INODE_ISIZE(i);
	unsigned long blocksize = INODE_BLKSIZE(i);

	dbg_im(KERN_DEBUG "- reg2im l=%d\n", (int)INODE_ISIZE(i));

	// verify the immediate file size
	if (l > EXT3301_IM_SIZE(i)) {
		printk(KERN_DEBUG "IM file bad state, size>capacity, ino: %lu\n",
			INODE_INO(i));
		return -EIO;
	}

	// prepare a kernel buffer to store the file contents
	data = kmalloc((size_t)l, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	// Lock the inode
	INODE_LOCK(i);

	// Special case: file length is zero, go straight to freeing blocks
	if (l==0)
		goto free;

	// Use a buffer head to find the block number (of the first data block)
	// 	'false' option: do not allocate new blocks with ext2_get_block
	search_bh.b_state = 0;
	search_bh.b_size = blocksize;
	err = ext2_get_block(i, block_offset, &search_bh, false);
	if (err < 0) {
		dbg_im(KERN_DEBUG "- ext2_get_block() failed\n");
		goto out;
	}
	
	// Retrieve and lock a paged buffer head to the block number
	bh = sb_getblk(INODE_SUPER(i), search_bh.b_blocknr);
	if (bh==NULL) {
		dbg_im(KERN_DEBUG "- sb_getblk() failed\n");
		err = -EIO;
		goto out;
	}
	lock_buffer(bh);

	// Copy directly from the buffer data segment to our kernel buffer
	memcpy((void *)data, (const void *)(bh->b_data), (size_t)l);

	// Release the paged buffer: unlock, release.
	unlock_buffer(bh);
	brelse(bh);

	//Write the kernel buffer into the block pointer area
	memcpy((void *)INODE_PAYLOAD(i), (const void *)data, (size_t)l);

free:

	// Free the block
	//
	
	// Set the file type to immediate
	INODE_MODE(i) = MODE_SET_IM(INODE_MODE(i));

out:
	// Finished - unlock the inode and mark it as dirty.
	// 	Note we haven't updated the ctime, filesize or anything else.
	// 	The previous write operation already did this
	mark_inode_dirty(i);
	INODE_UNLOCK(i);

	kfree(data);
	return err;
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
	if (I_ISIM(i)) {
		dbg_im(KERN_DEBUG "- Read-immediate\n");
		ret = ext3301_read_immediate(filp, buf, len, ppos);	

	} else {
		dbg_im(KERN_DEBUG "- Read-regular\n");
		ret = do_sync_read(filp, buf, len, ppos);	
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
	ssize_t ret, written;
	struct inode * i = FILP_INODE(filp);

	dbg_im(KERN_DEBUG "Write: '%s'\n", FILP_NAME(filp));

	//Encryption: Check if the file is in the encryption tree 
	if (ext3301_isencrypted(filp->f_path.dentry)) {
		//Encrypt the data being written
		dbg_cr(KERN_DEBUG "- Encrypting data (%d bytes)\n", (int)len);
		if (ext3301_cryptbuf(buf, len) < 0)
			return -EIO;
	}

	//Immediate file only: walk ppos forward manually for Append mode
	if (I_ISIM(i) && (FILP_FLAGS(filp) & O_APPEND)) {
		dbg_im(KERN_DEBUG "O_APPEND: walking ppos to EoF\n");
		*ppos += INODE_ISIZE(i);
	}

	//Immediate file only: Check if it needs to grow into a regular file
	if (I_ISIM(i) && (*ppos+len > EXT3301_IM_SIZE(i))) {
		dbg_im(KERN_DEBUG "- IM-->REG conversion\n");
		ret = ext3301_im2reg(filp);
		if (ret < 0) {
			printk(KERN_DEBUG "IM-->REG conversion fail: ino %lu, err %d\n",
				INODE_INO(i), (int)ret);
			return ret;
		}
		//Append mode: undo the ppos offset. We are now writing to a
		//regular file, and the default methods already handle this.
		if (FILP_FLAGS(filp) & O_APPEND) {
			dbg_im(KERN_DEBUG "O_APPEND: walking ppos back (REG)\n");
			*ppos -= INODE_ISIZE(i);
		}
	}

	//Write to file (immediate and regular files have different methods)
	if (I_ISIM(i)) {
		dbg_im(KERN_DEBUG "- Write-immediate\n");
		written = ext3301_write_immediate(filp, buf, len, ppos);	

	} else {
		dbg_im(KERN_DEBUG "- Write-regular\n");
		written = do_sync_write(filp, buf, len, ppos);	
	}

	//Regular file only: Check if it's small enough to convert to immediate
	if (INODE_TYPE(i)==DT_REG && (INODE_ISIZE(i)<=EXT3301_IM_SIZE(i))) {
		dbg_im(KERN_DEBUG "- REG-->IM conversion\n");
		ret = ext3301_reg2im(filp);
		if (ret < 0) {
			printk(KERN_DEBUG "REG-->IM file conversion failed: ino %lu\n",
				INODE_INO(i));
			return ret;
		}
	}
	
	return written; 
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
