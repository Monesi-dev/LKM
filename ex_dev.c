#include <linux/poll.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/miscdevice.h>

#include "ex_thread.h"

static struct miscdevice thread_out_device;

static int thread_open(struct inode *inode, struct file *file)
{
        // Print Data about Miscdevice
        printk( KERN_INFO "Miscdevice Opened!\n");
        printk( KERN_INFO "Device Major Number: %d\n", imajor(inode));
        printk( KERN_INFO "Device Minor Number: %d\n", iminor(inode));        

        return 0;
}

static int thread_close(struct inode *inode, struct file *file)
{
        printk( KERN_INFO "Miscdevice Closed!\n");
        return 0;
}

ssize_t thread_read(struct file *file, char __user *p, size_t len, loff_t *ppos)
{
        int res;
        wait_for_completion(&available_data);
        res = get_data(p, len);

        return res;
}

ssize_t thread_write(struct file * file, const char __user * buf, size_t len, loff_t * ppos)
{
        int res;
        res = write_data(buf, len);
        return res;
}

int my_device_create(void)
{
        return misc_register(&thread_out_device);
}

void my_device_destroy(void)
{
        misc_deregister(&thread_out_device);
}

static struct file_operations thread_fops = {
        .owner    =   THIS_MODULE,
        .read     =   thread_read,
        .open     =   thread_open,
        .write    =   thread_write,
        .release  =   thread_close,
};

static struct miscdevice thread_out_device = {
        .minor    =   MISC_DYNAMIC_MINOR, 
        .name     =   "thout", 
        .fops     =   &thread_fops,
};
