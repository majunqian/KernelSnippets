// 本文件示例了在Linux内核模块中实现tar风格的打包和解包功能。
// 主要入口函数：
//   - pack_path: 将指定路径(文件或目录)打包成tar格式归档文件
//   - unpack_archive: 将tar格式归档解压到指定目录
// 支持普通文件和目录的递归处理。未处理符号链接、设备文件等特殊类型。

#include <linux/cred.h>
#include <linux/dcache.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/namei.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/uidgid.h>

#define PACK_HEADER_SIZE 512

static char pack_cmd[256] = { 0 };
module_param_string(cmd, pack_cmd, sizeof(pack_cmd), 0644);
MODULE_PARM_DESC(cmd, "Command: 'pack source dest' or 'unpack archive dest'");

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("TAR-like packing functionality in kernel space");

/*
 * ===================== Structure Definitions =====================
 */

/* TAR header structure */
struct pack_header {
	char name[100];		/* file name */
	char mode[8];		/* file mode */
	char uid[8];		/* owner user ID */
	char gid[8];		/* owner group ID */
	char size[12];		/* file size in bytes */
	char mtime[12];		/* last modification time */
	char chksum[8];		/* checksum */
	char typeflag;		/* file type */
	char linkname[100];	/* name of linked file */
	char magic[6];		/* magic number */
	char version[2];	/* version */
	char uname[32];		/* owner user name */
	char gname[32];		/* owner group name */
	char devmajor[8];	/* device major number */
	char devminor[8];	/* device minor number */
	char prefix[155];	/* path prefix */
	char padding[12];	/* padding to 512 bytes */
};

/* Pack operation structure */
struct pack_info {
	struct file *output_file;
	loff_t offset;
	size_t total_size;
};

/* Directory iteration context */
struct pack_dir_context {
	struct dir_context ctx;
	struct pack_info *pack;
	const char *base_path;
	int error;
};

/* Unpack operation structure */
struct unpack_info {
	struct file *input_file;
	loff_t offset;
	size_t total_size;
	const char *extract_path;
};

/*
 * ===================== Utility Functions =====================
 */

/* Convert number to octal string */
static void num_to_oct(char *dst, unsigned long num, int size)
{
	char tmp[32];
	int i = 0;
	int j = 0;

	if (num == 0) {
		memset(dst, '0', size - 1);
		dst[size - 1] = '\0';
		return;
	}

	while (num > 0 && i < sizeof(tmp) - 1) {
		tmp[i++] = '0' + (num & 7);
		num >>= 3;
	}

	memset(dst, '0', size - 1);
	for (j = 0; j < i && j < size - 1; j++)
		dst[size - 2 - j] = tmp[j];
	dst[size - 1] = '\0';
}

/* Convert octal string to number */
static unsigned long oct_to_num(const char *oct_str, int size)
{
	unsigned long result = 0;
	int i;

	for (i = 0; i < size - 1 && oct_str[i] != '\0' && 
	     oct_str[i] != ' '; i++) {
		if (oct_str[i] >= '0' && oct_str[i] <= '7')
			result = result * 8 + (oct_str[i] - '0');
	}

	return result;
}

/* Calculate checksum for header */
static unsigned int calculate_checksum(struct pack_header *header)
{
	unsigned int sum = 0;
	unsigned char *ptr = (unsigned char *)header;
	int i;

	/* Clear checksum field */
	memset(header->chksum, ' ', 8);

	/* Calculate sum */
	for (i = 0; i < PACK_HEADER_SIZE; i++)
		sum += ptr[i];

	return sum;
}

/* Verify TAR header checksum */
static int verify_checksum(struct pack_header *header)
{
	unsigned int calculated_sum = 0;
	unsigned int stored_sum;
	unsigned char *ptr = (unsigned char *)header;
	char chksum_backup[8];
	int i;

	/* Backup and clear checksum field */
	memcpy(chksum_backup, header->chksum, 8);
	memset(header->chksum, ' ', 8);

	/* Calculate sum */
	for (i = 0; i < PACK_HEADER_SIZE; i++)
		calculated_sum += ptr[i];

	/* Restore checksum field */
	memcpy(header->chksum, chksum_backup, 8);

	/* Get stored checksum */
	stored_sum = oct_to_num(header->chksum, sizeof(header->chksum));

	return (calculated_sum == stored_sum) ? 0 : -EINVAL;
}

