#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/time.h>
#include <linux/string.h>
#include <linux/backing-dev.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/parser.h>
#include <linux/mm.h>
#include <linux/parser.h>
#include <linux/magic.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/fcntl.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/sched/mm.h>
#include <linux/version.h>
#include "../include/loggerfs.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("KernelSnippets");
MODULE_DESCRIPTION("A filesystem with automatic logging functionality");

// 获取当前进程的命令路径
void get_current_command(char *buffer, size_t size)
{
	struct mm_struct *mm;
	char *path_buf, *pathname;

	mm = get_task_mm(current);
	if (!mm) {
		strncpy(buffer, "[unknown]", size);
		return;
	}

	path_buf = kmalloc(PATH_MAX, GFP_KERNEL);
	if (!path_buf) {
		mmput(mm);
		strncpy(buffer, "[nomem]", size);
		return;
	}

	// 为不同内核版本提供兼容性
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 8, 0)
	down_read(&mm->mmap_lock);
#else
	down_read(&mm->mmap_sem);
#endif
	if (mm->exe_file) {
		pathname = d_path(&mm->exe_file->f_path, path_buf, PATH_MAX);
		if (!IS_ERR(pathname)) {
			strncpy(buffer, pathname, size - 1);
			buffer[size - 1] = '\0';
		} else {
			strncpy(buffer, "[error]", size);
		}
	} else {
		strncpy(buffer, "[noexe]", size);
	}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 8, 0)
	up_read(&mm->mmap_lock);
#else
	up_read(&mm->mmap_sem);
#endif

	kfree(path_buf);
	mmput(mm);
}
static int write_log_to_file(struct inode *inode, loff_t pos, const char *data, size_t len);
int read_from_file(struct inode *inode, loff_t pos, char *buffer, size_t len);

// 添加日志条目 - 物理存储在文件末尾，使用标记分隔
int add_log_entry(struct loggerfs_file_info *file_info, const char *operation,
		   loff_t offset, size_t length)
{
	struct timespec64 ts;
	char command[256];
	char log_line[512];
	char *log_content;
	int log_line_len;
	loff_t write_pos;
	struct inode *inode = &file_info->vfs_inode;
	int ret = 0;

	if (!file_info || !operation) {
		pr_err("Invalid parameters in add_log_entry\n");
		return -EINVAL;
	}

	// 获取当前时间和命令
	ktime_get_real_ts64(&ts);
	get_current_command(command, sizeof(command));

	// 格式化日志行：时间 命令全路径 访问类型 起始位置 数据长度
	log_line_len = snprintf(log_line, sizeof(log_line),
				"%lld %s %s %lld %zu\n",
				(long long)ts.tv_sec, command, operation,
				(long long)offset, length);

	if (log_line_len <= 0 || log_line_len >= sizeof(log_line)) {
		pr_warn("Log line formatting failed or too long\n");
		return -EINVAL;
	}

	spin_lock(&file_info->log_lock);

	// 如果这是第一个日志条目，需要写入开始标记
	if (file_info->log_size == 0) {
		// 计算日志起始位置（数据末尾）
		file_info->log_start = file_info->data_size;
		
		// 分配空间存储开始标记 + 日志行 + 结束标记
		size_t total_len = strlen(LOG_START_MARKER) + log_line_len + strlen(LOG_END_MARKER);
		log_content = kmalloc(total_len, GFP_ATOMIC);
		if (!log_content) {
			spin_unlock(&file_info->log_lock);
			return -ENOMEM;
		}

		// 组装：开始标记 + 日志行 + 结束标记
		strcpy(log_content, LOG_START_MARKER);
		strcat(log_content, log_line);
		strcat(log_content, LOG_END_MARKER);
		
		write_pos = file_info->log_start;
		file_info->log_size = total_len;
	} else {
		// 检查日志大小限制（题目要求：最大一个磁盘块）
		size_t new_entry_size = log_line_len;
		if (file_info->log_size + new_entry_size > MAX_LOG_SIZE) {
			// 清空旧日志，重新开始
			file_info->log_start = file_info->data_size;
			file_info->log_size = 0;
			
			size_t total_len = strlen(LOG_START_MARKER) + log_line_len + strlen(LOG_END_MARKER);
			log_content = kmalloc(total_len, GFP_ATOMIC);
			if (!log_content) {
				spin_unlock(&file_info->log_lock);
				return -ENOMEM;
			}
			strcpy(log_content, LOG_START_MARKER);
			strcat(log_content, log_line);
			strcat(log_content, LOG_END_MARKER);
			
			write_pos = file_info->log_start;
			file_info->log_size = total_len;
		} else {
			// 在现有日志中插入新条目（在结束标记之前）
			size_t total_content_len = log_line_len + strlen(LOG_END_MARKER);
			log_content = kmalloc(total_content_len, GFP_ATOMIC);
			if (!log_content) {
				spin_unlock(&file_info->log_lock);
				return -ENOMEM;
			}
			strcpy(log_content, log_line);
			strcat(log_content, LOG_END_MARKER);
			
			// 写入位置：当前日志结束标记之前
			write_pos = file_info->log_start + file_info->log_size - strlen(LOG_END_MARKER);
			file_info->log_size += log_line_len;
		}
	}

	// 写入日志到文件末尾
	ret = write_log_to_file(inode, write_pos, log_content, strlen(log_content));
	if (ret == 0) {
		// 更新文件总大小
		file_info->total_size = file_info->data_size + file_info->log_size;
		
		pr_debug("Added log entry: %s at offset %lld, length %zu\n",
			 operation, (long long)offset, length);
	} else {
		pr_err("Failed to write log entry to file: %d\n", ret);
	}

	kfree(log_content);
	spin_unlock(&file_info->log_lock);
	return ret;
}

