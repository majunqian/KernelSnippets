#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/uaccess.h>
#include <linux/fcntl.h>
#include <linux/slab.h>
#include "../include/loggerfs.h"

// 物理日志方案：
// 1. 文件内容和日志都存储在同一个文件中，日志紧跟在数据后面
// 2. 使用标记来分隔数据和日志区域
// 3. 读取时自动过滤掉日志部分
// 4. stat显示的文件大小仅包含数据部分

// 初始化文件信息（从已有文件中解析数据和日志布局）
static void init_file_info_from_disk(struct loggerfs_file_info *file_info)
{
	struct inode *inode = &file_info->vfs_inode;
	loff_t physical_size = i_size_read(inode);
	loff_t log_start;

	// 查找日志开始位置
	log_start = find_log_start(inode);
	if (log_start >= 0) {
		// 找到了日志
		file_info->data_size = log_start;
		file_info->log_start = log_start;
		file_info->log_size = parse_log_size(inode, log_start);
		file_info->total_size = physical_size;
		
		// 更新inode的逻辑大小（仅数据部分）
		if (i_size_read(inode) != file_info->data_size) {
			i_size_write(inode, file_info->data_size);
		}
		
		pr_debug("File layout: data=%lld, log_start=%lld, log_size=%zu, total=%lld\n",
			 file_info->data_size, file_info->log_start, 
			 file_info->log_size, file_info->total_size);
	} else {
		// 没有日志，全部都是数据
		file_info->data_size = physical_size;
		file_info->log_start = physical_size;
		file_info->log_size = 0;
		file_info->total_size = physical_size;
		
		pr_debug("No log found, pure data file: size=%lld\n", file_info->data_size);
	}
}

// 文件读操作 - 只读取数据部分，过滤掉日志
static ssize_t loggerfs_read(struct file *file, char __user *buf, size_t count,
			     loff_t *ppos)
{
	struct inode *inode = file_inode(file);
	struct loggerfs_file_info *file_info =
		container_of(inode, struct loggerfs_file_info, vfs_inode);
	ssize_t ret;
	loff_t pos = *ppos;
	size_t copied = 0;

	// 确保文件信息是最新的
	init_file_info_from_disk(file_info);

	pr_debug("Read operation: pos=%lld, count=%zu, data_size=%lld\n", 
		 pos, count, file_info->data_size);

	// 检查读取范围（只能读取数据部分）
	if (pos >= file_info->data_size)
		return 0;

	if (pos + count > file_info->data_size)
		count = file_info->data_size - pos;

	if (count == 0)
		return 0;

	// 逐页读取文件内容（仅数据部分）
	while (copied < count) {
		pgoff_t page_idx = (pos + copied) >> PAGE_SHIFT;
		size_t page_offset = (pos + copied) & (PAGE_SIZE - 1);
		size_t copy_size = min_t(size_t, count - copied, PAGE_SIZE - page_offset);
		struct page *page;
		void *page_addr;

		page = find_get_page(inode->i_mapping, page_idx);
		if (!page) {
			// 页面不存在，填零（稀疏文件处理）
			if (clear_user(buf + copied, copy_size)) {
				if (copied)
					break;
				return -EFAULT;
			}
		} else {
			page_addr = kmap_atomic(page);
			if (copy_to_user(buf + copied, (char *)page_addr + page_offset, copy_size)) {
				kunmap_atomic(page_addr);
				put_page(page);
				if (copied)
					break;
				return -EFAULT;
			}
			kunmap_atomic(page_addr);
			put_page(page);
		}
		copied += copy_size;
	}

	ret = copied;
	*ppos = pos + copied;

	// 记录读操作日志
	if (ret > 0) {
		add_log_entry(file_info, "read", pos, ret);
	}

	pr_debug("Read completed: pos=%lld->%lld, read=%zd\n", pos, *ppos, ret);
	return ret;
}

// 文件写操作 - 写入数据部分，日志自动追加到文件末尾
static ssize_t loggerfs_write(struct file *file, const char __user *buf,
			      size_t count, loff_t *ppos)
{
	struct inode *inode = file_inode(file);
	struct loggerfs_file_info *file_info =
		container_of(inode, struct loggerfs_file_info, vfs_inode);
	loff_t pos = *ppos;
	ssize_t ret;
	size_t copied = 0;

	// 确保文件信息是最新的
	init_file_info_from_disk(file_info);

	pr_debug("Write operation: pos=%lld, count=%zu, data_size=%lld\n", 
		 pos, count, file_info->data_size);

	// 备份原始数据（用于revert功能）
	if (pos < file_info->data_size) {
		size_t backup_len = min_t(size_t, count, file_info->data_size - pos);
		backup_original_data(file_info, pos, backup_len);
	}

	// 如果写入位置超过当前数据大小，需要先清除日志区域
	if (pos > file_info->data_size) {
		// 简化处理：清空日志，扩展数据区域
		spin_lock(&file_info->log_lock);
		truncate_inode_pages(inode->i_mapping, file_info->data_size);
		file_info->log_size = 0;
		file_info->log_start = file_info->data_size;
		file_info->total_size = file_info->data_size;
		spin_unlock(&file_info->log_lock);
	}

	// 逐页写入文件内容
	while (copied < count) {
		pgoff_t page_idx = (pos + copied) >> PAGE_SHIFT;
		size_t page_offset = (pos + copied) & (PAGE_SIZE - 1);
		size_t copy_size = min_t(size_t, count - copied, PAGE_SIZE - page_offset);
		struct page *page;
		void *page_addr;

		// 获取或创建页面
		page = grab_cache_page(inode->i_mapping, page_idx);
		if (!page) {
			ret = copied ? copied : -ENOMEM;
			goto out;
		}

		// 如果页面不完整且我们不是写入整页，需要先处理现有内容
		if (!PageUptodate(page) && (page_offset || copy_size < PAGE_SIZE)) {
			page_addr = kmap_atomic(page);
			memset(page_addr, 0, PAGE_SIZE);
			kunmap_atomic(page_addr);
		}

		// 写入数据
		page_addr = kmap_atomic(page);
		if (copy_from_user((char *)page_addr + page_offset, buf + copied, copy_size)) {
			kunmap_atomic(page_addr);
			unlock_page(page);
			put_page(page);
			ret = copied ? copied : -EFAULT;
			goto out;
		}
		kunmap_atomic(page_addr);

		// 标记页面为最新和脏页
		SetPageUptodate(page);
		set_page_dirty(page);
		unlock_page(page);
		put_page(page);

		copied += copy_size;
	}

	ret = copied;
	*ppos = pos + copied;

	// 更新数据大小
	if (*ppos > file_info->data_size) {
		spin_lock(&file_info->log_lock);
		file_info->data_size = *ppos;
		file_info->log_start = file_info->data_size;
		// 注意：这里不更新i_size，因为物理文件大小包含日志
		spin_unlock(&file_info->log_lock);
	}

out:
	// 记录写操作日志（这会更新物理文件大小）
	if (ret > 0) {
		add_log_entry(file_info, "write", pos, ret);

		// 更新inode的逻辑大小（仅数据部分，供stat使用）
		i_size_write(inode, file_info->data_size);

		// 更新时间戳
		inode->i_mtime = inode->i_ctime = current_time(inode);
		mark_inode_dirty(inode);
	}

	pr_debug("Write completed: pos=%lld->%lld, written=%zd, data_size=%lld\n", 
		 pos, *ppos, ret, file_info->data_size);
	return ret;
}

