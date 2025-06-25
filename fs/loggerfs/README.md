# LoggerFS - 带日志的文件系统

## 项目描述

LoggerFS是一个Linux内核模块，实现了一个带有自动日志记录功能的文件系统。该文件系统能够自动记录对文件的所有读写操作，并将日志保存在文件末尾。

## 功能特性

### 任务一：基本日志功能
- **自动日志记录**：对文件执行写操作时，自动维护文件末尾的日志
- **透明读取**：读文件时自动去除末尾的日志，用户只看到实际数据
- **日志提取**：通过fcntl的READLOG子命令可以提取文件的日志
- **正确的文件大小**：stat命令获取文件大小时，只包含数据长度，不包含日志部分
- **日志格式**：`时间戳 命令全路径 操作类型 偏移 数据长度`

### 任务二：撤销功能
- **Revert操作**：可以撤销最后一次写操作（类似Ctrl-Z功能）
- **日志清理**：撤销操作时会从日志中删除对应的写操作记录
- **原始数据备份**：在每次写操作前自动备份原始数据
- **完整数据恢复**：支持恢复到写操作前的确切内容
- **智能回退策略**：
  - 对于扩展文件的写操作：恢复到写操作前的文件大小
  - 对于中间位置的写操作：恢复原始数据内容
  - 无备份数据时：使用简单截断策略作为后备方案

### 技术特点
- **日志大小限制**：日志最大为一个磁盘块（4KB）
- **自动清理**：当日志超出容量时，自动移除最老的日志内容
- **内存高效**：使用页缓存进行文件数据管理

## 文件结构

```
fs/loggerfs/
├── src/                    # 源代码目录
│   ├── loggerfs_core.c     # 核心功能（日志管理）
│   ├── loggerfs_file.c     # 文件操作实现
│   ├── loggerfs_inode.c    # inode操作实现
│   ├── loggerfs_super.c    # 超级块和文件系统注册
│   └── logctl.c            # 用户空间工具源码
├── include/                # 头文件目录
│   └── loggerfs.h          # 主要头文件
├── tests/                  # 测试目录
│   ├── test_loggerfs.sh    # 完整功能测试脚本
│   ├── unit_test.sh        # 单元测试脚本
│   └── performance_test.sh # 性能测试脚本
├── Makefile                # 构建脚本
├── question.txt            # 原始需求描述
└── README.md               # 本文件
```

## 编译和安装

### 前提条件
- Linux系统（建议使用现代发行版）
- 安装了内核开发包（linux-headers）
- GCC编译器
- Root权限

### 编译
```bash
# 编译内核模块和用户空间工具
make

# 或者分别编译
make module      # 只编译内核模块
make userspace   # 只编译用户空间工具

# 查看所有可用命令
make help
```

### 安装和挂载
```bash
# 加载内核模块
sudo make install

# 挂载文件系统
sudo make mount

# 卸载文件系统
sudo make umount

# 卸载内核模块
sudo make uninstall
```

## 使用方法

### 基本文件操作
```bash
# 创建文件
touch /mnt/loggerfs/testfile

# 写入数据
echo "Hello World" > /mnt/loggerfs/testfile
dd if=/dev/zero of=/mnt/loggerfs/testfile bs=20 count=1 seek=0

# 读取数据（自动去除日志）
cat /mnt/loggerfs/testfile
dd if=/mnt/loggerfs/testfile of=/dev/null bs=20 count=1 skip=0
```

### 日志操作
```bash
# 读取文件的日志
./logctl /mnt/loggerfs/testfile readlog

# 撤销最后一次写操作
./logctl /mnt/loggerfs/testfile revert
```

### 自动化测试
```bash
# 运行基本功能测试（题目要求的各种操作）
make test

# 运行单元测试
make unittest

# 运行性能测试
make perftest

# 运行完整功能测试
make fulltest
```

## 测试案例

根据题目要求，测试包括以下操作：

1. **在偏移0处写入20字节**
   ```bash
   dd if=/dev/zero of=testfile bs=20 count=1 seek=0
   ```

2. **在偏移90处写入20字节**
   ```bash
   dd if=/dev/zero of=testfile bs=1 count=20 seek=90
   ```

3. **在偏移100处写入20字节**
   ```bash
   dd if=/dev/zero of=testfile bs=1 count=20 seek=100
   ```

4. **在文件末尾写入20字节**
   ```bash
   dd if=/dev/zero of=testfile bs=20 count=1 oflag=append
   ```

