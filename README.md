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
