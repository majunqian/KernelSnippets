#!/bin/bash

# LoggerFS 功能测试脚本
# 用于验证带日志的文件系统功能

set -e

MOUNT_POINT="/mnt/loggerfs"
TEST_FILE="$MOUNT_POINT/testfile"

# 显示用法
show_usage() {
    echo "用法: $0 [选项]"
    echo "选项:"
    echo "  test      运行完整测试（默认）"
    echo "  cleanup   仅执行清理"
    echo "  help      显示此帮助信息"
}

# 处理命令行参数
case "${1:-test}" in
    cleanup)
        echo "=== LoggerFS 清理模式 ==="
        # 获取脚本所在目录
        SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
        if [ -f "$SCRIPT_DIR/cleanup_loggerfs.sh" ]; then
            exec "$SCRIPT_DIR/cleanup_loggerfs.sh"
        else
            echo "清理脚本未找到，执行简单清理..."
            umount $MOUNT_POINT 2>/dev/null || true
            rmmod loggerfs 2>/dev/null || true
            rmdir $MOUNT_POINT 2>/dev/null || true
            echo "清理完成。"
        fi
        exit 0
        ;;
    help)
        show_usage
        exit 0
        ;;
    test)
        # 继续执行测试
        ;;
    *)
        echo "未知选项: $1"
        show_usage
        exit 1
        ;;
esac

echo "=== LoggerFS 功能测试 ==="

# 检查是否以root权限运行
if [[ $EUID -ne 0 ]]; then
   echo "此脚本需要root权限运行"
   exit 1
fi

# 清理函数
cleanup() {
    echo "正在清理环境..."
    
    # 1. 卸载文件系统
    if mountpoint -q $MOUNT_POINT 2>/dev/null; then
        echo "   卸载文件系统..."
        umount $MOUNT_POINT 2>/dev/null || {
            echo "   强制卸载文件系统..."
            umount -f $MOUNT_POINT 2>/dev/null || true
        }
    fi
    
    # 2. 卸载内核模块
    if lsmod | grep -q "loggerfs"; then
        echo "   卸载内核模块..."
        rmmod loggerfs 2>/dev/null || {
            echo "   强制卸载内核模块..."
            rmmod -f loggerfs 2>/dev/null || true
        }
    fi
    
    # 3. 清理挂载点
    if [ -d $MOUNT_POINT ]; then
        echo "   清理挂载点..."
        rmdir $MOUNT_POINT 2>/dev/null || true
    fi
    
    # 4. 清理可能存在的测试文件
    if [ -f "$LOGGERFS_DIR/logctl" ]; then
        echo "   清理用户空间工具..."
    fi
    
    echo "清理完成。"
}

# 手动清理函数（用于测试失败时的紧急清理）
force_cleanup() {
    echo "执行强制清理..."
    
    # 强制卸载所有可能的挂载点
    umount -f $MOUNT_POINT 2>/dev/null || true
    umount -f /mnt/loggerfs 2>/dev/null || true
    
    # 强制卸载模块
    rmmod -f loggerfs 2>/dev/null || true
    
    # 清理目录
    rmdir $MOUNT_POINT 2>/dev/null || true
    rmdir /mnt/loggerfs 2>/dev/null || true
    
    echo "强制清理完成。"
}

# 设置清理陷阱（捕获多种退出信号）
trap cleanup EXIT
trap force_cleanup INT TERM

# 获取脚本所在目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOGGERFS_DIR="$(dirname "$SCRIPT_DIR")"

echo "0. 预清理环境..."
# 确保开始前环境是干净的
umount $MOUNT_POINT 2>/dev/null || true
rmmod loggerfs 2>/dev/null || true
rmdir $MOUNT_POINT 2>/dev/null || true

echo "1. 编译内核模块..."
cd "$LOGGERFS_DIR"
make clean && make

echo "2. 加载内核模块..."
insmod "$LOGGERFS_DIR/loggerfs.ko"

echo "3. 创建挂载点并挂载文件系统..."
mkdir -p $MOUNT_POINT
mount -t loggerfs none $MOUNT_POINT

