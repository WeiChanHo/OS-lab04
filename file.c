#include <linux/fs.h>
#include <linux/uaccess.h>
#include "osfs.h"

/**
 * Function: osfs_read
 * Description: Reads data from a file.
 * Inputs:
 *   - filp: The file pointer representing the file to read from.
 *   - buf: The user-space buffer to copy the data into.
 *   - len: The number of bytes to read.
 *   - ppos: The file position pointer.
 * Returns:
 *   - The number of bytes read on success.
 *   - 0 if the end of the file is reached.
 *   - -EFAULT if copying data to user space fails.
 */
static ssize_t osfs_read(struct file *filp, char __user *buf, size_t len, loff_t *ppos)
{
    struct inode *inode = file_inode(filp);
    struct osfs_inode *osfs_inode = inode->i_private;
    struct osfs_sb_info *sb_info = inode->i_sb->s_fs_info;
    void *data_block;
    ssize_t bytes_read;

    // If the file has not been allocated a data block, it indicates the file is empty
    if (osfs_inode->i_blocks == 0)
        return 0;

    if (*ppos >= osfs_inode->i_size)
        return 0;

    if (*ppos + len > osfs_inode->i_size)
        len = osfs_inode->i_size - *ppos;

    data_block = sb_info->data_blocks + osfs_inode->i_block * BLOCK_SIZE + *ppos;
    if (copy_to_user(buf, data_block, len))
        return -EFAULT;

    *ppos += len;
    bytes_read = len;

    return bytes_read;
}


/**
 * Function: osfs_write
 * Description: Writes data to a file.
 * Inputs:
 *   - filp: The file pointer representing the file to write to.
 *   - buf: The user-space buffer containing the data to write.
 *   - len: The number of bytes to write.
 *   - ppos: The file position pointer.
 * Returns:
 *   - The number of bytes written on success.
 *   - -EFAULT if copying data from user space fails.
 *   - Adjusted length if the write exceeds the block size.
 */
static ssize_t osfs_write(struct file *filp, const char __user *buf, size_t len, loff_t *ppos)
{   
    //Step1: Retrieve the inode and filesystem information
    struct inode *inode = file_inode(filp);
    struct osfs_inode *osfs_inode = inode->i_private;
    struct osfs_sb_info *sb_info = inode->i_sb->s_fs_info;
    void *data_block;
    ssize_t bytes_written = 0;
    int ret;
    uint32_t logical_block;
    uint32_t phys_block;
    uint32_t offset_in_block;
    uint32_t to_write;

    while (len > 0) {
        logical_block = *ppos / sb_info->block_size;
        offset_in_block = *ppos % sb_info->block_size;
        to_write = sb_info->block_size - offset_in_block;
        if (to_write > len)
            to_write = len;

        phys_block = 0;

        if (logical_block < 10) {
            // Direct Pointer
            if (osfs_inode->i_block[logical_block] == 0) {
                ret = osfs_alloc_data_block(sb_info, &osfs_inode->i_block[logical_block]);
                if (ret) return ret;
                osfs_inode->i_blocks++;
                inode->i_blocks++;
            }
            phys_block = osfs_inode->i_block[logical_block];
        } else {
            // Indirect Pointer
            /* 處理 Indirect Pointer (索引 > 9) */
            
            // 1. 檢查是否已配置 Indirect Block (i_block[10])
            if (osfs_inode->i_block[10] == 0) {
                ret = osfs_alloc_data_block(sb_info, &osfs_inode->i_block[10]);
                if (ret) return ret;
                
                // 初始化 Indirect Block 為 0
                memset(sb_info->data_blocks + osfs_inode->i_block[10] * sb_info->block_size, 0, sb_info->block_size);
                
                osfs_inode->i_blocks++;
                inode->i_blocks++;
            }

            // 2. 讀取 Indirect Block 表格
            uint32_t *indirect_table = (uint32_t *)(sb_info->data_blocks + osfs_inode->i_block[10] * sb_info->block_size);
            uint32_t indirect_index = logical_block - 10;

            // 3. 檢查目標資料塊是否已配置
            if (indirect_table[indirect_index] == 0) {
                ret = osfs_alloc_data_block(sb_info, &indirect_table[indirect_index]);
                if (ret) return ret;
                osfs_inode->i_blocks++;
                inode->i_blocks++;
            }
            phys_block = indirect_table[indirect_index];
        }

        // Write data
        data_block = sb_info->data_blocks + phys_block * sb_info->block_size + offset_in_block;
        if (copy_from_user(data_block, buf, to_write))
            return -EFAULT;

        *ppos += to_write;
        buf += to_write;
        len -= to_write;
        bytes_written += to_write;

        if (*ppos > osfs_inode->i_size) {
            osfs_inode->i_size = *ppos;
            inode->i_size = *ppos;
        }
    }

    osfs_inode->__i_mtime = osfs_inode->__i_ctime = current_time(inode);
    inode_set_mtime_to_ts(inode, osfs_inode->__i_mtime);
    inode_set_ctime_to_ts(inode, osfs_inode->__i_ctime);
    mark_inode_dirty(inode);

    return bytes_written;
}

/**
 * Struct: osfs_file_operations
 * Description: Defines the file operations for regular files in osfs.
 */
const struct file_operations osfs_file_operations = {
    .open = generic_file_open, // Use generic open or implement osfs_open if needed
    .read = osfs_read,
    .write = osfs_write,
    .llseek = default_llseek,
    // Add other operations as needed
};

/**
 * Struct: osfs_file_inode_operations
 * Description: Defines the inode operations for regular files in osfs.
 * Note: Add additional operations such as getattr as needed.
 */
const struct inode_operations osfs_file_inode_operations = {
    // Add inode operations here, e.g., .getattr = osfs_getattr,
};
