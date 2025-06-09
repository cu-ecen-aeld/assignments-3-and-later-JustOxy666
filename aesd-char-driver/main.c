/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/gfp.h>  /* kmalloc flags */
#include <linux/slab.h>  /* kmalloc */
#include <linux/mutex.h> /* mutex */
#include <linux/fs.h> // file_operations
#include "aesdchar.h"
#include "aesd-circular-buffer.h"
int aesd_major =   0; // use dynamic major
int aesd_minor =   0;


MODULE_AUTHOR("JustOxy666");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;


int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");
    mutex_lock(&aesd_device.mutex_lock);
    /**
     * TODO: handle open
     */
    return 0;
}


int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    mutex_unlock(&aesd_device.mutex_lock);
    /**
     * TODO: handle release
     */
    return 0;
}


ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    uint8_t index = 0U;
    ssize_t retval = 0;
    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);
    /**
     * TODO: handle read
     */
    for (index = 0; index < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; index++)
    {
        if (aesd_device.circ_buffer->entry[index].buffptr != NULL)
        {
            memcpy(buf, aesd_device.circ_buffer->entry[index].buffptr, aesd_device.circ_buffer->entry[index].size);
        }
    }
    return retval;
}


ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    struct aesd_buffer_entry *new_entry;
    ssize_t retval = 0;
    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);
    PDEBUG("buf = %s", buf);
    /**
     * TODO: handle write
     */
    new_entry->size = kmalloc(sizeof(count), GFP_KERNEL);
    memcpy(new_entry->size, count, sizeof(count));

    if (count != 0)
    {
        new_entry->buffptr = kmalloc((sizeof(char) * count), GFP_KERNEL);
        if (new_entry->buffptr <= 0)
        {
            retval = -ENOMEM;
            PDEBUG("kmalloc failed");
        }
        else
        {
            memcpy(new_entry->buffptr, buf, count);
            aesd_circular_buffer_add_entry(aesd_device.circ_buffer, new_entry);
        }
    }
    else
    {
        PDEBUG("write called with zero count, nothing to do");
    }
    
    return retval;
}


struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
};


static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}


int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
            "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));

    /**
     * TODO: initialize the AESD specific portion of the device
     */
    aesd_device.circ_buffer = kmalloc(sizeof(aesd_device.circ_buffer), GFP_KERNEL);
    aesd_circular_buffer_init(aesd_device.circ_buffer);
    mutex_init(&aesd_device.mutex_lock);

    result = aesd_setup_cdev(&aesd_device);

    if( result )
    {
        unregister_chrdev_region(dev, 1);
    }
    return result;

}


void aesd_cleanup_module(void)
{
    uint8_t index = 0U;
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    /**
     * TODO: cleanup AESD specific poritions here as necessary
     */
    mutex_destroy(&aesd_device.mutex_lock);
    for (index = 0; index < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; index++)
    {
        if (aesd_device.circ_buffer->entry[index].buffptr != NULL)
        {
            kfree((void *)aesd_device.circ_buffer->entry[index].buffptr);
            aesd_device.circ_buffer->entry[index].buffptr = NULL;
        }
    }

    kfree(aesd_device.circ_buffer);

    unregister_chrdev_region(devno, 1);
}


module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
