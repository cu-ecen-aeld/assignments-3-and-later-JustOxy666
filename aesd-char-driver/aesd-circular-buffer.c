/**
 * @file aesd-circular-buffer.c
 * @brief Functions and data related to a circular buffer imlementation
 *
 * @author Dan Walkes
 * @date 2020-03-01
 * @copyright Copyright (c) 2020
 *
 */

#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#endif

#include "aesd-circular-buffer.h"

/**
 * @param buffer the buffer to search for corresponding offset.  Any necessary locking must be performed by caller.
 * @param char_offset the position to search for in the buffer list, describing the zero referenced
 *      character index if all buffer strings were concatenated end to end
 * @param entry_offset_byte_rtn is a pointer specifying a location to store the byte of the returned aesd_buffer_entry
 *      buffptr member corresponding to char_offset.  This value is only set when a matching char_offset is found
 *      in aesd_buffer.
 * @return the struct aesd_buffer_entry structure representing the position described by char_offset, or
 * NULL if this position is not available in the buffer (not enough data is written).
 */
struct aesd_buffer_entry *aesd_circular_buffer_find_entry_offset_for_fpos(struct aesd_circular_buffer *buffer,
            size_t char_offset, size_t *entry_offset_byte_rtn )
{
    struct aesd_buffer_entry *entry = NULL;

    size_t buf_index = buffer->out_offs;
    size_t cnt = 0;
    size_t bytecnt = 0;
    while (cnt < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED)
    {   
        /* Check whether we need to roll-over */
        if (buf_index >= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED)
        {
            buf_index = 0;
        }

        /* Check if we are on requested entry */
        if ((bytecnt + buffer->entry[buf_index].size) > char_offset)
        {
            entry = &buffer->entry[buf_index];
            *entry_offset_byte_rtn = (char_offset - bytecnt);
            break;
        }

        bytecnt += buffer->entry[buf_index].size;
        cnt++;
        buf_index++;
    }

    return entry;
}

long aesd_buffer_find_offset(struct aesd_circular_buffer *buffer, uint32_t write_cmd, uint32_t write_cmd_offset)
{
    size_t buf_index = buffer->out_offs;
    size_t cnt = 0;
    long offset = 0;

    while (cnt < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED)
    {
        /* Check whether we need to roll-over */
        if (buf_index >= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED)
        {
            buf_index = 0;
        }

        /* Check if we are on requested entry */
        if (buf_index == write_cmd)
        {
            if (write_cmd_offset >= buffer->entry[buf_index].size)
            {
                /* Error: incorrect entry offset requested */
                offset = -1;
            }
            else
            {
                offset += write_cmd_offset;
            }

            break;
        }

        /* Check if we are on requested entry */
        if (buffer->entry[buf_index].size > 0 && write_cmd == cnt)
        {
            offset = write_cmd_offset;
            break;
        }

        offset += buffer->entry[buf_index].size;
        cnt++;
        buf_index++;
    }

    return offset;
}

/**
* Adds entry @param add_entry to @param buffer in the location specified in buffer->in_offs.
* If the buffer was already full, overwrites the oldest entry and advances buffer->out_offs to the
* new start location.
* Any necessary locking must be handled by the caller
* Any memory referenced in @param add_entry must be allocated by and/or must have a lifetime managed by the caller.
*/
void 
aesd_circular_buffer_add_entry(
    struct aesd_circular_buffer *buffer,
    const struct aesd_buffer_entry *add_entry,
    Boolean complete_entry,
    Boolean new_entry
)
{
    buffer->entry[buffer->in_offs] = *add_entry;
    if (new_entry == TRUE)
    {
        /* Move read point if buffer is full */
        if (buffer->full == true)
        {
            buffer->out_offs++;
            if (buffer->out_offs >= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED)
            {
                buffer->out_offs = 0;
            }
        }
    }

    if (complete_entry == TRUE)
    {
        /* Move write point to next location and hadle overrun */
        buffer->in_offs++;
        if (buffer->in_offs >= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED)
        {
            buffer->in_offs = 0;
        }

        /* Check if buffer is full */
        if (buffer->in_offs == buffer->out_offs)
        {
            buffer->full = true;
        }
        else
        {
            buffer->full = false;
        }
    }
}

/**
* Initializes the circular buffer described by @param buffer to an empty struct
*/
void aesd_circular_buffer_init(struct aesd_circular_buffer *buffer)
{
    memset(buffer,0,sizeof(struct aesd_circular_buffer));
}