5. **在偏移2MB处写入20字节**
   ```bash
   dd if=/dev/zero of=testfile bs=1 count=20 seek=2097152
   ```

6. **从各个位置读取数据**
   ```bash
   dd if=testfile of=/dev/null bs=20 count=1 skip=0
   dd if=testfile of=/dev/null bs=1 count=20 skip=90
   ```

## 日志格式说明

日志采用纯文本格式，每行记录一个操作，格式为：
```
时间戳 命令全路径 操作类型 偏移 数据长度
```

示例：
```
1640995200 /bin/dd write 0 20
1640995201 /bin/dd write 90 20
1640995202 /bin/dd read 0 20
```

## 技术实现

### 内核模块架构
- **模块化设计**：代码分为核心、文件操作、inode操作和超级块操作四个模块
- **文件系统注册**：注册为"loggerfs"文件系统类型
- **内存管理**：使用slab缓存管理文件信息结构
- **页缓存集成**：与Linux页缓存系统集成，提供高效的文件I/O
- **日志管理**：动态管理日志缓冲区，自动处理溢出

### 关键数据结构
```c
struct backup_data {
    loff_t offset;          // 备份数据的偏移位置
    size_t length;          // 备份数据的长度
    char *original_data;    // 原始数据内容
    bool is_valid;          // 备份是否有效
};

struct loggerfs_file_info {
    struct inode vfs_inode; // VFS inode
    loff_t data_size;       // 实际数据大小
    loff_t total_size;      // 总大小（包含日志）
    char *log_buffer;       // 日志缓冲区
    size_t log_size;        // 当前日志大小
    struct backup_data backup; // 最后一次写操作的原始数据备份
};
```

### API接口
- **READLOG_CMD (0x1000)**：通过ioctl读取日志
- **REVERT_CMD (0x2000)**：通过ioctl撤销最后一次写操作

## 故障排除

### 常见问题

1. **编译失败**
   - 确保安装了正确版本的内核头文件
   - 检查内核版本兼容性

2. **挂载失败**
   - 确保内核模块已正确加载
   - 检查挂载点权限

3. **日志读取失败**
   - 确保使用正确的ioctl命令
   - 检查文件权限

### 调试信息
```bash
# 查看内核消息
dmesg | tail

# 查看已加载的模块
lsmod | grep loggerfs

# 查看挂载的文件系统
mount | grep loggerfs
```

## 清理环境

```bash
# 完整清理
sudo make umount    # 卸载文件系统
sudo make uninstall # 卸载内核模块
make clean          # 清理编译文件
```

## 注意事项

1. **权限要求**：需要root权限来加载内核模块和挂载文件系统
2. **内核兼容性**：代码针对现代Linux内核编写，可能需要针对特定内核版本调整
3. **安全性**：这是一个示例实现，生产环境使用需要额外的安全检查
4. **性能**：当前实现优先考虑功能完整性，性能优化可以进一步改进

## 开发指南

### 代码结构说明

- **src/loggerfs_core.c**: 核心功能实现，包括日志管理、进程命令获取等
- **src/loggerfs_file.c**: 文件操作实现，包括读写和ioctl操作
- **src/loggerfs_inode.c**: inode相关操作，包括setattr和目录操作
- **src/loggerfs_super.c**: 超级块操作和文件系统注册
- **include/loggerfs.h**: 共享的头文件，包含结构体定义和函数声明

### 测试说明

项目包含多种类型的测试：

1. **基本功能测试** (`make test`): 验证题目要求的基本功能
2. **单元测试** (`make unittest`): 系统性地测试各个功能模块
3. **性能测试** (`make perftest`): 测试文件系统的性能表现
4. **备份恢复测试** (`./tests/test_backup_restore.sh`): 专门测试新的数据备份和恢复功能

### 备份恢复功能测试

新增的备份恢复测试脚本验证以下功能：
- 写操作前的原始数据自动备份
- 完整的数据内容恢复（包括中间位置写操作）
- 文件大小的正确恢复
- 备份数据的内存管理和清理

运行备份恢复测试：
```bash
sudo ./tests/test_backup_restore.sh
```

### 扩展功能

您可以通过以下方式扩展LoggerFS：

1. **添加更多日志字段**: 修改`add_log_entry`函数
2. **支持更多ioctl命令**: 在`loggerfs_ioctl`中添加新的命令处理
3. **优化性能**: 改进页缓存使用和日志管理算法
4. **添加持久化**: 将日志保存到磁盘上的独立文件

## 许可证

GPL v2 许可证