/* Write data to output file */
static int write_to_file(struct pack_info *pack, const void *data, size_t len)
{
	ssize_t written;

	written = kernel_write(pack->output_file, data, len, &pack->offset);
	if (written != len)
		return -EIO;

	pack->total_size += len;
	return 0;
}

/* Read data from input file */
static int read_from_file(struct unpack_info *unpack, void *data, size_t len)
{
	ssize_t bytes_read;

	bytes_read = kernel_read(unpack->input_file, data, len, 
				 &unpack->offset);
	if (bytes_read != len)
		return -EIO;

	return 0;
}

/*
 * ===================== Packing Implementation =====================
 */

/* Create and write TAR header */
static int write_pack_header(struct pack_info *pack, const char *path,
			     struct kstat *stat, char type)
{
	struct pack_header header;
	unsigned int checksum;
	size_t path_len;

	memset(&header, 0, sizeof(header));

	/* Fill header fields with safe string operations */
	path_len = strlen(path);
	if (path_len >= sizeof(header.name)) {
		pr_warn("pack: Path too long, truncating: %s\n", path);
		path_len = sizeof(header.name) - 1;
	}
	strscpy(header.name, path, sizeof(header.name));

	num_to_oct(header.mode, stat->mode & 07777, sizeof(header.mode));
	num_to_oct(header.uid, from_kuid(&init_user_ns, stat->uid),
		   sizeof(header.uid));
	num_to_oct(header.gid, from_kgid(&init_user_ns, stat->gid),
		   sizeof(header.gid));
	num_to_oct(header.size, stat->size, sizeof(header.size));
	num_to_oct(header.mtime, stat->mtime.tv_sec, sizeof(header.mtime));

	header.typeflag = type;
	strncpy(header.magic, "ustar", sizeof(header.magic) - 1);
	strncpy(header.version, "00", sizeof(header.version) - 1);

	/* Calculate and set checksum */
	checksum = calculate_checksum(&header);
	num_to_oct(header.chksum, checksum, sizeof(header.chksum) - 1);
	header.chksum[sizeof(header.chksum) - 1] = ' ';

	return write_to_file(pack, &header, sizeof(header));
}

/* Pack a regular file */
static int pack_regular_file(struct pack_info *pack, const char *path)
{
	struct file *file;
	struct kstat stat;
	loff_t pos = 0;
	char *buffer = NULL;
	char *padding = NULL;
	ssize_t bytes_read;
	size_t remaining;
	int ret = 0;

	file = filp_open(path, O_RDONLY, 0);
	if (IS_ERR(file))
		return PTR_ERR(file);

	ret = vfs_getattr(&file->f_path, &stat, 
			  STATX_BASIC_STATS | STATX_BTIME,
			  AT_STATX_SYNC_AS_STAT);
	if (ret)
		goto out_close_file;

	/* Write header */
	ret = write_pack_header(pack, path, &stat, '0'); /* Regular file */
	if (ret)
		goto out_close_file;

	/* Allocate buffer for file content */
	buffer = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!buffer) {
		ret = -ENOMEM;
		goto out_close_file;
	}

	/* Copy file content */
	remaining = stat.size;
	while (remaining > 0) {
		size_t to_read = min_t(size_t, remaining, PAGE_SIZE);

		bytes_read = kernel_read(file, buffer, to_read, &pos);
		if (bytes_read <= 0) {
			ret = bytes_read < 0 ? bytes_read : -EIO;
			goto out_free_buffer;
		}

		ret = write_to_file(pack, buffer, bytes_read);
		if (ret)
			goto out_free_buffer;

		remaining -= bytes_read;
	}

	/* Pad to 512-byte boundary */
	if (stat.size % PACK_HEADER_SIZE) {
		size_t padding_size = PACK_HEADER_SIZE - 
				      (stat.size % PACK_HEADER_SIZE);
		padding = kzalloc(padding_size, GFP_KERNEL);
		if (!padding) {
			ret = -ENOMEM;
			goto out_free_buffer;
		}

		ret = write_to_file(pack, padding, padding_size);
		if (ret)
			goto out_free_padding;
	}

out_free_padding:
	kfree(padding);
out_free_buffer:
	kfree(buffer);
out_close_file:
	filp_close(file, NULL);
	return ret;
}

/* Pack a directory recursively */
static int pack_directory(struct pack_info *pack, const char *path);

