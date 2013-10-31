ext3301-fs
==========

**ext2** File System, with immediate files and encryption added. 

Final assignment for UQ's *COMP3301 - Operating Systems Principles* course.

Important files:
* file.c contains wrappers for file read and write (handling encryption and providing special methods for immediate files).
* ext2.h contains function prototypes, global variables, debug features, preprocessor utilities (all at the bottom) 
and the immediate file type.
* ext3301util.c contains utility functions: a suite of file operations, utilities for building and analysing file paths,
and working with user-space buffers.
* namei.c contains the modified ext2_rename() function (handling encryption of moved files).
* super.c contains the modified parse_options() function (handling reading the encryption key).
* inode.c contains the modified ext2_iget() function (uses init_ext3301_inode() instead of init_special_inode())

Known bugs/incomplete features:
* Switching between immediate and regular files is not fully implemented. The partial implementation is viewable in file.c
* Immediate files are functional, however after the filesystem is unmounted and re-mounted they can no longer be written/read to

Comments:
* In my opinion, the decision to introduce a new file type (DT_IM) for immediate files is unwise. It causes a multitude of problems with generic linux kernel code outside the ext2 implementation. We would have been better off taking advantage of one of the unused bits in the inode flag mask, e.g. the unused file compression bit. See http://wiki.osdev.org/Ext2#Inode_Flags
