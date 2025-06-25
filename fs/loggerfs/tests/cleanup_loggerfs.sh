#!/bin/bash

# LoggerFS 清理脚本
# 用于清理LoggerFS测试环境

echo "=== LoggerFS 环境清理 ==="

# 检查是否以root权限运行
if [[ $EUID -ne 0 ]]; then
   echo "此脚本需要root权限运行"
   exit 1
fi

MOUNT_POINT="/mnt/loggerfs"

# 详细清理函数
detailed_cleanup() {
    echo "开始详细清理..."
    
    # 1. 检查并卸载文件系统
    echo "1. 检查挂载状态..."
    if mountpoint -q $MOUNT_POINT 2>/dev/null; then
        echo "   发现已挂载的LoggerFS，正在卸载..."
        umount $MOUNT_POINT 2>/dev/null && echo "   卸载成功" || {
            echo "   常规卸载失败，尝试强制卸载..."
            umount -f $MOUNT_POINT 2>/dev/null && echo "   强制卸载成功" || {
                echo "   强制卸载也失败，尝试延迟卸载..."
                umount -l $MOUNT_POINT 2>/dev/null && echo "   延迟卸载成功" || echo "   卸载失败，可能需要重启"
            }
        }
    else
        echo "   未发现已挂载的LoggerFS"
    fi
    
    # 2. 检查并卸载内核模块
    echo "2. 检查内核模块状态..."
    if lsmod | grep -q "loggerfs"; then
        echo "   发现LoggerFS内核模块，正在卸载..."
        rmmod loggerfs 2>/dev/null && echo "   卸载成功" || {
            echo "   常规卸载失败，尝试强制卸载..."
            rmmod -f loggerfs 2>/dev/null && echo "   强制卸载成功" || echo "   卸载失败，可能被占用"
        }
    else
        echo "   未发现LoggerFS内核模块"
    fi
    
    # 3. 清理挂载点
    echo "3. 清理挂载点..."
    if [ -d $MOUNT_POINT ]; then
        if rmdir $MOUNT_POINT 2>/dev/null; then
            echo "   挂载点清理成功"
        else
            echo "   挂载点可能非空，列出内容："
            ls -la $MOUNT_POINT 2>/dev/null || echo "   无法访问挂载点"
            echo "   如果确认可以删除，请手动运行: rm -rf $MOUNT_POINT"
        fi
    else
        echo "   挂载点不存在"
    fi
    
    # 4. 检查其他可能的挂载点
    echo "4. 检查其他可能的LoggerFS挂载..."
    mount | grep loggerfs && echo "   发现其他LoggerFS挂载，请手动处理" || echo "   未发现其他LoggerFS挂载"
    
    echo "清理完成！"
}

# 快速清理函数
quick_cleanup() {
    echo "执行快速清理..."
    umount -f $MOUNT_POINT 2>/dev/null || true
    rmmod -f loggerfs 2>/dev/null || true
    rmdir $MOUNT_POINT 2>/dev/null || true
    echo "快速清理完成！"
}

# 显示状态函数
show_status() {
    echo "=== 当前LoggerFS状态 ==="
    
    echo "挂载状态："
    mount | grep loggerfs && echo "发现LoggerFS挂载" || echo "未发现LoggerFS挂载"
    
    echo "内核模块状态："
    lsmod | grep loggerfs && echo "LoggerFS模块已加载" || echo "LoggerFS模块未加载"
    
    echo "挂载点状态："
    if [ -d $MOUNT_POINT ]; then
        echo "挂载点 $MOUNT_POINT 存在"
        ls -la $MOUNT_POINT 2>/dev/null || echo "无法访问挂载点"
    else
        echo "挂载点 $MOUNT_POINT 不存在"
    fi
}

# 显示用法
show_usage() {
    echo "用法: $0 [选项]"
    echo "选项:"
    echo "  -d, --detailed    详细清理（默认）"
    echo "  -q, --quick       快速清理"
    echo "  -s, --status      显示当前状态"
    echo "  -h, --help        显示此帮助信息"
}

# 主逻辑
case "${1:-detailed}" in
    -d|--detailed|detailed)
        detailed_cleanup
        ;;
    -q|--quick|quick)
        quick_cleanup
        ;;
    -s|--status|status)
        show_status
        ;;
    -h|--help|help)
        show_usage
        ;;
    *)
        echo "未知选项: $1"
        show_usage
        exit 1
        ;;
esac