/* Directory entry callback */
static int pack_dir_entry(struct dir_context *ctx, const char *name, 
			  int namlen, loff_t offset, u64 ino, 
			  unsigned int d_type)
{
	struct pack_dir_context *pack_ctx = container_of(ctx, 
					struct pack_dir_context, ctx);
	char *full_path;
	size_t base_len, total_len;
	int ret = 0;

	/* Skip . and .. entries */
	if (name[0] == '.' && (namlen == 1 || 
	    (namlen == 2 && name[1] == '.')))
		return 0;

	/* Construct full path with bounds checking */
	base_len = strlen(pack_ctx->base_path);
	total_len = base_len + namlen + 2; /* +2 for '/' and '\0' */

	if (total_len > PATH_MAX) {
		pack_ctx->error = -ENAMETOOLONG;
		return -ENAMETOOLONG;
	}

	full_path = kmalloc(total_len, GFP_KERNEL);
	if (!full_path) {
		pack_ctx->error = -ENOMEM;
		return -ENOMEM;
	}

	snprintf(full_path, total_len, "%s/%.*s", pack_ctx->base_path, 
		 namlen, name);

	/* Pack the entry based on its type */
	if (d_type == DT_REG) {
		ret = pack_regular_file(pack_ctx->pack, full_path);
	} else if (d_type == DT_DIR) {
		ret = pack_directory(pack_ctx->pack, full_path);
	}
	/*
	 * Add support for other file types if needed (symlinks, etc.)
	 */

	if (ret) {
		pack_ctx->error = ret;
		pr_err("pack: Failed to pack %s: %d\n", full_path, ret);
	}

	kfree(full_path);
	return ret;
}

/* Pack a directory recursively */
static int pack_directory(struct pack_info *pack, const char *path)
{
	struct path dir_path;
	struct kstat stat;
	struct file *dir_file = NULL;
	struct pack_dir_context pack_ctx;
	int ret;

	ret = kern_path(path, LOOKUP_FOLLOW, &dir_path);
	if (ret)
		return ret;

	ret = vfs_getattr(&dir_path, &stat, 
			  STATX_BASIC_STATS | STATX_BTIME,
			  AT_STATX_SYNC_AS_STAT);
	if (ret)
		goto out_put_path;

	/* Write directory header */
	ret = write_pack_header(pack, path, &stat, '5'); /* Directory */
	if (ret)
		goto out_put_path;

	/* Open directory for reading */
	dir_file = filp_open(path, O_RDONLY, 0);
	if (IS_ERR(dir_file)) {
		ret = PTR_ERR(dir_file);
		goto out_put_path;
	}

	/* Initialize directory iteration context */
	pack_ctx.ctx.actor = pack_dir_entry;
	pack_ctx.ctx.pos = 0;
	pack_ctx.pack = pack;
	pack_ctx.base_path = path;
	pack_ctx.error = 0;

	/* Iterate through directory entries */
	ret = iterate_dir(dir_file, &pack_ctx.ctx);
	if (ret == 0 && pack_ctx.error)
		ret = pack_ctx.error;

	filp_close(dir_file, NULL);
out_put_path:
	path_put(&dir_path);
	return ret;
}

/**
 * pack_path - 打包指定路径为tar格式归档文件
 * @source_path: 待打包的源路径（文件或目录）
 * @output_path: 输出tar归档文件路径
 *
 * 该函数打开源路径，获取其信息，根据类型调用相应的打包函数，
 * 最后写入归档文件并返回执行结果。
 *
 * 返回值：
 *   0  - 成功
 *  <0  - 内核错误码
 * 示例：
 *   pack_path("/root/tmp", "/tmp/tmp.tar");
 */
