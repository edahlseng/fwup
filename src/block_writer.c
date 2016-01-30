#include "block_writer.h"
#include "util.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/**
 * @brief Helper for buffering writes into block-sized pieces
 *
 * This is needed since the unzip process creates variable sized chunks that need
 * to be combined before being written. File I/O to Flash media works best in 128 KB+
 * sized chunks, so cache the writes until they get to that size. Writes MUST be
 * monotically increasing in offset. Seeking backwards is NOT supported.
 *
 * Based on benchmarks and inspection of how Linux writes to Flash media at a low level,
 * the chunks don't need to be on Flash erase block boundaries or Flash erase block size.
 * This doesn't appear to hurt, but I found out that Linux was sending 120 KB blocks (NOT
 * 128 KB blocks) via a USB sniffer no matter how much larger blocks were written. Performance
 * was still very fast.
 *
 * This code also pads files out to block boundaries (512 bytes for all supported media now).
 * This appears to be needed on OSX when writing to /dev/rdiskN devices.
 *
 * @param aw
 * @param fd              the target file to write to
 * @param buffer_size     the size of the buffer (rounded to multiple of 2^log2_block_size)
 * @param log2_block_size 9 for 512 byte blocks
 * @return 0 if ok, -1 if not
 */
int block_writer_init(struct block_writer *bw, int fd, int buffer_size, int log2_block_size)
{
    bw->fd = fd;
    bw->block_size = (1 << log2_block_size);
    bw->block_size_mask = ~((off_t) bw->block_size - 1);
    bw->buffer_size = (buffer_size + ~bw->block_size_mask) & bw->block_size_mask;
    bw->buffer = (char *) malloc(bw->buffer_size);
    if (bw->buffer == 0)
        ERR_RETURN("Cannot allocate write buffer of %d bytes.", bw->buffer_size);

    bw->write_offset = 0;
    bw->buffer_index = 0;
    return 0;
}

static ssize_t flush_buffer(struct block_writer *bw)
{
    assert(bw->buffer_index <= bw->buffer_size); // Math failed somewhere if we're buffering more than the block_size

    // Make sure that we're writing a whole block or pad it with 0s just in case.
    // Note: this isn't necessary when writing through a cache to a device, but that's
    //       not always the case.
    size_t block_boundary = (bw->buffer_index + ~bw->block_size_mask) & bw->block_size_mask;
    ssize_t added_bytes = 0;
    if (block_boundary != bw->buffer_index) {
        added_bytes = block_boundary - bw->buffer_index;
        memset(&bw->buffer[bw->buffer_index], 0, added_bytes);
        bw->buffer_index = block_boundary;
    }

    ssize_t rc = pwrite(bw->fd, bw->buffer, bw->buffer_index, bw->write_offset);
    bw->write_offset += bw->buffer_index;
    bw->buffer_index = 0;

    // Don't report back the bytes that we padded the output by.
    if (rc > added_bytes)
        rc -= added_bytes;

    return rc;
}

/**
 * @brief Write a number of bytes to an offset like pwrite
 *
 * The offset must be monotonically increasing, but gaps are supported
 * when dealing with sparse files.
 *
 * @param bw
 * @param buf    the buffer
 * @param count  how many bytes to write
 * @param offset where to write in the file
 * @return -1 if error or the number of bytes written to Flash (if <count, then bytes were buffered)
 */
ssize_t block_writer_pwrite(struct block_writer *bw, const void *buf, size_t count, off_t offset)
{
    ssize_t amount_written = 0;
    if (bw->buffer_index) {
        // If we're buffering, fill in the buffer the rest of the way
        off_t position = bw->write_offset + bw->buffer_index;
        assert(offset >= position); // Check monotonicity of offset
        size_t to_write = bw->buffer_size - bw->buffer_index;
        if (offset > position) {
            off_t gap_size = offset - position;
            if (gap_size >= (off_t) to_write) {
                // Big gap, so flush the buffer
                ssize_t rc = flush_buffer(bw);
                if (rc < 0)
                    return rc;
                amount_written += rc;
                goto empty_buffer;
            } else {
                // Small gap, so fill it in with 0s and buffer more
                memset(&bw->buffer[bw->buffer_index], 0, gap_size);
                bw->buffer_index += gap_size;
                to_write -= gap_size;
            }
        }

        assert(offset == position);
        if (count < to_write) {
            // Not enough to write to disk, so buffer for next time
            memcpy(&bw->buffer[bw->buffer_index], buf, count);
            bw->buffer_index += count;
            return 0;
        } else {
            // Fill out the buffer
            memcpy(&bw->buffer[bw->buffer_index], buf, to_write);
            buf += to_write;
            count -= to_write;
            offset += to_write;
            bw->buffer_index += to_write;
            ssize_t rc = flush_buffer(bw);
            if (rc < 0)
                return rc;
            amount_written += rc;
        }
    }

empty_buffer:
    assert(bw->buffer_index == 0);
    assert(offset >= bw->write_offset);

    // Advance to the offset, but make sure that it's on a block boundary
    bw->write_offset = (offset & bw->block_size_mask);
    size_t padding = offset - bw->write_offset;
    if (padding > 0) {
        // NOTE: Padding both to the front and back seems like we could get into a situation
        //       where we overlap and wipe out some previously written data. This is impossible
        //       to do, though, since within a file, we're guaranteed to be monotonically
        //       increasing in our write offsets. Therefore, if we flush a block, we're guaranteed
        //       to never see that block again. Between files there could be a problem if the
        //       user configures overlapping blocks offsets between raw_writes. I can't think of
        //       a use case for this, but the config file format specifies everything in block
        //       offsets anyway.
        memset(bw->buffer, 0, padding);
        if (count + padding >= bw->block_size) {
            // Handle the block fragment
            size_t to_write = bw->block_size - padding;
            memcpy(&bw->buffer[padding], buf, to_write);
            if (pwrite(bw->fd, buf, bw->block_size, bw->write_offset) < 0)
                return -1;

            bw->write_offset += bw->block_size;
            amount_written += to_write;
            buf += to_write;
            count -= to_write;
        } else {
            // Buffer the block fragment for next time
            memcpy(&bw->buffer[padding], buf, count);
            bw->buffer_index = count + padding;
            return 0;
        }
    }

    // At this point, we're aligned to a block boundary
    assert((bw->write_offset & ~bw->block_size_mask) == 0);

    // If we have more than a buffer's worth, write all filled blocks
    // without even trying to buffer.
    if (count > bw->buffer_size) {
        size_t to_write = (count & bw->block_size_mask);
        amount_written += to_write;
        if (pwrite(bw->fd, buf, to_write, bw->write_offset) < 0)
            return -1;
        bw->write_offset += to_write;
        buf += to_write;
        count -= to_write;
    }

    // Buffer the left overs
    if (count) {
        memcpy(bw->buffer, buf, count);
        bw->buffer_index = count;
    }

    return amount_written;
}

/**
 * @brief flush and free the aligned writer data structures
 * @param bw
 * @return number of bytes written or -1 if error
 */
ssize_t block_writer_free(struct block_writer *bw)
{
    // Write any data that's still in the buffer
    ssize_t rc = 0;
    if (bw->buffer_index)
        rc = flush_buffer(bw);

    // Free our buffer and clean up
    free(bw->buffer);
    bw->fd = -1;
    bw->buffer = 0;
    bw->buffer_index = 0;
    return rc;
}