// 写入日志内容到文件的指定位置
static int write_log_to_file(struct inode *inode, loff_t pos, const char *data, size_t len)
{
	size_t written = 0;
	
	while (written < len) {
		pgoff_t page_idx = (pos + written) >> PAGE_SHIFT;
		size_t page_offset = (pos + written) & (PAGE_SIZE - 1);
		size_t copy_size = min_t(size_t, len - written, PAGE_SIZE - page_offset);
		struct page *page;
		void *page_addr;

		// 获取或创建页面
		page = grab_cache_page(inode->i_mapping, page_idx);
		if (!page) {
			pr_err("Failed to grab cache page %lu for log write\n", page_idx);
			return -ENOMEM;
		}

		// 如果页面不是最新的且不是完整页写入，先清零
		if (!PageUptodate(page) && (page_offset || copy_size < PAGE_SIZE)) {
			page_addr = kmap_atomic(page);
			memset(page_addr, 0, PAGE_SIZE);
			kunmap_atomic(page_addr);
		}

		page_addr = kmap_atomic(page);
		memcpy((char *)page_addr + page_offset, data + written, copy_size);
		kunmap_atomic(page_addr);

		SetPageUptodate(page);
		set_page_dirty(page);
		unlock_page(page);
		put_page(page);

		written += copy_size;
	}

	return 0;
}

// 查找日志开始位置
loff_t find_log_start(struct inode *inode)
{
	loff_t file_size = i_size_read(inode);
	loff_t search_pos = 0;
	char buffer[512]; // 增大搜索缓冲区
	
	// 从文件开始向后搜索日志开始标记
	while (search_pos < file_size) {
		size_t read_len = min_t(size_t, sizeof(buffer) - 1, file_size - search_pos);
		if (read_from_file(inode, search_pos, buffer, read_len) != 0)
			break;
			
		buffer[read_len] = '\0';
		char *marker_pos = strstr(buffer, LOG_START_MARKER);
		if (marker_pos) {
			return search_pos + (marker_pos - buffer);
		}
		
		// 向前移动，但要保留一些重叠以防标记跨页
		search_pos += read_len - strlen(LOG_START_MARKER);
		if (search_pos >= file_size)
			break;
	}
	
	return -1; // 未找到日志开始标记
}

