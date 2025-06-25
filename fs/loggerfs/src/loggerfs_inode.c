#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/stat.h>
#include <linux/time.h>
#include "../include/loggerfs.h"

// 外部声明 - 在 loggerfs_super.c 中定义
extern struct kmem_cache *loggerfs_inode_cachep;

// 创建新的loggerfs文件info结构 - 物理日志方案
static struct loggerfs_file_info *loggerfs_alloc_inode(struct super_block *sb)
{
	struct loggerfs_file_info *file_info;

	file_info = kmem_cache_alloc(loggerfs_inode_cachep, GFP_KERNEL);
	if (!file_info)
		return NULL;

	// 初始化文件数据和日志布局信息
	file_info->data_size = 0;
	file_info->log_start = 0;
	file_info->log_size = 0;
	file_info->total_size = 0;

	// 初始化备份数据结构
	file_info->backup.is_valid = false;
	file_info->backup.original_data = NULL;
	file_info->backup.offset = 0;
	file_info->backup.length = 0;

	// log_lock已在alloc_inode中初始化，无需重复初始化

	pr_debug("Allocated new loggerfs inode with physical log support\n");
	return file_info;
}

// 目录操作 - 创建文件
static int loggerfs_create(struct inode *dir, struct dentry *dentry,
			   umode_t mode, bool excl)
{
	struct loggerfs_file_info *file_info;
	struct inode *inode;

	file_info = loggerfs_alloc_inode(dir->i_sb);
	if (!file_info) {
		pr_err("Failed to allocate loggerfs_file_info for file creation\n");
		return -ENOSPC;
	}

	inode = &file_info->vfs_inode;
	inode_init_once(inode);
	inode->i_sb = dir->i_sb;
	inode->i_mode = mode | S_IFREG;
	inode->i_uid = current_fsuid();
	inode->i_gid = current_fsgid();
	inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);
	inode->i_ino = get_next_ino();
	inode->i_op = &loggerfs_file_inode_operations;
	inode->i_fop = &loggerfs_file_operations;
	inode->i_size = 0;

	d_instantiate(dentry, inode);
	dget(dentry);

	pr_debug("Created new loggerfs file: %s\n", dentry->d_name.name);
	return 0;
}

// 目录操作 - 创建目录
static int loggerfs_mkdir(struct inode *dir, struct dentry *dentry,
			  umode_t mode)
{
	struct loggerfs_file_info *file_info;
	struct inode *inode;

	file_info = loggerfs_alloc_inode(dir->i_sb);
	if (!file_info) {
		pr_err("Failed to allocate loggerfs_file_info for directory creation\n");
		return -ENOSPC;
	}

	inode = &file_info->vfs_inode;
	inode_init_once(inode);
	inode->i_sb = dir->i_sb;
	inode->i_mode = mode | S_IFDIR;
	inode->i_uid = current_fsuid();
	inode->i_gid = current_fsgid();
	inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);
	inode->i_op = &simple_dir_inode_operations;
	inode->i_fop = &simple_dir_operations;
	inc_nlink(inode);

	d_instantiate(dentry, inode);
	dget(dentry);
	inc_nlink(dir);

	pr_debug("Created new directory: %s\n", dentry->d_name.name);
	return 0;
}

// 特殊文件创建（设备文件、FIFO等）
static int loggerfs_mknod(struct inode *dir, struct dentry *dentry,
			  umode_t mode, dev_t dev)
{
	struct loggerfs_file_info *file_info;
	struct inode *inode;

	file_info = loggerfs_alloc_inode(dir->i_sb);
	if (!file_info) {
		pr_err("Failed to allocate loggerfs_file_info for inode\n");
		return -ENOSPC;
	}

	inode = &file_info->vfs_inode;
	inode_init_once(inode);
	inode->i_sb = dir->i_sb;
	inode->i_ino = get_next_ino();

	if (S_ISREG(mode)) {
		inode->i_op = &loggerfs_file_inode_operations;
		inode->i_fop = &loggerfs_file_operations;
		inode->i_size = 0;
	} else {
		init_special_inode(inode, mode, dev);
	}

	inode->i_mode = mode;
	inode->i_uid = current_fsuid();
	inode->i_gid = current_fsgid();
	inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);

	d_instantiate(dentry, inode);
	dget(dentry);

	pr_debug("Created inode: %s (mode=0%o)\n", dentry->d_name.name, mode);
	return 0;
}

// 目录inode操作结构体
const struct inode_operations loggerfs_dir_inode_operations = {
	.create = loggerfs_create,
	.lookup = simple_lookup,
	.link = simple_link,
	.unlink = simple_unlink,
	.mkdir = loggerfs_mkdir,
	.rmdir = simple_rmdir,
	.mknod = loggerfs_mknod,
	.rename = simple_rename,
};
