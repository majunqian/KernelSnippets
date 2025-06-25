# LoggerFS 测试脚本使用说明

本目录包含用于测试和管理LoggerFS文件系统的脚本。

## 脚本列表

### 1. test_loggerfs.sh - 主测试脚本
完整的LoggerFS功能测试脚本，包含编译、加载、测试和清理功能。

**用法：**
```bash
sudo ./test_loggerfs.sh          # 运行完整测试
sudo ./test_loggerfs.sh test     # 运行完整测试
sudo ./test_loggerfs.sh cleanup  # 仅执行清理
sudo ./test_loggerfs.sh help     # 显示帮助信息
```

**功能：**
- 自动编译内核模块和用户空间工具
- 加载LoggerFS内核模块
- 创建挂载点并挂载文件系统
- 执行各种文件操作测试
- 测试日志读取和撤销功能
- 自动清理环境

### 2. cleanup_loggerfs.sh - 专用清理脚本
独立的清理脚本，用于清理LoggerFS测试环境。

**用法：**
```bash
sudo ./cleanup_loggerfs.sh              # 详细清理（默认）
sudo ./cleanup_loggerfs.sh --detailed   # 详细清理
sudo ./cleanup_loggerfs.sh --quick      # 快速清理
sudo ./cleanup_loggerfs.sh --status     # 显示当前状态
sudo ./cleanup_loggerfs.sh --help       # 显示帮助信息
```

**功能：**
- 检查并卸载LoggerFS文件系统
- 检查并卸载LoggerFS内核模块
- 清理挂载点目录
- 显示当前系统状态
- 提供多种清理策略

### 3. revert_demo.sh - Revert功能演示脚本
专门演示LoggerFS的撤销功能。

**用法：**
```bash
sudo ./revert_demo.sh
```

**功能：**
- 演示完整的revert操作流程
- 展示如何撤销最后一次写操作
- 显示文件内容和大小的变化
- 展示日志记录的变化

### 4. test_backup_restore.sh - 备份恢复功能测试脚本
专门测试LoggerFS的数据备份和恢复功能的详细测试脚本。

**用法：**
```bash
sudo ./test_backup_restore.sh
```

**功能：**
- 测试写操作前的原始数据自动备份
- 验证完整的数据内容恢复（包括中间位置写操作）
- 测试文件大小的正确恢复
- 验证备份数据的内存管理和清理
- 展示智能回退策略的不同情况
- 检查内核日志中的详细调试信息

**测试场景：**
- 基本写操作的备份和恢复
- 文件追加操作的回退
- 文件中间位置写操作的回退
- 连续多次写操作的备份覆盖
- 错误处理和资源清理

**技术文档：**
参见 `BACKUP_RESTORE_TECHNICAL.md` 了解详细的实现原理和技术细节。

### 测试流程

### 正常测试流程
1. 运行测试脚本：
   ```bash
   sudo ./test_loggerfs.sh
   ```

2. 脚本会自动执行以下步骤：
   - 预清理环境
   - 编译内核模块和用户空间工具
   - 加载内核模块
   - 挂载文件系统
   - 执行各种测试操作
   - 读取操作日志
   - 测试撤销功能
   - 自动清理环境

### 手动清理
如果测试脚本异常退出或需要单独清理：

1. 使用专用清理脚本：
   ```bash
   sudo ./cleanup_loggerfs.sh
   ```

2. 或使用测试脚本的清理功能：
   ```bash
   sudo ./test_loggerfs.sh cleanup
   ```

3. 手动清理命令：
   ```bash
   sudo umount /mnt/loggerfs
   sudo rmmod loggerfs
   sudo rmdir /mnt/loggerfs
   ```

### 故障排除

#### 挂载点占用
如果遇到"device or resource busy"错误：
```bash
# 检查占用进程
sudo lsof /mnt/loggerfs
sudo fuser -v /mnt/loggerfs

# 强制卸载
sudo umount -f /mnt/loggerfs
sudo umount -l /mnt/loggerfs  # 延迟卸载
```

#### 内核模块占用
如果无法卸载内核模块：
```bash
# 检查模块依赖
lsmod | grep loggerfs

# 强制卸载
sudo rmmod -f loggerfs
```

#### 权限问题
确保以root权限运行所有脚本：
```bash
sudo su
./test_loggerfs.sh
```

## 注意事项

1. **权限要求**：所有脚本都需要root权限运行
2. **内核兼容性**：确保当前内核支持加载自定义模块
3. **清理重要性**：测试完成后务必清理环境，避免影响系统
4. **日志查看**：使用`dmesg`命令查看内核日志信息
5. **编译依赖**：确保系统安装了内核开发包和编译工具

## 测试输出

测试脚本会产生以下输出：
- 编译过程信息
- 模块加载状态
- 文件操作结果
- 操作日志内容
- 撤销操作结果
- 内核消息摘要
- 清理过程信息

## 故障诊断

如果测试失败，请检查：
1. 是否有root权限
2. 内核开发包是否安装
3. 编译是否成功
4. 内核模块是否正确加载
5. 挂载点是否可用
6. 查看dmesg输出获取详细错误信息