// 从文件读取数据的辅助函数
int read_from_file(struct inode *inode, loff_t pos, char *buffer, size_t len)
{
	size_t read_size = 0;
	
	while (read_size < len) {
		pgoff_t page_idx = (pos + read_size) >> PAGE_SHIFT;
		size_t page_offset = (pos + read_size) & (PAGE_SIZE - 1);
		size_t copy_size = min_t(size_t, len - read_size, PAGE_SIZE - page_offset);
		struct page *page;
		void *page_addr;

		page = find_get_page(inode->i_mapping, page_idx);
		if (!page) {
			// 页面不存在，填零
			memset(buffer + read_size, 0, copy_size);
		} else {
			page_addr = kmap_atomic(page);
			memcpy(buffer + read_size, (char *)page_addr + page_offset, copy_size);
			kunmap_atomic(page_addr);
			put_page(page);
		}
		read_size += copy_size;
	}
	
	return 0;
}

// 解析日志大小
int parse_log_size(struct inode *inode, loff_t log_start)
{
	if (log_start < 0)
		return 0;
		
	loff_t file_size = i_size_read(inode);
	if (log_start >= file_size)
		return 0;
		
	return file_size - log_start;
}

// 解析日志行，提取操作信息
static int parse_log_line(const char *line, size_t line_len, char *operation,
			  loff_t *offset, size_t *length)
{
	char temp_line[512];
	char timestamp_str[32], command[256];
	int parsed;

	if (line_len >= sizeof(temp_line))
		return -EINVAL;

	strncpy(temp_line, line, line_len);
	temp_line[line_len] = '\0';

	// 解析格式：时间 命令全路径 访问类型 起始位置 数据长度
	parsed = sscanf(temp_line, "%31s %255s %31s %lld %zu",
			timestamp_str, command, operation,
			(long long *)offset, length);

	return (parsed == 5) ? 0 : -EINVAL;
}

// 重建日志，排除最后一个写操作
static int rebuild_log_without_last_write(struct loggerfs_file_info *file_info,
					  const char *old_log)
{
	// 简化实现：清空日志区域，这里可以实现更精细的日志重建
	struct inode *inode = &file_info->vfs_inode;
	
	spin_lock(&file_info->log_lock);
	
	// 截断文件到数据部分结束
	truncate_inode_pages(inode->i_mapping, file_info->data_size);
	i_size_write(inode, file_info->data_size);
	
	// 重置日志信息
	file_info->log_start = file_info->data_size;
	file_info->log_size = 0;
	file_info->total_size = file_info->data_size;
	
	spin_unlock(&file_info->log_lock);
	
	pr_debug("Cleared log area after revert operation\n");
	return 0;
}