// 文件截断操作
static int loggerfs_setattr(struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = d_inode(dentry);
	struct loggerfs_file_info *file_info =
		container_of(inode, struct loggerfs_file_info, vfs_inode);
	int ret;

	ret = setattr_prepare(dentry, attr);
	if (ret)
		return ret;

	// 处理文件大小变化（truncate操作）
	if (attr->ia_valid & ATTR_SIZE) {
		loff_t new_size = attr->ia_size;
		
		// 确保文件信息是最新的
		init_file_info_from_disk(file_info);
		
		pr_debug("Truncate operation: %lld->%lld\n", 
			 file_info->data_size, new_size);

		// 备份即将被截断的数据（用于revert功能）
		if (new_size < file_info->data_size) {
			backup_original_data(file_info, new_size, 
					    file_info->data_size - new_size);
		}

		spin_lock(&file_info->log_lock);

		// 截断页面
		truncate_inode_pages(inode->i_mapping, new_size);

		// 更新文件布局
		file_info->data_size = new_size;
		file_info->log_start = new_size;
		file_info->log_size = 0; // 截断时清空日志
		file_info->total_size = new_size;

		// 更新inode逻辑大小
		i_size_write(inode, new_size);

		spin_unlock(&file_info->log_lock);

		// 记录truncate操作日志
		add_log_entry(file_info, "truncate", new_size, 0);
	}

	setattr_copy(inode, attr);
	mark_inode_dirty(inode);
	return 0;
}

// ioctl操作 - 支持READLOG和REVERT命令
static long loggerfs_ioctl(struct file *file, unsigned int cmd,
			   unsigned long arg)
{
	struct inode *inode = file_inode(file);
	struct loggerfs_file_info *file_info =
		container_of(inode, struct loggerfs_file_info, vfs_inode);

	switch (cmd) {
	case READLOG_CMD: {
		char *log_buffer;
		size_t log_len;

		// 确保文件信息是最新的
		init_file_info_from_disk(file_info);

		if (file_info->log_size == 0) {
			return 0; // 没有日志
		}

		// 分配缓冲区读取日志内容
		log_buffer = kmalloc(file_info->log_size + 1, GFP_KERNEL);
		if (!log_buffer)
			return -ENOMEM;

		// 从文件读取日志内容
		if (read_from_file(inode, file_info->log_start, log_buffer, file_info->log_size) != 0) {
			kfree(log_buffer);
			return -EIO;
		}

		log_buffer[file_info->log_size] = '\0';

		// 查找日志内容（跳过开始标记，去除结束标记）
		char *log_content = strstr(log_buffer, LOG_START_MARKER);
		if (log_content) {
			log_content += strlen(LOG_START_MARKER);
			char *log_end = strstr(log_content, LOG_END_MARKER);
			if (log_end) {
				*log_end = '\0';
			}
			log_len = strlen(log_content);
		} else {
			log_len = 0;
		}

		// 将日志内容复制到用户空间
		if (log_len > 0) {
			if (copy_to_user((void __user *)arg, log_content, log_len)) {
				kfree(log_buffer);
				return -EFAULT;
			}
		}

		kfree(log_buffer);
		pr_debug("READLOG: returned %zu bytes of log data\n", log_len);
		return log_len;
	}

	case REVERT_CMD:
		// 撤销最后一次写操作
		pr_debug("REVERT: attempting to revert last write operation\n");
		return remove_last_write_log(file_info);

	default:
		return -ENOTTY;
	}
}

// 不需要自定义address_space_operations，使用默认的页缓存机制

// 文件操作结构体
const struct file_operations loggerfs_file_operations = {
	.read = loggerfs_read,
	.write = loggerfs_write,
	.unlocked_ioctl = loggerfs_ioctl,
	.llseek = generic_file_llseek,
	.mmap = generic_file_mmap,
	.open = generic_file_open,
};

// 文件inode操作结构体
const struct inode_operations loggerfs_file_inode_operations = {
	.setattr = loggerfs_setattr,
	.getattr = simple_getattr,
};
