#ifndef LOGGERFS_H
#define LOGGERFS_H

#include <linux/fs.h>
#include <linux/types.h>

/* LoggerFS 魔数和常量 */
#define LOGGERFS_MAGIC 0x858458f6
#define MAX_LOG_SIZE 4096
#define MAX_LOG_ENTRIES 50

/* ioctl 命令定义 */
#define READLOG_CMD 0x1000
#define REVERT_CMD 0x2000

/* 日志边界标记 */
#define LOG_START_MARKER "<<<LOGGERFS_LOG_START>>>\n"
#define LOG_END_MARKER "<<<LOGGERFS_LOG_END>>>\n"
#define LOG_MARKER_LEN 26

/* 原始数据备份结构 */
struct backup_data {
	loff_t offset;          // 备份数据的偏移位置
	size_t length;          // 备份数据的长度
	char *original_data;    // 原始数据内容
	bool is_valid;          // 备份是否有效
};

/* 文件私有数据结构 - 物理日志方案 */
struct loggerfs_file_info {
	struct inode vfs_inode; // VFS inode必须是第一个成员
	
	// 文件数据和日志的物理布局信息
	loff_t data_size;       // 数据部分大小（stat显示的大小）
	loff_t log_start;       // 日志开始位置（数据末尾）
	size_t log_size;        // 日志部分大小
	loff_t total_size;      // 文件总大小（数据+日志）
	
	struct backup_data backup; // 最后一次写操作的原始数据备份
	spinlock_t log_lock;    // 日志操作锁
};

/* 函数声明 */
void get_current_command(char *buffer, size_t size);
int add_log_entry(struct loggerfs_file_info *file_info, const char *operation,
		   loff_t offset, size_t length);
int remove_last_write_log(struct loggerfs_file_info *file_info);
int backup_original_data(struct loggerfs_file_info *file_info, loff_t offset, size_t length);
int restore_original_data(struct loggerfs_file_info *file_info);
void cleanup_backup_data(struct loggerfs_file_info *file_info);
loff_t find_log_start(struct inode *inode);
int parse_log_size(struct inode *inode, loff_t log_start);
int read_from_file(struct inode *inode, loff_t pos, char *buffer, size_t len);

/* 文件操作函数声明 */
extern const struct file_operations loggerfs_file_operations;
extern const struct inode_operations loggerfs_file_inode_operations;
extern const struct inode_operations loggerfs_dir_inode_operations;
extern const struct super_operations loggerfs_ops;

/* 内核版本兼容性宏 */
#include <linux/version.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0)
#define PAGE_CACHE_SIZE PAGE_SIZE
#define PAGE_CACHE_SHIFT PAGE_SHIFT
#define page_cache_release put_page
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0)
#define CURRENT_TIME current_time(inode)
#else
#define CURRENT_TIME CURRENT_TIME_SEC
#endif

#endif /* LOGGERFS_H */