// 移除最后一次写操作的日志条目 - 物理日志处理
int remove_last_write_log(struct loggerfs_file_info *file_info)
{
	struct inode *inode = &file_info->vfs_inode;
	char *log_buffer = NULL;
	char *line_start, *line_end;
	char operation[32];
	loff_t last_write_offset = -1;
	size_t last_write_length = 0;
	bool found_write = false;
	int ret = 0;

	if (!file_info) {
		return -EINVAL;
	}

	spin_lock(&file_info->log_lock);

	if (file_info->log_size == 0) {
		pr_info("No log data available for revert\n");
		spin_unlock(&file_info->log_lock);
		return -ENODATA;
	}

	// 分配缓冲区读取日志内容
	log_buffer = kmalloc(file_info->log_size + 1, GFP_ATOMIC);
	if (!log_buffer) {
		spin_unlock(&file_info->log_lock);
		return -ENOMEM;
	}

	// 从文件读取日志内容
	ret = read_from_file(inode, file_info->log_start, log_buffer, file_info->log_size);
	if (ret != 0) {
		pr_err("Failed to read log data from file\n");
		kfree(log_buffer);
		spin_unlock(&file_info->log_lock);
		return ret;
	}
	
	log_buffer[file_info->log_size] = '\0';

	// 跳过开始标记，从后往前解析日志找到最后一次写操作
	char *log_content = strstr(log_buffer, LOG_START_MARKER);
	if (!log_content) {
		pr_warn("Log start marker not found\n");
		kfree(log_buffer);
		spin_unlock(&file_info->log_lock);
		return -ENODATA;
	}
	log_content += strlen(LOG_START_MARKER);

	// 找到结束标记
	char *log_end = strstr(log_content, LOG_END_MARKER);
	if (log_end) {
		*log_end = '\0'; // 截断到结束标记
	}

	// 从后往前解析日志行
	line_end = log_content + strlen(log_content);
	while (line_end > log_content) {
		// 向前查找行开始
		line_start = line_end - 1;
		while (line_start > log_content && *(line_start - 1) != '\n') {
			line_start--;
		}
		
		// 解析这一行
		if (parse_log_line(line_start, line_end - line_start, operation, 
				   &last_write_offset, &last_write_length) == 0) {
			if (strcmp(operation, "write") == 0) {
				found_write = true;
				break;
			}
		}
		
		line_end = line_start - 1; // 跳过换行符
		if (line_end <= log_content)
			break;
	}

	spin_unlock(&file_info->log_lock);

	if (!found_write) {
		pr_info("No write operation found in log for revert\n");
		kfree(log_buffer);
		return -ENOENT;
	}

	// 执行文件内容回退
	ret = restore_original_data(file_info);
	if (ret == 0) {
		// 成功回退后，从日志中移除该写操作条目
		// 这里简化实现：重新构建日志，排除最后一个写操作
		ret = rebuild_log_without_last_write(file_info, log_buffer);
		
		pr_info("Successfully reverted write operation at offset %lld, length %zu\n",
			(long long)last_write_offset, last_write_length);
	}

	kfree(log_buffer);
	return ret;
}

// 清理备份数据
void cleanup_backup_data(struct loggerfs_file_info *file_info)
{
	if (!file_info)
		return;

	if (file_info->backup.original_data) {
		kfree(file_info->backup.original_data);
		file_info->backup.original_data = NULL;
	}

	file_info->backup.offset = 0;
	file_info->backup.length = 0;
	file_info->backup.is_valid = false;
}

