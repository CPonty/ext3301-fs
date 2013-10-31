/*
 *  linux/fs/ext2/ext3301util.c
 *  Added to ext2 as part of the ext3301 improvements
 *
 *  Written by Chris Ponticello 
 * 	(christopher.ponticello@uqconnect.edu.au)
 *
 * 	Created 2013-10-23
 */

#include <linux/time.h>
#include <linux/pagemap.h>
#include <linux/quotaops.h>
#include <linux/kernel.h>
#include <linux/dcache.h>
#include <linux/string.h>
#include "ext2.h"
#include "xattr.h"
#include "acl.h"

unsigned char crypter_key = 0;
const char * crypter_dir = "encrypt";

/*
 * ext3301 init_ext3301_inode: wrapper for the linux kernel utility 
 * 	init_special_inode (in /linux/fs/inode.c).
 * Sole purpose of doing this is to suppress "bogus i_mode" messages
 * 	when immediate files are created.
 */
void init_ext3301_inode(struct inode *inode, umode_t mode, dev_t rdev) {
	if (!S_ISIM(mode))
		init_special_inode(inode, mode, rdev);
}

/*
 * ext3301 cryptbuf: apply the encryption XOR byte cipher to a buffer in
 *  user space. Return 0 on success, <0 on failure.
 */
int ext3301_cryptbuf(char __user * buf, size_t l) {
	char * kbuf;
	int err = 0;
	size_t i;

	// special case: nothing to encrypt
	if (!l)
		return 0;

	// allocate kernel buffer
	kbuf = kmalloc(l, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	// copy user-space payload into kernel buffer
	err = copy_from_user((void *)kbuf, (const void *)buf, (unsigned long)l);
	if (err) {
		err = -EACCES;
		goto out;
	}

	// encrypt buffer
	for (i=0; i<l; i++)
		kbuf[i] ^= crypter_key;

	// write kernel buffer back to user space
	err = copy_to_user((void *)buf, (const void *)kbuf, (unsigned long)l);
	if (err) {
		err = -EACCES;
		goto out;
	}

out:
	kfree(kbuf);
	return err;
}

/*
 * ext3301 isencrypted: check if directory entry is in the encryption tree.
 * To get the dentry of a file:
 * 	(struct file *)filp->f_path.dentry
 */
bool ext3301_isencrypted(struct dentry * dcheck) {
	struct dentry * dsearch;
	struct dentry * dtop = dcheck;

	// Walk along directory entry parents until we hit root (root's parent
	// 	is itself)
	do {
		dsearch = dtop;
		dtop = dtop->d_parent;
	} while (dtop != dtop->d_parent);

	// Check if the top directory entry matches the encrypted folder
	return !(strcmp(crypter_dir, dsearch->d_name.name));
}

/*
 * ext3301 getpath: build a string path to the directory entry given. 
 * 	Writes the string into the buffer supplied.
 * 	The string is built from the end of the buffer backwards.
 *	Returns a pointer to the start of the string on success, Null on failure.
 */
char * ext3301_getpath(struct dentry * dcheck, char * buf, int buflen) {
	char * s = dentry_path_raw(dcheck, buf, buflen);

	// This is basically just a wrapper for dentry_path_raw
	if (s==ERR_PTR(-ENAMETOOLONG))
		return NULL;
	else
		return s;
}


/*-----------------------------------------------------------*/

/*
 * kernel file open: run filp_open() in the kernel address space.
 *
 * Returns a file struct pointer on success, Null on failure.
 * During the open, the address limit of the process is set to that of the
 * 	kernel (and then reverted).
 * If the first character in the path is the root slash ('/'), it is 
 *	stripped. This allows the file opening utility to search the set of
 *	mounted file systems to open from.
 */
struct file * kfile_open(const char * fpath, int flags) {
	struct file * f = NULL;
	int offset = 0;
	mm_segment_t fs;

	if (fpath==NULL)
		return NULL;
	if (fpath[0]=='/')
		offset++;

	fs = get_fs();
	set_fs(get_ds());
	f = filp_open(fpath+offset, flags, 0);
	set_fs(fs);

	if (IS_ERR(f))
		return NULL;
	return f;
}

/*
 * kernel file read: run vfs_read() in the kernel address space.
 *
 *	Return value is directly passed from vfs_read (which gets it from
 *	 the fs-specific read function, in this case ext3301_read).
 *	Negative values indicate an error condition; Positive values indicate
 *	 the number of bytes read up to *size*. It is possible for the read to
 *	 return fewer bytes than requested.
 */
ssize_t kfile_read(struct file * f, char * buf, size_t size, 
		loff_t * offset) {
	ssize_t ret;
	mm_segment_t fs;

	fs = get_fs();
	set_fs(get_ds());
	ret = vfs_read(f, buf, size, offset);
	set_fs(fs);

	return ret;
}

/*
 * kernel file write: run vfs_write() in the kernel address space.
 */
ssize_t kfile_write(struct file * f, char * buf, size_t size, 
		loff_t * offset) {
	ssize_t ret;
	mm_segment_t fs;

	fs = get_fs();
	set_fs(get_ds());
	ret = vfs_write(f, buf, size, offset);
	set_fs(fs);

	return ret;
}

/*
 * kernel file sync: run vfs_fsync()
 */
void kfile_sync(struct file * f) {
	vfs_fsync(f, 0);
}

/*
 * kernel file close: run filp_close().
 */
void kfile_close(struct file * f) {
	filp_close(f, 0);
}

