#!/bin/bash

# LoggerFS 备份和恢复功能测试脚本

echo "=== LoggerFS 备份和恢复功能测试 ==="

# 检查logctl工具是否存在
if [ ! -f "./logctl" ]; then
    echo "错误: logctl 工具不存在，请先编译"
    echo "运行: make logctl"
    exit 1
fi

# 检查是否以 root 权限运行
if [ "$EUID" -ne 0 ]; then
    echo "错误: 请以 root 权限运行此脚本"
    exit 1
fi

MOUNT_POINT="/mnt/loggerfs_test"
TEST_FILE="$MOUNT_POINT/backup_test.txt"

# 清理函数
cleanup() {
    echo "清理测试环境..."
    umount $MOUNT_POINT 2>/dev/null
    rmmod loggerfs 2>/dev/null
    rmdir $MOUNT_POINT 2>/dev/null
}

# 设置退出时清理
trap cleanup EXIT

echo "1. 编译并加载 LoggerFS 模块..."
cd "$(dirname "$0")/.."
make clean && make

if [ $? -ne 0 ]; then
    echo "错误: 编译失败"
    exit 1
fi

insmod loggerfs.ko
if [ $? -ne 0 ]; then
    echo "错误: 模块加载失败"
    exit 1
fi

echo "2. 创建挂载点并挂载文件系统..."
mkdir -p $MOUNT_POINT
mount -t loggerfs none $MOUNT_POINT

if [ $? -ne 0 ]; then
    echo "错误: 挂载失败"
    exit 1
fi

echo "3. 测试基本写操作和备份功能..."

# 创建初始文件内容
echo "Hello, World!" > $TEST_FILE
echo "初始文件内容:"
cat $TEST_FILE
echo ""

# 查看日志
echo "4. 查看初始写操作日志..."
./logctl "$TEST_FILE" readlog
echo ""

# 进行第二次写操作（这将覆盖第一次写操作的备份）
echo "This is a new line." >> $TEST_FILE
echo "添加内容后的文件:"
cat $TEST_FILE
echo ""

# 查看更新后的日志
echo "5. 查看更新后的日志..."
./logctl "$TEST_FILE" readlog
echo ""

# 测试回退功能
echo "6. 测试回退功能（恢复到上一次写操作前的状态）..."
echo "文件当前大小: $(stat -c%s "$TEST_FILE")"
echo "执行回退命令: ./logctl \"$TEST_FILE\" revert"
./logctl "$TEST_FILE" revert
REVERT_RESULT=$?

echo "回退命令返回码: $REVERT_RESULT"
if [ $REVERT_RESULT -eq 0 ]; then
    echo "回退命令执行成功"
else
    echo "回退命令执行失败，返回码: $REVERT_RESULT"
    echo "可能的原因："
    echo "  1. logctl工具没有正确实现revert功能"
    echo "  2. 文件系统没有记录备份信息"
    echo "  3. 文件路径或权限问题"
fi

echo "文件回退后大小: $(stat -c%s "$TEST_FILE")"
echo "回退后的文件内容:"
cat "$TEST_FILE"
echo ""

# 查看回退后的日志
echo "7. 查看回退后的日志..."
./logctl "$TEST_FILE" readlog
echo ""

# 测试中间写操作的情况
echo "8. 测试中间位置写操作的备份和恢复..."

# 重新创建一个较长的文件
echo "Line 1: This is the first line" > $TEST_FILE
echo "Line 2: This is the second line" >> $TEST_FILE
echo "Line 3: This is the third line" >> $TEST_FILE

echo "原始文件内容:"
cat $TEST_FILE
echo ""

# 使用 dd 在文件中间写入数据
echo "在文件中间写入数据..."
echo "MODIFIED" | dd of=$TEST_FILE bs=1 seek=20 conv=notrunc 2>/dev/null

echo "修改后的文件内容:"
cat $TEST_FILE
echo ""

# 尝试回退中间写操作
echo "9. 尝试回退中间写操作..."
echo "执行回退命令: ./logctl \"$TEST_FILE\" revert"
./logctl "$TEST_FILE" revert
REVERT_RESULT2=$?

if [ $REVERT_RESULT2 -eq 0 ]; then
    echo "回退命令执行成功"
else
    echo "回退命令执行失败，返回码: $REVERT_RESULT2"
fi

echo "回退后的文件内容:"
cat "$TEST_FILE"
echo ""

# 查看内核日志中的相关信息
echo "10. 检查内核日志中的 LoggerFS 相关信息..."
dmesg | grep -i loggerfs | tail -20

echo ""
echo "=== 测试完成 ==="
echo "注意: 查看内核日志以了解备份和恢复操作的详细信息"
