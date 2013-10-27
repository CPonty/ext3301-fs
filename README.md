ext3301-fs
==========

**ext2** File System, with immediate files and encryption added. 

Final assignment for UQ's *COMP3301 - Operating Systems Principles* course.

Important files:
* file.c contains wrappers for file read and write (handling encryption of written/read files).
* ext2.h contains function prototypes, 2 global variables, a few #define utilities (all at the bottom) 
and an immediate file type.
* ext3301util.c contains utility functions: a suite of file operations, utility to build a filepath from a dentry,
and utility to check if a dentry should be encrypted or not.
* namei.c contains the edited ext2_rename() function (handling encryption of moved files).