static int pack_path(const char *source_path, const char *output_path)
{
	struct pack_info pack;
	struct path path;
	struct kstat stat;
	int ret;
	char *zero_blocks = NULL;

    /* Open output file */
    pack.output_file = filp_open(output_path, 
                     O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (IS_ERR(pack.output_file))
        return PTR_ERR(pack.output_file);

    pack.offset = 0;
    pack.total_size = 0;

    /* Get source path info */
    ret = kern_path(source_path, LOOKUP_FOLLOW, &path);
    if (ret)
        goto out_close_output;

    ret = vfs_getattr(&path, &stat, STATX_BASIC_STATS | STATX_BTIME,
              AT_STATX_SYNC_AS_STAT);
    if (ret)
        goto out_put_path;

    /* Pack based on file type */
    if (S_ISREG(stat.mode)) {
        ret = pack_regular_file(&pack, source_path);
    } else if (S_ISDIR(stat.mode)) {
        ret = pack_directory(&pack, source_path);
    } else {
        ret = -EINVAL; /* Unsupported file type */
        goto out_put_path;
    }

    if (ret)
        goto out_put_path;

    /* Write end-of-archive markers (two zero blocks) */
    zero_blocks = kzalloc(PACK_HEADER_SIZE * 2, GFP_KERNEL);
    if (!zero_blocks) {
        ret = -ENOMEM;
        goto out_put_path;
    }
    ret = write_to_file(&pack, zero_blocks, PACK_HEADER_SIZE * 2);
    kfree(zero_blocks);
    if (ret)
        goto out_put_path;

	pr_info("pack: Successfully packed %s to %s (%zu bytes)\n",
		source_path, output_path, pack.total_size);

out_put_path:
	path_put(&path);
out_close_output:
	filp_close(pack.output_file, NULL);
	return ret;
}

/*
 * ===================== Unpacking Implementation =====================
 */

/* Create directory recursively */
static int create_directory_recursive(const char *path, umode_t mode)
{
	char *path_copy = NULL;
	char *parent_str = NULL;
	char *parent_dir;
	char *last_slash;
	struct path parent_path;
	struct path parent;
	struct dentry *dentry = NULL;
	struct inode *dir_inode;
	size_t parent_len;
	int ret = 0;

	path_copy = kstrdup(path, GFP_KERNEL);
	if (!path_copy)
		return -ENOMEM;

	/* Find parent directory */
	last_slash = strrchr(path_copy, '/');
	if (last_slash && last_slash != path_copy) {
		*last_slash = '\0';
		parent_dir = path_copy;

		/* Check if parent exists, create if not */
		ret = kern_path(parent_dir, LOOKUP_FOLLOW, &parent_path);
		if (ret) {
			/* Parent doesn't exist, create it recursively */
			ret = create_directory_recursive(parent_dir, 0755);
			if (ret)
				goto out_free_path_copy;
		} else {
			path_put(&parent_path);
		}
	}

	/* Check if target directory already exists */
	ret = kern_path(path, LOOKUP_FOLLOW, &parent_path);
	if (ret == 0) {
		/* Directory already exists */
		path_put(&parent_path);
		ret = 0;
		goto out_free_path_copy;
	}

	/* Directory doesn't exist, create it */
	last_slash = strrchr(path, '/');
	if (!last_slash || last_slash == path) {
		/* Root or invalid path */
		ret = -EINVAL;
		goto out_free_path_copy;
	}

	parent_len = last_slash - path;
	parent_str = kmalloc(parent_len + 1, GFP_KERNEL);
	if (!parent_str) {
		ret = -ENOMEM;
		goto out_free_path_copy;
	}
	strncpy(parent_str, path, parent_len);
	parent_str[parent_len] = '\0';

	ret = kern_path(parent_str, LOOKUP_FOLLOW, &parent);
	if (ret)
		goto out_free_parent_str;

	dir_inode = d_inode(parent.dentry);
	if (!dir_inode) {
		ret = -ENOENT;
		goto out_put_parent;
	}

	dentry = lookup_one_len(last_slash + 1, parent.dentry,
				strlen(last_slash + 1));
	if (IS_ERR(dentry)) {
		ret = PTR_ERR(dentry);
		goto out_put_parent;
	}

	ret = vfs_mkdir(dir_inode, dentry, mode);

	dput(dentry);
out_put_parent:
	path_put(&parent);
out_free_parent_str:
	kfree(parent_str);
out_free_path_copy:
	kfree(path_copy);
	return ret;
}

/* Extract regular file */
static int extract_regular_file(struct unpack_info *unpack,
				struct pack_header *header,
				const char *file_path)
{
	struct file *output_file = NULL;
	char *buffer = NULL;
	char *padding = NULL;
	size_t file_size;
	size_t remaining;
	size_t to_read;
	umode_t mode;
	kuid_t uid;
	kgid_t gid;
	int ret = 0;

	file_size = oct_to_num(header->size, sizeof(header->size));
	mode = oct_to_num(header->mode, sizeof(header->mode));
	uid = make_kuid(&init_user_ns, oct_to_num(header->uid, sizeof(header->uid)));
	gid = make_kgid(&init_user_ns, oct_to_num(header->gid, sizeof(header->gid)));

	/* Create output file */
	output_file = filp_open(file_path, O_WRONLY | O_CREAT | O_TRUNC, mode);
	if (IS_ERR(output_file))
		return PTR_ERR(output_file);

	buffer = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!buffer) {
		ret = -ENOMEM;
		goto out_close_file;
	}

	/* Copy file content */
	remaining = file_size;
	while (remaining > 0) {
		to_read = min_t(size_t, remaining, PAGE_SIZE);

		ret = read_from_file(unpack, buffer, to_read);
		if (ret)
			goto out_free_buffer;

		ret = kernel_write(output_file, buffer, to_read, NULL);
		if (ret < 0)
			goto out_free_buffer;

		remaining -= to_read;
	}

	/* Skip padding to 512-byte boundary */
	if (file_size % PACK_HEADER_SIZE) {
		size_t padding_size = PACK_HEADER_SIZE - 
				      (file_size % PACK_HEADER_SIZE);
		padding = kmalloc(padding_size, GFP_KERNEL);
		if (!padding) {
			ret = -ENOMEM;
			goto out_free_buffer;
		}

		ret = read_from_file(unpack, padding, padding_size);
		/* Continue even if padding read fails */
		kfree(padding);
	}

	/* Set owner and group */
	if (uid_valid(uid) && gid_valid(gid)) {
		ret = vfs_fchown(output_file, uid, gid);
		if (ret)
			pr_warn("pack: Failed to set owner/group for %s: %d\n", file_path, ret);
	}

	/* Set mode (permissions) */
	ret = vfs_fchmod(output_file, mode);
	if (ret)
		pr_warn("pack: Failed to set mode for %s: %d\n", file_path, ret);

	ret = 0; /* Success */

out_free_buffer:
	kfree(buffer);
out_close_file:
	filp_close(output_file, NULL);
	return ret;
}

