#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Chris Ponticello");
MODULE_DESCRIPTION("A simple kernel module");

int __init init_module(void) {
	printk(KERN_INFO "HELLO WORLD!\n");
	return 0;
}

void __exit cleanup_module(void) {
	printk(KERN_INFO "Goodbye world.\n");
}
