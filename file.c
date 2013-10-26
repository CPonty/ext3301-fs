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

/*
 * ext3301 utility: check if file is in the encryption tree 
 */
bool ext3301_isencrypted(struct file *filp) {
	struct dentry * dsearch;
	struct dentry * dtop;

	dtop = filp->f_path.dentry;
	do {
		dsearch = dtop;
		dtop = dtop->d_parent;
	} while (dtop != dtop->d_parent);
	return !(strcmp(crypter_dir, dsearch->d_name.name));
}

/* 
 * ext3301 variant of the standard file read function.
 *  modifications: handling encryption and immediate files.
 *  original: do_sync_read
 */
ssize_t ext3301_read(struct file *filp, char __user *buf, size_t len, loff_t *ppos) {
	ssize_t ret;
	ret = do_sync_read(filp, buf, len, ppos);	

	//Check if the file is in the encryption tree
	if (ext3301_isencrypted(filp)) {
		//Decrypt the data which was read
		//
		//
		printk(KERN_DEBUG "Reading from encrypted file %s\n",
			filp->f_path.dentry->d_name.name);
	}

	return ret; 
}

//
//
//filp->f_path.dentry->d_parent (->d_name.name)
//
//printk(KERN_DEBUG "Wrote file %s in directory %s\n",
//	filp->f_path.dentry->d_name.name,
//	filp->f_path.dentry->d_parent->d_name.name
//);	
//
//printk(KERN_DEBUG "Wrote file, is %s == %s?\n",
//	dsearch->d_name.name, crypter_dir
//);
//
//

/*
 * ext3301 variant of the standard file write function.
 *  modifications: handling encryption and immediate files.
 *  original: do_sync_write
 */
ssize_t ext3301_write(struct file *filp, char __user *buf, size_t len, loff_t *ppos) {
	ssize_t ret;

	//Check if the file is in the encryption tree 
	if (ext3301_isencrypted(filp)) {
		//Encrypt the data being written
		//
		//
		printk(KERN_DEBUG "Writing to encrypted file %s\n",
			filp->f_path.dentry->d_name.name);
	}

	ret = do_sync_write(filp, buf, len, ppos);	
	return ret; 
}


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
