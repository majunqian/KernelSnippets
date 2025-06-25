#!/bin/bash

# LoggerFS 单元测试脚本

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
MOUNT_POINT="/mnt/loggerfs_test"
TEST_FILE="$MOUNT_POINT/unittest_file"

# 测试结果统计
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0

# 颜色定义
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 测试函数
run_test() {
    local test_name="$1"
    local test_command="$2"
    
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    echo -e "${YELLOW}测试 $TOTAL_TESTS: $test_name${NC}"
    
    if eval "$test_command"; then
        echo -e "${GREEN}✓ 通过${NC}"
        PASSED_TESTS=$((PASSED_TESTS + 1))
    else
        echo -e "${RED}✗ 失败${NC}"
        FAILED_TESTS=$((FAILED_TESTS + 1))
    fi
    echo
}

# 设置函数
setup() {
    echo "=== LoggerFS 单元测试 ==="
    echo "设置测试环境..."
    
    # 检查root权限
    if [[ $EUID -ne 0 ]]; then
        echo "错误：需要root权限运行测试"
        exit 1
    fi
    
    # 编译项目
    cd "$PROJECT_DIR"
    make clean >/dev/null 2>&1
    if ! make >/dev/null 2>&1; then
        echo "错误：编译失败"
        exit 1
    fi
    
    # 加载模块
    if ! insmod loggerfs.ko 2>/dev/null; then
        echo "错误：加载内核模块失败"
        exit 1
    fi
    
    # 创建挂载点
    mkdir -p "$MOUNT_POINT"
    if ! mount -t loggerfs none "$MOUNT_POINT" 2>/dev/null; then
        echo "错误：挂载文件系统失败"
        rmmod loggerfs 2>/dev/null
        exit 1
    fi
    
    echo "测试环境设置完成"
    echo
}

# 清理函数
cleanup() {
    echo "清理测试环境..."
    umount "$MOUNT_POINT" 2>/dev/null || true
    rmmod loggerfs 2>/dev/null || true
    rmdir "$MOUNT_POINT" 2>/dev/null || true
    cd "$PROJECT_DIR"
}

# 测试用例

test_file_creation() {
    touch "$TEST_FILE" && [ -f "$TEST_FILE" ]
}

test_basic_write() {
    echo "test data" > "$TEST_FILE" && [ -s "$TEST_FILE" ]
}

test_basic_read() {
    echo "test data" > "$TEST_FILE"
    local content=$(cat "$TEST_FILE")
    [ "$content" = "test data" ]
}

test_file_size_stat() {
    echo "1234567890" > "$TEST_FILE"
    local size=$(stat -c%s "$TEST_FILE")
    [ "$size" = "11" ]  # 10 字符 + 换行符
}

test_dd_write_at_offset() {
    # 创建100字节的文件
    dd if=/dev/zero of="$TEST_FILE" bs=100 count=1 2>/dev/null
    # 在偏移50处写入20字节
    dd if=/dev/zero of="$TEST_FILE" bs=1 count=20 seek=50 conv=notrunc 2>/dev/null
    # 检查文件大小
    local size=$(stat -c%s "$TEST_FILE")
    [ "$size" = "100" ]
}

test_dd_read_from_offset() {
    # 写入测试数据
    dd if=/dev/zero of="$TEST_FILE" bs=100 count=1 2>/dev/null
    # 从偏移10读取20字节
    dd if="$TEST_FILE" of=/dev/null bs=1 count=20 skip=10 2>/dev/null
    # 如果没有错误，测试通过
    [ $? -eq 0 ]
}

test_log_functionality() {
    # 写入一些数据
    echo "test log" > "$TEST_FILE"
    # 尝试读取日志
    cd "$PROJECT_DIR"
    timeout 5s ./logctl "$TEST_FILE" readlog >/dev/null 2>&1
    # 如果命令不超时且无错误，测试通过
    [ $? -eq 0 ]
}

test_revert_functionality() {
    # 写入初始数据
    echo "initial data" > "$TEST_FILE"
    # 写入更多数据
    echo "additional data" >> "$TEST_FILE"
    # 尝试撤销
    cd "$PROJECT_DIR"
    timeout 5s ./logctl "$TEST_FILE" revert >/dev/null 2>&1
    # 如果命令不超时且无错误，测试通过
    [ $? -eq 0 ]
}

test_large_file_operations() {
    # 测试大文件操作（1MB）
    dd if=/dev/zero of="$TEST_FILE" bs=1024 count=1024 2>/dev/null
    local size=$(stat -c%s "$TEST_FILE")
    [ "$size" = "1048576" ]  # 1MB
}

test_multiple_files() {
    # 测试多个文件
    local file1="$MOUNT_POINT/test1"
    local file2="$MOUNT_POINT/test2"
    
    echo "file1 content" > "$file1"
    echo "file2 content" > "$file2"
    
    [ -f "$file1" ] && [ -f "$file2" ] && \
    [ "$(cat "$file1")" = "file1 content" ] && \
    [ "$(cat "$file2")" = "file2 content" ]
}

# 主测试流程
main() {
    setup
    trap cleanup EXIT
    
    # 运行所有测试
    run_test "文件创建" "test_file_creation"
    run_test "基本写入" "test_basic_write"
    run_test "基本读取" "test_basic_read"
    run_test "文件大小stat" "test_file_size_stat"
    run_test "dd偏移写入" "test_dd_write_at_offset"
    run_test "dd偏移读取" "test_dd_read_from_offset"
    run_test "日志功能" "test_log_functionality"
    run_test "撤销功能" "test_revert_functionality"
    run_test "大文件操作" "test_large_file_operations"
    run_test "多文件操作" "test_multiple_files"
    
    # 显示测试结果
    echo "=== 测试结果 ==="
    echo -e "总测试数: $TOTAL_TESTS"
    echo -e "${GREEN}通过: $PASSED_TESTS${NC}"
    echo -e "${RED}失败: $FAILED_TESTS${NC}"
    
    if [ $FAILED_TESTS -eq 0 ]; then
        echo -e "${GREEN}所有测试通过！${NC}"
        exit 0
    else
        echo -e "${RED}有测试失败${NC}"
        exit 1
    fi
}

main "$@"
