/*
 * aesdchar.h
 *
 *  Created on: Oct 23, 2019
 *      Author: Dan Walkes
 */

#include "aesd-circular-buffer.h"

#ifndef AESD_CHAR_DRIVER_AESDCHAR_H_
#define AESD_CHAR_DRIVER_AESDCHAR_H_

#define AESD_DEBUG 1  //Remove comment on this line to enable debug

#undef PDEBUG             /* undef it, just in case */
#ifdef AESD_DEBUG
#  ifdef __KERNEL__
     /* This one if debugging is on, and kernel space */
#    define PDEBUG(fmt, args...) printk( KERN_DEBUG "aesdchar: " fmt, ## args)
#  else
     /* This one for user space */
#    define PDEBUG(fmt, args...) fprintf(stderr, fmt, ## args)
#  endif
#else
#  define PDEBUG(fmt, args...) /* not debugging: nothing */
#endif

struct aesd_dev
{
    Boolean write_entry_new_flag; /* Flag to indicate new write buffer entry */
    struct aesd_circular_buffer *circ_buffer; /* Circular buffer for AESD data */
    struct mutex mutex_lock; /* Mutex for synchronizing access to the circular buffer */
    struct cdev cdev;     /* Char device structure      */
    unsigned long size;       /* amount of data stored here */
};


#endif /* AESD_CHAR_DRIVER_AESDCHAR_H_ */