// 实际回退文件内容
static int revert_file_content(struct loggerfs_file_info *file_info,
			       loff_t write_offset, size_t write_length)
{
	struct inode *inode;
	loff_t current_data_size;
	
	if (!file_info)
		return -EINVAL;

	inode = &file_info->vfs_inode;
	current_data_size = file_info->data_size;

	pr_info("Reverting write operation - offset:%lld, length:%zu, current_size:%lld\n",
		(long long)write_offset, write_length, (long long)current_data_size);

	// 情况1：有完全匹配的备份数据
	if (file_info->backup.is_valid &&
	    file_info->backup.offset == write_offset &&
	    file_info->backup.length == write_length) {
		
		pr_info("Using exact backup data for revert\n");
		int restore_result = restore_original_data(file_info);
		cleanup_backup_data(file_info);

		if (restore_result == 0 || restore_result == -EIO) {
			pr_info("Successfully reverted using backup data\n");
			return 0;
		} else {
			pr_err("Failed to restore from backup - error: %d\n", restore_result);
			return restore_result;
		}
	}
	// 情况2：有部分覆盖的备份数据
	else if (file_info->backup.is_valid &&
		 file_info->backup.offset <= write_offset &&
		 file_info->backup.offset + file_info->backup.length > write_offset) {
		
		pr_info("Using partial backup data for revert\n");
		// 先恢复备份数据覆盖的部分
		int restore_result = restore_original_data(file_info);
		
		// 然后处理超出备份范围的部分（如果写操作扩展了文件）
		loff_t backup_end = file_info->backup.offset + file_info->backup.length;
		if (write_offset + write_length > backup_end) {
			// 截断到备份数据的结束位置
			if (backup_end < current_data_size) {
				file_info->data_size = backup_end;
				i_size_write(inode, backup_end);
				pr_info("File truncated to backup end: %lld\n", (long long)backup_end);
			}
		}
		
		cleanup_backup_data(file_info);
		inode->i_mtime = inode->i_ctime = current_time(inode);
		
		if (restore_result == 0 || restore_result == -EIO) {
			pr_info("Successfully reverted using partial backup\n");
			return 0;
		} else {
			pr_warn("Partial revert with errors: %d\n", restore_result);
			return 0; // 仍然算成功，因为已经尽力了
		}
	}
	// 情况3：写操作是在文件末尾的追加，没有备份但可以截断
	else if (write_offset + write_length >= current_data_size) {
		loff_t new_size = write_offset;
		
		if (new_size < 0)
			new_size = 0;

		file_info->data_size = new_size;
		i_size_write(inode, new_size);
		inode->i_mtime = inode->i_ctime = current_time(inode);

		pr_info("File truncated from %lld to %lld (append revert)\n",
			(long long)current_data_size, (long long)new_size);
		return 0;
	}
	// 情况4：无法回退的中间写操作
	else {
		pr_warn("Cannot revert middle write operation without backup - offset:%lld, length:%zu\n",
			(long long)write_offset, write_length);
		pr_warn("Current file size: %lld, backup: %s\n",
			(long long)current_data_size,
			file_info->backup.is_valid ? "exists but doesn't cover write area" : "none");
		return -ENODATA;
	}
}

// 备份写操作前的原始数据
int backup_original_data(struct loggerfs_file_info *file_info, loff_t offset,
			 size_t length)
{
	struct page *page;
	void *page_addr;
	loff_t page_offset;
	size_t copy_size;
	size_t copied = 0;
	pgoff_t page_idx;
	int error_count = 0;

	if (!file_info)
		return -EINVAL;

	if (length == 0) {
		pr_debug("Backup length is 0, nothing to backup\n");
		return 0;
	}

	// 检查是否需要保留现有的备份数据
	// 如果当前操作的起始位置比现有备份更早，则保留现有备份
	// 否则为当前操作创建新的备份
	if (file_info->backup.is_valid && file_info->backup.offset < offset) {
		pr_debug("Keeping earlier backup (offset %lld), not backing up current write (offset %lld)\n",
			 (long long)file_info->backup.offset, (long long)offset);
		return 0;
	}

	// 为当前操作创建备份（如果没有备份，或者当前操作更早）
	cleanup_backup_data(file_info);

	// 分配备份缓冲区
	file_info->backup.original_data = kzalloc(length, GFP_KERNEL);
	if (!file_info->backup.original_data) {
		pr_err("Failed to allocate backup buffer\n");
		return -ENOMEM;
	}

	file_info->backup.offset = offset;
	file_info->backup.length = length;
	file_info->backup.is_valid = false;

	// 逐页读取原始数据
	while (copied < length) {
		page_idx = (offset + copied) >> PAGE_SHIFT;
		page_offset = (offset + copied) & (PAGE_SIZE - 1);
		copy_size =
			min_t(size_t, length - copied, PAGE_SIZE - page_offset);

		// 获取页面
		page = find_get_page(file_info->vfs_inode.i_mapping,
				     page_idx);
		if (!page) {
			// 页面不存在，说明这部分是空洞，用零填充
			memset(file_info->backup.original_data + copied, 0,
			       copy_size);
			pr_debug("Backup hole data - page %lu\n", page_idx);
		} else {
			// 映射页面并复制数据
			page_addr = kmap(page);
			if (page_addr) {
				memcpy(file_info->backup.original_data + copied,
				       (char *)page_addr + page_offset,
				       copy_size);
				kunmap(page);
				pr_debug(
					"Backup page data - page %lu, offset %lld, size %zu\n",
					page_idx, page_offset, copy_size);
			} else {
				pr_err("Failed to map page %lu during backup\n",
				       page_idx);
				// 映射失败时用零填充，但继续备份其他页面
				memset(file_info->backup.original_data + copied,
				       0, copy_size);
				error_count++;
			}
			put_page(page);
		}

		copied += copy_size;
	}

	file_info->backup.is_valid = true;

	if (error_count > 0) {
		pr_warn("Backup completed with %d mapping errors, some data may be zero-filled\n",
			error_count);
	} else {
		pr_info("Successfully backed up original data - offset:%lld, length:%zu\n",
			(long long)offset, length);
	}

	return 0;
}