/* Extract directory */
static int extract_directory(struct unpack_info *unpack,
			     struct pack_header *header, const char *dir_path)
{
	umode_t mode;
	kuid_t uid;
	kgid_t gid;
	struct path dir_p;
	int ret;

	mode = oct_to_num(header->mode, sizeof(header->mode));
	uid = make_kuid(&init_user_ns, oct_to_num(header->uid, sizeof(header->uid)));
	gid = make_kgid(&init_user_ns, oct_to_num(header->gid, sizeof(header->gid)));

	ret = create_directory_recursive(dir_path, mode);
	if (ret && ret != -EEXIST)
		return ret;

	/* Set owner/group/mode if directory exists */
	ret = kern_path(dir_path, LOOKUP_FOLLOW, &dir_p);
	if (ret)
		return ret;

	if (uid_valid(uid) && gid_valid(gid)) {
		int rc = vfs_chown(&dir_p, uid, gid);
		if (rc)
			pr_warn("pack: Failed to set owner/group for dir %s: %d\n", dir_path, rc);
	}
	ret = vfs_chmod(&dir_p, mode);
	if (ret)
		pr_warn("pack: Failed to set mode for dir %s: %d\n", dir_path, ret);

	path_put(&dir_p);
	return 0;
}

/**
 * unpack_archive - 解压归档文件到指定目录
 * @archive_path: 待解压的tar归档文件路径
 * @extract_path: 解压目标目录
 *
 * 该函数打开归档文件，循环读取pack_header头部，
 * 校验校验和和magic字段，根据typeflag分发到对应
 * 的提取函数（extract_regular_file或extract_directory）,
 * 处理完所有条目后关闭文件并返回执行结果。
 *
 * 返回值：
 *   0  - 成功
 *  <0  - 内核错误码
 * 示例：
 *   unpack_archive("/tmp/tmp.tar", "/tmp/extracted");
 */
