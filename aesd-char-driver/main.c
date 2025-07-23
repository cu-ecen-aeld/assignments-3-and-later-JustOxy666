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
#include "aesd_ioctl.h"
int aesd_major =   0; // use dynamic major
int aesd_minor =   0;


MODULE_AUTHOR("JustOxy666");
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev *aesd_device;

void aesd_cleanup_module(void);


int aesd_open(struct inode *inode, struct file *filp)
{
    struct aesd_dev *dev;

    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = dev;

    PDEBUG("open");

    return 0;
}


int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    struct aesd_dev *dev;

    dev = filp->private_data;

    return 0;
}


ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0;
    uint16_t read_bytes = 0U;
    size_t entry_offset_byte_rtn = 0U;
    struct aesd_dev *dev;
    struct aesd_buffer_entry *entry;

    PDEBUG("-------------");
    PDEBUG("aesd_read debug info:");
    PDEBUG("filp->f_pos %lld", filp->f_pos);
    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);
    dev = filp->private_data;
    if (mutex_lock_interruptible(&dev->mutex_lock))
    {
		retval = -ERESTARTSYS;
        goto out;
    }

    entry = aesd_circular_buffer_find_entry_offset_for_fpos(dev->circ_buffer, *f_pos, &entry_offset_byte_rtn);
    if (entry == NULL)
    {
        PDEBUG("NULL entry detected");
        goto unlock;
    }

    PDEBUG("entry->buffptr = %s", entry->buffptr);
    PDEBUG("entry->size = %zu bytes", entry->size);

    read_bytes = 0U;
    while (read_bytes < entry->size)
    {
        if (entry->buffptr != NULL)
        {
            copy_to_user((const char*)buf + read_bytes,
                        entry->buffptr + read_bytes, 
                        sizeof(char));
            read_bytes++;

            if (read_bytes >= count)
            {   
                PDEBUG("read_bytes >= count, breaking out of loop");
                break;
            }
        }
    }

    *f_pos += read_bytes;
    retval += read_bytes;
    PDEBUG("-------------");
    PDEBUG("completed reading entry");
    PDEBUG("read %zu bytes from entry", read_bytes);
    PDEBUG("filp->f_pos %lld", *f_pos);
    PDEBUG("-------------");
    
    unlock:
    mutex_unlock(&dev->mutex_lock);
    
    out:
    return retval;
}


ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval, buf_offset = 0;
    struct aesd_dev *dev;
    struct aesd_buffer_entry *new_entry;

    PDEBUG("write %zu bytes with offset %lld", count, *f_pos);
    dev = filp->private_data;
    if (mutex_lock_interruptible(&dev->mutex_lock))
    {
		retval -ERESTARTSYS;
        goto out;
    }

    /* Free memory for oldest entry is the buffer is full and previaous enrty was completed */
    if ((dev->circ_buffer->full == true) && (dev->write_entry_new_flag == TRUE))
    {
        struct aesd_buffer_entry *old_entry = 
            &dev->circ_buffer->entry[dev->circ_buffer->in_offs];
        if (old_entry->buffptr != NULL)
        {
            /* Remove size of an old entry from file pointer offset */
            *f_pos -= old_entry->size;
            kfree((void *)old_entry->buffptr);
            old_entry->buffptr = NULL;
            old_entry->size = 0;
        }
    }

    /* Allocate memory for a new entry struct pointer */
    new_entry = kmalloc(sizeof(struct aesd_buffer_entry), GFP_KERNEL);
    if (!new_entry)
    {
        retval = -ENOMEM;
        PDEBUG("kmalloc failed for new_entry");
        goto unlock;
    }

    new_entry->size = count;
    PDEBUG("-------------");
    PDEBUG("Requested write to aesd char device");
    PDEBUG("new_entry->size = %zu bytes", new_entry->size);
    PDEBUG("filp->f_pos %lld", filp->f_pos);
    PDEBUG("filp->f_pos = %lld", filp->f_pos);
    if (count == 0)
    {
        PDEBUG("write called with zero count, nothing to do");
        goto unlock;
    }

    if (dev->write_entry_new_flag == FALSE)
    {
        /* Append new data to the existing entry */
        dev->write_entry_new_flag = FALSE;
        buf_offset = dev->circ_buffer->entry[dev->circ_buffer->in_offs].size;
        new_entry->size = count + buf_offset;
        new_entry->buffptr = kmalloc((sizeof(char) * (count + buf_offset)), GFP_KERNEL);
        if (new_entry->buffptr <= 0)
        {
            retval = -ENOMEM;
            PDEBUG("kmalloc failed for new_entry->buffptr");
                goto unlock;
        }

        memcpy(new_entry->buffptr, 
                dev->circ_buffer->entry[dev->circ_buffer->in_offs].buffptr, 
                buf_offset);
        kfree(dev->circ_buffer->entry[dev->circ_buffer->in_offs].buffptr);
        dev->circ_buffer->entry[dev->circ_buffer->in_offs].buffptr = NULL;
        dev->circ_buffer->entry[dev->circ_buffer->in_offs].size = 0;
    }
    else /* dev->write_entry_new_flag == TRUE */
    {
        /* Don't append to existing entry */
        new_entry->buffptr = kmalloc((sizeof(char) * count), GFP_KERNEL);
        if (new_entry->buffptr <= 0)
        {
            retval = -ENOMEM;
            PDEBUG("kmalloc failed for new_entry->buffptr");
                goto unlock;
        }
    }

    if (copy_from_user((const char*)(new_entry->buffptr + buf_offset), buf, count))
    {
        retval = -EFAULT;
        PDEBUG("copy_from_user failed for new_entry->buffptr");
        kfree(new_entry->buffptr);
        new_entry->buffptr = NULL;
        goto unlock;
    }

    *f_pos += count; /* Update file position */

    /* Check if entry is complete */
    if (new_entry->buffptr[buf_offset + count - 1] != '\n')
    {
        PDEBUG("Entry not complete, setting new flag to FALSE");
        aesd_circular_buffer_add_entry(dev->circ_buffer, new_entry, FALSE, dev->write_entry_new_flag);
        dev->write_entry_new_flag = FALSE;
    }
    else
    {
        PDEBUG("Entry complete, setting new flag to TRUE");
        aesd_circular_buffer_add_entry(dev->circ_buffer, new_entry, TRUE, dev->write_entry_new_flag);
        dev->write_entry_new_flag = TRUE;
    }

    
    PDEBUG("--- WRITE IS DONE ----");
    PDEBUG("aesd_write debug info:");
    PDEBUG("dev->circ_buffer->in_offs %d", dev->circ_buffer->in_offs);
    PDEBUG("dev->circ_buffer->out_offs %d", dev->circ_buffer->out_offs);
    PDEBUG("dev->circ_buffer->full %d", dev->circ_buffer->full);
    PDEBUG("dev->write_entry_new_flag %d", dev->write_entry_new_flag);
    PDEBUG("write added buf = %s", new_entry->buffptr);
    PDEBUG("write added %zu bytes to circular buffer", (count + buf_offset));
    PDEBUG("filp->f_pos %lld", filp->f_pos);
    PDEBUG("-------------");

    retval = count;

    unlock:
    mutex_unlock(&dev->mutex_lock);
    
    out:
    return retval;
}