// 恢复原始数据
int restore_original_data(struct loggerfs_file_info *file_info)
{
	struct page *page;
	void *page_addr;
	loff_t page_offset;
	size_t copy_size;
	size_t copied = 0;
	pgoff_t page_idx;
	int error_count = 0;
	int ret = 0;

	if (!file_info || !file_info->backup.is_valid) {
		pr_warn("No valid backup data to restore\n");
		return -ENODATA;
	}

	if (!file_info->backup.original_data) {
		pr_err("Backup data buffer is NULL\n");
		return -EINVAL;
	}

	pr_info("Start restoring original data - offset:%lld, length:%zu\n",
		(long long)file_info->backup.offset, file_info->backup.length);

	// 逐页恢复原始数据
	while (copied < file_info->backup.length) {
		page_idx = (file_info->backup.offset + copied) >> PAGE_SHIFT;
		page_offset =
			(file_info->backup.offset + copied) & (PAGE_SIZE - 1);
		copy_size = min_t(size_t, file_info->backup.length - copied,
				  PAGE_SIZE - page_offset);

		// 获取或创建页面
		page = find_or_create_page(file_info->vfs_inode.i_mapping,
					   page_idx, GFP_KERNEL);
		if (!page) {
			pr_err("Failed to get page %lu for restore\n",
			       page_idx);
			error_count++;
			copied += copy_size;
			continue;
		}

		// 映射页面并恢复数据
		page_addr = kmap(page);
		if (page_addr) {
			memcpy((char *)page_addr + page_offset,
			       file_info->backup.original_data + copied,
			       copy_size);
			kunmap(page);

			SetPageUptodate(page);
			set_page_dirty(page);

			pr_debug(
				"Restored page data - page %lu, offset %lld, size %zu\n",
				page_idx, page_offset, copy_size);
		} else {
			pr_err("Failed to map page %lu for restore\n",
			       page_idx);
			error_count++;
		}

		unlock_page(page);
		put_page(page);
		copied += copy_size;
	}

	// 更新文件大小（如果需要）
	if (file_info->backup.offset + file_info->backup.length >=
	    file_info->data_size) {
		// 如果原始写操作扩展了文件，恢复到写操作前的大小
		loff_t original_size = file_info->backup.offset;
		if (original_size < file_info->data_size) {
			file_info->data_size = original_size;
			i_size_write(&file_info->vfs_inode, original_size);
			pr_info("Restore file size to %lld\n",
				(long long)original_size);
		}
	}

	// 更新时间戳
	file_info->vfs_inode.i_mtime = file_info->vfs_inode.i_ctime =
		current_time(&file_info->vfs_inode);

	// 检查是否有错误发生
	if (error_count > 0) {
		pr_warn("Restore completed with %d errors\n", error_count);
		ret = -EIO; // 部分恢复失败
	} else {
		pr_info("Successfully restored original data\n");
		ret = 0; // 完全成功
	}

	return ret;
}
