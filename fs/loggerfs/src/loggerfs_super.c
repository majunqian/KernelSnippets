#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/statfs.h>
#include <linux/mount.h>
#include <linux/module.h>
#include "../include/loggerfs.h"

struct kmem_cache *loggerfs_inode_cachep;

// inode操作函数 - 物理日志方案
static struct inode *loggerfs_alloc_inode(struct super_block *sb)
{
	struct loggerfs_file_info *file_info;

	file_info = kmem_cache_alloc(loggerfs_inode_cachep, GFP_KERNEL);
	if (!file_info) {
		pr_err("Failed to allocate loggerfs_file_info\n");
		return NULL;
	}

	// 初始化文件数据和日志布局信息
	file_info->data_size = 0;
	file_info->log_start = 0;
	file_info->log_size = 0;
	file_info->total_size = 0;

	// 初始化备份数据结构
	file_info->backup.offset = 0;
	file_info->backup.length = 0;
	file_info->backup.original_data = NULL;
	file_info->backup.is_valid = false;

	// 初始化日志操作锁
	spin_lock_init(&file_info->log_lock);

	pr_debug("Allocated inode with physical log support\n");
	return &file_info->vfs_inode;
}

static void loggerfs_destroy_inode(struct inode *inode)
{
	if (!inode)
		return;
	struct loggerfs_file_info *file_info =
		container_of(inode, struct loggerfs_file_info, vfs_inode);

	// 清理备份数据
	cleanup_backup_data(file_info);

	if (loggerfs_inode_cachep && file_info)
		kmem_cache_free(loggerfs_inode_cachep, file_info);
}

static int loggerfs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	buf->f_type = LOGGERFS_MAGIC;
	buf->f_bsize = PAGE_CACHE_SIZE;
	buf->f_namelen = 255;
	return 0;
}

// 超级块操作结构体
const struct super_operations loggerfs_ops = {
	.alloc_inode = loggerfs_alloc_inode,
	.destroy_inode = loggerfs_destroy_inode,
	.statfs = loggerfs_statfs,
	.drop_inode = generic_delete_inode,
};

// 挂载操作
static int loggerfs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct inode *inode;

	sb->s_maxbytes = MAX_LFS_FILESIZE;
	sb->s_blocksize = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_magic = LOGGERFS_MAGIC;
	sb->s_op = &loggerfs_ops;
	sb->s_time_gran = 1;

	inode = loggerfs_alloc_inode(sb); // 统一用loggerfs_alloc_inode
	if (!inode) {
		pr_err("Failed to allocate root inode\n");
		return -ENOMEM;
	}

	inode->i_ino = 1;
	inode->i_mode = S_IFDIR | 0755;
	inode->i_uid = current_fsuid();
	inode->i_gid = current_fsgid();
	inode->i_atime = inode->i_mtime = inode->i_ctime = current_time(inode);
	inode->i_op = &loggerfs_dir_inode_operations;
	inode->i_fop = &simple_dir_operations;
	set_nlink(inode, 2);

	sb->s_root = d_make_root(inode);
	if (!sb->s_root) {
		pr_err("Failed to create root dentry\n");
		return -ENOMEM;
	}

	return 0;
}

static struct dentry *loggerfs_mount(struct file_system_type *fs_type,
				     int flags, const char *dev_name,
				     void *data)
{
	return mount_nodev(fs_type, flags, data, loggerfs_fill_super);
}

static void loggerfs_kill_sb(struct super_block *sb)
{
	kill_litter_super(sb);
}

static struct file_system_type loggerfs_fs_type = {
	.owner = THIS_MODULE,
	.name = "loggerfs",
	.mount = loggerfs_mount,
	.kill_sb = loggerfs_kill_sb,
	.fs_flags = FS_USERNS_MOUNT,
};

// 缓存构造函数
static void init_once(void *foo)
{
	struct loggerfs_file_info *file_info = (struct loggerfs_file_info *)foo;
	inode_init_once(&file_info->vfs_inode);
}

// 模块初始化
static int __init loggerfs_init(void)
{
	int ret;

	loggerfs_inode_cachep = kmem_cache_create(
		"loggerfs_inode_cache", sizeof(struct loggerfs_file_info), 0,
		SLAB_RECLAIM_ACCOUNT | SLAB_MEM_SPREAD, init_once);
	if (!loggerfs_inode_cachep) {
		pr_err("Failed to create inode cache\n");
		return -ENOMEM;
	}

	ret = register_filesystem(&loggerfs_fs_type);
	if (ret) {
		kmem_cache_destroy(loggerfs_inode_cachep);
		pr_err("Failed to register loggerfs filesystem\n");
	}

	pr_info("Filesystem registered successfully\n");
	return ret;
}

// 模块清理
static void __exit loggerfs_exit(void)
{
	unregister_filesystem(&loggerfs_fs_type);
	kmem_cache_destroy(loggerfs_inode_cachep);
	pr_info("Filesystem unregistered\n");
}

module_init(loggerfs_init);
module_exit(loggerfs_exit);