static int unpack_archive(const char *archive_path, const char *extract_path)
{
	struct unpack_info unpack;
	struct pack_header header;
	char *full_path = NULL;
	size_t extract_path_len;
	int ret = 0;
	int entries_extracted = 0;
	const char *member_name;

	/* Open input file */
	unpack.input_file = filp_open(archive_path, O_RDONLY, 0);
	if (IS_ERR(unpack.input_file))
		return PTR_ERR(unpack.input_file);

	unpack.offset = 0;
	unpack.total_size = 0;
	unpack.extract_path = extract_path;

	extract_path_len = strlen(extract_path);

	/* Create extraction directory if it doesn't exist */
	ret = create_directory_recursive(extract_path, 0755);
	if (ret && ret != -EEXIST)
		goto out_close_input;

	/* Process archive entries */
	while (true) {
		/* Read header */
		ret = read_from_file(&unpack, &header, sizeof(header));
		if (ret) {
			if (ret == -EIO && entries_extracted > 0) {
				/* End of file reached */
				ret = 0;
			}
			break;
		}

		/* Check for end of archive (all zeros) */
		if (header.name[0] == '\0') {
			ret = 0;
			break;
		}

		/* Verify header */
		ret = verify_checksum(&header);
		if (ret) {
			pr_err("pack: Invalid checksum in header\n");
			break;
		}

		/* Check magic number */
		if (strncmp(header.magic, "ustar", 5) != 0) {
			pr_err("pack: Invalid magic number\n");
			ret = -EINVAL;
			break;
		}

		/* Construct full extraction path */
		member_name = header.name;
		if (member_name[0] == '/') {
			pr_info("pack: Removing leading '/' from member name: %s\n",
				member_name);
			member_name++; /* Skip the first '/' */
		}
		
		full_path = kmalloc(extract_path_len + strlen(header.name) + 2,
				    GFP_KERNEL);
		if (!full_path) {
			ret = -ENOMEM;
			break;
		}

		snprintf(full_path, 
			 extract_path_len + strlen(header.name) + 2,
			 "%s/%s", extract_path, header.name);

		/* Extract based on file type */
		switch (header.typeflag) {
		case '0': /* Regular file */
		case '\0': /* Also regular file in some implementations */
			ret = extract_regular_file(&unpack, &header, full_path);
			break;
		case '5': /* Directory */
			ret = extract_directory(&unpack, &header, full_path);
			break;
		default:
			pr_warn("pack: Unsupported file type: %c\n",
				header.typeflag);
			/* Skip unsupported file types */
			ret = 0;
		}

		if (ret) {
			pr_err("pack: Failed to extract %s: %d\n", 
			       header.name, ret);
			kfree(full_path);
			break;
		}

		pr_info("pack: Extracted %s\n", header.name);
		entries_extracted++;
		kfree(full_path);
		full_path = NULL;
	}

	if (full_path)
		kfree(full_path);

	if (!ret) {
		pr_info("pack: Successfully extracted %d entries from %s to %s\n",
			entries_extracted, archive_path, extract_path);
	}

out_close_input:
	filp_close(unpack.input_file, NULL);
	return ret;
}

/*
 * ===================== Module Entry/Exit =====================
 */

/*
 * insmod pack.ko cmd="pack /root/tmp /tmp/tmp.tar"
 * insmod pack.ko cmd="unpack /tmp/tmp.tar /tmp/"
 */
static int __init pack_init(void)
{
	char cmd[32], source[256], dest[256];
	int ret = 0;
	int parsed_items;

	pr_info("pack: Initializing module with command: %s\n", pack_cmd);

	/* Parse parameters */
	if (strlen(pack_cmd) > 0) {
		/* Use sscanf to parse command string */
		parsed_items = sscanf(pack_cmd, "%31s %255s %255s", 
				      cmd, source, dest);

		if (parsed_items != 3) {
			pr_err("pack: Invalid cmd format. Expected: 'cmd src dst' %s\n",
			       pack_cmd);
			return -EINVAL;
		}

		/* Execute command */
		if (strcmp(cmd, "pack") == 0) {
			ret = pack_path(source, dest);
			if (ret)
				pr_err("pack: Failed to pack %s to %s: %d\n",
				       source, dest, ret);
		} else if (strcmp(cmd, "unpack") == 0) {
			ret = unpack_archive(source, dest);
			if (ret)
				pr_err("pack: Failed to unpack %s to %s: %d\n",
				       source, dest, ret);
		} else {
			pr_err("pack: Unknown command: %s\n", cmd);
			ret = -EINVAL;
		}
	}

	if (ret)
		return ret;

	pr_info("pack: Module loaded successfully\n");
	return 0;
}

static void __exit pack_exit(void)
{
	pr_info("pack: Module unloaded\n");
}

module_init(pack_init);
module_exit(pack_exit);
