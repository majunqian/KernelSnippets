#!/bin/bash

# LoggerFS Revert 功能演示脚本
# 演示如何使用revert功能撤销写操作

set -e

MOUNT_POINT="/mnt/loggerfs"
TEST_FILE="$MOUNT_POINT/revert_demo.txt"

echo "=== LoggerFS Revert 功能演示 ==="

# 检查是否以root权限运行
if [[ $EUID -ne 0 ]]; then
   echo "此脚本需要root权限运行"
   exit 1
fi

# 获取脚本所在目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOGGERFS_DIR="$(dirname "$SCRIPT_DIR")"

# 检查LoggerFS是否已加载和挂载
if ! lsmod | grep -q "loggerfs"; then
    echo "错误: LoggerFS内核模块未加载"
    echo "请先运行: sudo ./test_loggerfs.sh"
    exit 1
fi

if ! mountpoint -q $MOUNT_POINT 2>/dev/null; then
    echo "错误: LoggerFS未挂载"
    echo "请先运行: sudo ./test_loggerfs.sh"
    exit 1
fi

echo "步骤1: 创建演示文件..."
echo "Initial content" > $TEST_FILE
echo "文件初始内容:"
cat $TEST_FILE
echo "文件初始大小:"
ls -la $TEST_FILE

echo ""
echo "步骤2: 执行写操作（在文件末尾追加）..."
echo "Additional data" >> $TEST_FILE
echo "写入后内容:"
cat $TEST_FILE
echo "写入后大小:"
ls -la $TEST_FILE

echo ""
echo "步骤3: 查看操作日志..."
if [ -f "$LOGGERFS_DIR/logctl" ]; then
    "$LOGGERFS_DIR/logctl" $TEST_FILE readlog
else
    echo "警告: logctl工具未找到"
fi

echo ""
echo "步骤4: 撤销写操作..."
if [ -f "$LOGGERFS_DIR/logctl" ]; then
    "$LOGGERFS_DIR/logctl" $TEST_FILE revert
    echo "撤销后内容:"
    cat $TEST_FILE
    echo "撤销后大小:"
    ls -la $TEST_FILE
    echo "撤销后日志:"
    "$LOGGERFS_DIR/logctl" $TEST_FILE readlog
else
    echo "错误: logctl工具未找到，无法执行撤销操作"
    exit 1
fi

echo ""
echo "步骤5: 尝试再次撤销（应该失败或无效果）..."
echo "执行第二次revert..."
if "$LOGGERFS_DIR/logctl" $TEST_FILE revert; then
    echo "第二次撤销执行完成（可能无操作可撤销）"
else
    echo "第二次撤销失败（符合预期，因为只支持一次回退）"
fi
echo "最终内容:"
cat $TEST_FILE
echo "最终大小:"
ls -la $TEST_FILE
echo "最终日志:"
"$LOGGERFS_DIR/logctl" $TEST_FILE readlog

echo ""
echo "=== 演示完成 ==="
echo "总结:"
echo "- 文件进行一次写操作后，通过revert操作成功回退到初始状态"
echo "- 当前LoggerFS实现只支持回退一次操作"
echo "- 第二次revert尝试演示了当没有可回退操作时的行为"

echo ""
echo "清理演示文件..."
rm -f $TEST_FILE
echo "演示文件已清理"
