/*
 *  linux/fs/ext2/crypter.c
 *
 *  Added to ext2 as part of the ext3301 fork
 *  Written by Chris Ponticello
 * 	(christopher.ponticello@uqconnect.edu.au)
 */

#include <linux/time.h>
#include <linux/pagemap.h>
#include <linux/quotaops.h>
#include <linux/kernel.h>
#include "ext2.h"
#include "xattr.h"
#include "acl.h"

unsigned char crypter_key = 0;
const char * crypter_dir = "encrypt";

/*
 * ext3301 utility: check if directory entry is in the encryption tree 
 * To get the dentry of a file:
 * 	(struct file *)filp->f_path.dentry
 */
bool ext3301_isencrypted(struct dentry * dcheck) {
	struct dentry * dsearch;
	struct dentry * dtop = dcheck;
	do {
		dsearch = dtop;
		dtop = dtop->d_parent;
	} while (dtop != dtop->d_parent);
	return !(strcmp(crypter_dir, dsearch->d_name.name));
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