loff_t aesd_llseek(struct file *filp, loff_t off, int whence)
{
    aesd_dev *dev = filp->private_data;
    loff_t newpos;

    switch (whence) 
    {
        case SEEK_SET:
            newpos = dev->circ_buffer->out_offs;
            break;
     
        case SEEK_CUR:
            newpos = filp->f_pos + off;
            break;
     
        case SEEK_END:
            newpos = dev->offset;
            break;
     
        default: /* can't happen */
            return -EINVAL;
    }
}

int aesd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    struct aesd_dev *dev = filp->private_data;
    struct aesd_seekto seekto;
    int retval = 0;

    PDEBUG("aesd_ioctl called with cmd %u", cmd);

    if (_IOC_TYPE(cmd) != AESD_IOC_MAGIC)
        return -ENOTTY;

    if (_IOC_NR(cmd) > AESDCHAR_IOC_MAXNR)
        return -ENOTTY;

    switch (cmd) {
        case AESDCHAR_IOCSEEKTO:
            if (copy_from_user(&seekto, (struct aesd_seekto __user *)arg, sizeof(seekto)))
                return -EFAULT;

            PDEBUG("seek to write_cmd %u, offset %u", seekto.write_cmd, seekto.write_cmd_offset);
            break;

        default:
            return -ENOTTY;
    }

    return retval;
}

struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
    // .unlocked_ioctl = aesd_ioctl,
    // .llseek =   aesd_llseek,
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

    PDEBUG("aesdchar module init");
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
            "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }

    aesd_device = kmalloc(sizeof(struct aesd_dev), GFP_KERNEL);
    if (!aesd_device) {
        result = -ENOMEM;
        printk(KERN_ERR "kmalloc failed for aesd_device");
        goto fail;
    }

    memset(aesd_device,0,sizeof(struct aesd_dev));

    /**
     * TODO: initialize the AESD specific portion of the device
     */

    aesd_device->circ_buffer = kmalloc(sizeof(struct aesd_circular_buffer), GFP_KERNEL);
    memset(aesd_device->circ_buffer, 0, sizeof(struct aesd_circular_buffer));
    aesd_circular_buffer_init(aesd_device->circ_buffer);
    aesd_device->write_entry_new_flag = TRUE;
    mutex_init(&aesd_device->mutex_lock);

    result = aesd_setup_cdev(aesd_device);

    if( result )
    {
        unregister_chrdev_region(dev, 1);
    }
    return result;

    fail:
       aesd_cleanup_module();
       return result;
}


void aesd_cleanup_module(void)
{
    uint8_t index = 0U;
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    PDEBUG("cleanup module");
    cdev_del(&aesd_device->cdev);

    /**
     * TODO: cleanup AESD specific poritions here as necessary
     */
    mutex_destroy(&aesd_device->mutex_lock);
    for (index = 0; index < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; index++)
    {
        if (aesd_device->circ_buffer->entry[index].buffptr != NULL)
        {
            kfree(aesd_device->circ_buffer->entry[index].buffptr);
            aesd_device->circ_buffer->entry[index].buffptr = NULL;
        }
    }

    kfree(aesd_device->circ_buffer);
    kfree(aesd_device);

    unregister_chrdev_region(devno, 1);
}


module_init(aesd_init_module);
module_exit(aesd_cleanup_module);