echo "4. 创建测试文件..."
touch $TEST_FILE

echo "5. 执行测试操作..."

echo "   5.1 在偏移0处写入20字节..."
dd if=/dev/zero of=$TEST_FILE bs=20 count=1 seek=0 2>/dev/null

echo "   5.2 在偏移90处写入20字节..."
dd if=/dev/zero of=$TEST_FILE bs=1 count=20 seek=90 2>/dev/null

echo "   5.3 在偏移100处写入20字节..."
dd if=/dev/zero of=$TEST_FILE bs=1 count=20 seek=100 2>/dev/null

echo "   5.4 在文件末尾追加20字节..."
dd if=/dev/zero of=$TEST_FILE bs=20 count=1 oflag=append 2>/dev/null

echo "   5.5 在偏移2MB处写入20字节..."
dd if=/dev/zero of=$TEST_FILE bs=1 count=20 seek=2097152 2>/dev/null

echo "   5.6 从偏移0读取20字节..."
dd if=$TEST_FILE of=/dev/null bs=20 count=1 skip=0 2>/dev/null

echo "   5.7 从偏移90读取20字节..."
dd if=$TEST_FILE of=/dev/null bs=1 count=20 skip=90 2>/dev/null

echo "6. 检查文件大小（应该只显示数据部分，不包含日志）..."
ls -la $TEST_FILE

echo "7. 读取文件操作日志..."
if [ -f "$LOGGERFS_DIR/logctl" ]; then
    echo "   使用logctl读取操作日志:"
    "$LOGGERFS_DIR/logctl" $TEST_FILE readlog
    
    echo ""
    echo "8. 测试撤销功能..."
    echo "   当前文件大小（撤销前）:"
    ls -la $TEST_FILE
    
    echo "   当前日志内容（撤销前）:"
    "$LOGGERFS_DIR/logctl" $TEST_FILE readlog | tail -3
    
    echo ""
    echo "   执行撤销最后一次写操作..."
    "$LOGGERFS_DIR/logctl" $TEST_FILE revert
    
    echo "   撤销后文件大小:"
    ls -la $TEST_FILE
    
    echo "   撤销后日志内容:"
    "$LOGGERFS_DIR/logctl" $TEST_FILE readlog | tail -3
    
    echo ""
    echo "   再次测试撤销功能..."
    echo "   再次撤销前文件大小:"
    ls -la $TEST_FILE
    
    echo "   执行第二次撤销..."
    "$LOGGERFS_DIR/logctl" $TEST_FILE revert
    
    echo "   第二次撤销后文件大小:"
    ls -la $TEST_FILE
    
    echo "   最终日志内容:"
    "$LOGGERFS_DIR/logctl" $TEST_FILE readlog
    
else
    echo "   警告: logctl工具未找到，无法读取日志"
    echo "   注意：日志读取需要使用fcntl的READLOG命令，这需要专门的C程序"
fi

echo "9. 测试完成！"
echo "   - 文件大小应该反映实际数据大小（不包含日志）"
echo "   - 所有读写操作都应该记录在日志中"
echo "   - 检查dmesg查看内核消息"

echo "查看内核消息:"
dmesg | tail -10

echo ""
echo "=== 测试总结 ==="
echo "测试已完成，系统将自动清理。"
echo ""
echo "如果需要单独的清理工具，可以使用："
echo "  sudo ./cleanup_loggerfs.sh          # 详细清理"
echo "  sudo ./cleanup_loggerfs.sh --quick  # 快速清理"
echo "  sudo ./cleanup_loggerfs.sh --status # 检查状态"
echo ""
echo "手动清理命令："
echo "  sudo umount $MOUNT_POINT"
echo "  sudo rmmod loggerfs"
echo "  sudo rmdir $MOUNT_POINT"
echo ""
echo "强制清理命令："
echo "  sudo umount -f $MOUNT_POINT"
echo "  sudo rmmod -f loggerfs"
echo "  sudo rmdir $MOUNT_POINT"
