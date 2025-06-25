#!/bin/bash

# LoggerFS 文件追加功能测试脚本
# 专门测试页缓存方式下的文件追加操作

set -e

MOUNT_POINT="/mnt/loggerfs"
TEST_FILE="$MOUNT_POINT/append_test"

echo "=== LoggerFS 文件追加测试 ==="

# 检查是否以root权限运行
if [[ $EUID -ne 0 ]]; then
   echo "此脚本需要root权限运行"
   exit 1
fi

# 获取脚本所在目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOGGERFS_DIR="$(dirname "$SCRIPT_DIR")"

echo "1. 确保文件系统已挂载..."
if ! mountpoint -q $MOUNT_POINT 2>/dev/null; then
    echo "   文件系统未挂载，正在挂载..."
    mkdir -p $MOUNT_POINT
    
    # 检查模块是否已加载
    if ! lsmod | grep -q "loggerfs"; then
        echo "   加载内核模块..."
        cd "$LOGGERFS_DIR"
        make clean && make
        insmod loggerfs.ko
    fi
    
    mount -t loggerfs none $MOUNT_POINT
fi

echo "2. 创建测试文件..."
rm -f $TEST_FILE
touch $TEST_FILE

echo "3. 测试基本文件追加操作..."

echo "   3.1 写入初始数据（20字节）..."
echo -n "initial data 20bytes" > $TEST_FILE
ls -la $TEST_FILE
echo "   文件大小应该是20字节"

echo "   3.2 追加数据（append模式，15字节）..."
echo -n " appended data." >> $TEST_FILE
ls -la $TEST_FILE
echo "   文件大小应该是35字节"

echo "   3.3 使用dd在末尾追加数据（20字节）..."
dd if=/dev/zero of=$TEST_FILE bs=20 count=1 oflag=append 2>/dev/null
ls -la $TEST_FILE
echo "   文件大小应该是55字节"

echo "   3.4 使用dd在特定偏移写入（在偏移100处写入30字节）..."
dd if=/dev/zero of=$TEST_FILE bs=1 count=30 seek=100 2>/dev/null
ls -la $TEST_FILE
echo "   文件大小应该是130字节（包含空洞）"

echo "   3.5 在文件中间覆盖写入（偏移10处写入10字节）..."
echo -n "0123456789" | dd of=$TEST_FILE bs=1 seek=10 conv=notrunc 2>/dev/null
ls -la $TEST_FILE
echo "   文件大小应该仍是130字节"

echo "4. 测试读取操作..."

echo "   4.1 读取前20字节..."
dd if=$TEST_FILE of=/dev/null bs=20 count=1 2>/dev/null
echo "   读取完成"

echo "   4.2 读取偏移100处的30字节..."
dd if=$TEST_FILE of=/dev/null bs=1 count=30 skip=100 2>/dev/null
echo "   读取完成"

echo "5. 检查日志..."
if [ -f "$LOGGERFS_DIR/logctl" ]; then
    echo "   当前操作日志:"
    "$LOGGERFS_DIR/logctl" $TEST_FILE readlog
else
    echo "   警告: logctl工具未编译"
fi

echo "6. 测试文件截断..."

echo "   6.1 截断到50字节..."
truncate -s 50 $TEST_FILE
ls -la $TEST_FILE
echo "   文件大小应该是50字节"

echo "   6.2 截断到200字节（扩展文件）..."
truncate -s 200 $TEST_FILE
ls -la $TEST_FILE
echo "   文件大小应该是200字节"

echo "7. 最终日志检查..."
if [ -f "$LOGGERFS_DIR/logctl" ]; then
    echo "   所有操作的日志:"
    "$LOGGERFS_DIR/logctl" $TEST_FILE readlog
    
    echo ""
    echo "8. 测试撤销功能..."
    echo "   执行撤销操作..."
    "$LOGGERFS_DIR/logctl" $TEST_FILE revert
    ls -la $TEST_FILE
    echo "   撤销后的文件大小"
    
    echo "   撤销后的日志:"
    "$LOGGERFS_DIR/logctl" $TEST_FILE readlog
fi

echo ""
echo "=== 追加测试完成 ==="
echo "测试结果："
echo "- 文件追加操作应该正确更新文件大小"
echo "- 中间写入应该正确处理文件空洞"
echo "- 覆盖写入不应该改变文件大小"
echo "- 截断操作应该正确调整文件和日志大小"
echo "- 所有操作都应该记录在日志中"
echo ""
echo "检查内核消息以获取详细信息:"
echo "dmesg | tail -20"
