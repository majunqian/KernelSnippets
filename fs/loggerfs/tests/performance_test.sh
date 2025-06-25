#!/bin/bash

# LoggerFS 性能测试脚本

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
MOUNT_POINT="/mnt/loggerfs_perf"
TEST_FILE="$MOUNT_POINT/perf_test_file"

# 性能测试参数
SMALL_FILE_SIZE="1K"
MEDIUM_FILE_SIZE="1M"
LARGE_FILE_SIZE="10M"
BLOCK_SIZES=("1" "4K" "64K" "1M")

# 颜色定义
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m'

# 设置函数
setup() {
    echo "=== LoggerFS 性能测试 ==="
    echo "设置测试环境..."
    
    if [[ $EUID -ne 0 ]]; then
        echo "错误：需要root权限运行性能测试"
        exit 1
    fi
    
    cd "$PROJECT_DIR"
    make clean >/dev/null 2>&1
    if ! make >/dev/null 2>&1; then
        echo "错误：编译失败"
        exit 1
    fi
    
    if ! insmod loggerfs.ko 2>/dev/null; then
        echo "错误：加载内核模块失败"
        exit 1
    fi
    
    mkdir -p "$MOUNT_POINT"
    if ! mount -t loggerfs none "$MOUNT_POINT" 2>/dev/null; then
        echo "错误：挂载文件系统失败"
        rmmod loggerfs 2>/dev/null
        exit 1
    fi
    
    echo "测试环境设置完成"
    echo
}

cleanup() {
    echo "清理测试环境..."
    umount "$MOUNT_POINT" 2>/dev/null || true
    rmmod loggerfs 2>/dev/null || true
    rmdir "$MOUNT_POINT" 2>/dev/null || true
    cd "$PROJECT_DIR"
}

# 性能测试函数
benchmark_write() {
    local file_size="$1"
    local block_size="$2"
    local test_file="$3"
    
    echo -e "${BLUE}写入测试: 文件大小=$file_size, 块大小=$block_size${NC}"
    
    # 清理文件
    rm -f "$test_file"
    
    # 执行写入测试
    local start_time=$(date +%s.%N)
    dd if=/dev/zero of="$test_file" bs="$block_size" count=$(($(numfmt --from=iec "$file_size") / $(numfmt --from=iec "$block_size"))) 2>/dev/null
    local end_time=$(date +%s.%N)
    
    local duration=$(echo "$end_time - $start_time" | bc)
    local throughput=$(echo "scale=2; $(numfmt --from=iec "$file_size") / $duration / 1024 / 1024" | bc)
    
    echo "  时间: ${duration}s, 吞吐量: ${throughput} MB/s"
    
    # 获取实际文件大小
    local actual_size=$(stat -c%s "$test_file" 2>/dev/null || echo "0")
    echo "  实际文件大小: $actual_size 字节"
    echo
}

benchmark_read() {
    local file_size="$1"
    local block_size="$2"
    local test_file="$3"
    
    echo -e "${BLUE}读取测试: 文件大小=$file_size, 块大小=$block_size${NC}"
    
    # 确保文件存在
    if [ ! -f "$test_file" ]; then
        dd if=/dev/zero of="$test_file" bs="$file_size" count=1 2>/dev/null
    fi
    
    # 执行读取测试
    local start_time=$(date +%s.%N)
    dd if="$test_file" of=/dev/null bs="$block_size" 2>/dev/null
    local end_time=$(date +%s.%N)
    
    local duration=$(echo "$end_time - $start_time" | bc)
    local file_size_bytes=$(stat -c%s "$test_file")
    local throughput=$(echo "scale=2; $file_size_bytes / $duration / 1024 / 1024" | bc)
    
    echo "  时间: ${duration}s, 吞吐量: ${throughput} MB/s"
    echo
}

benchmark_random_access() {
    local test_file="$1"
    local num_operations=100
    
    echo -e "${BLUE}随机访问测试: $num_operations 次操作${NC}"
    
    # 创建测试文件
    dd if=/dev/zero of="$test_file" bs=1M count=10 2>/dev/null
    
    local start_time=$(date +%s.%N)
    
    for i in $(seq 1 $num_operations); do
        local offset=$((RANDOM % (10 * 1024 * 1024)))
        dd if=/dev/zero of="$test_file" bs=1 count=1 seek=$offset conv=notrunc 2>/dev/null
    done
    
    local end_time=$(date +%s.%N)
    local duration=$(echo "$end_time - $start_time" | bc)
    local ops_per_sec=$(echo "scale=2; $num_operations / $duration" | bc)
    
    echo "  时间: ${duration}s, 操作/秒: ${ops_per_sec}"
    echo
}

benchmark_log_operations() {
    local test_file="$1"
    local num_operations=50
    
    echo -e "${BLUE}日志操作测试: $num_operations 次操作${NC}"
    
    # 清理文件
    rm -f "$test_file"
    
    local start_time=$(date +%s.%N)
    
    for i in $(seq 1 $num_operations); do
        echo "test data $i" >> "$test_file"
        cd "$PROJECT_DIR"
        ./logctl "$test_file" readlog >/dev/null 2>&1
    done
    
    local end_time=$(date +%s.%N)
    local duration=$(echo "$end_time - $start_time" | bc)
    local ops_per_sec=$(echo "scale=2; $num_operations / $duration" | bc)
    
    echo "  时间: ${duration}s, 日志操作/秒: ${ops_per_sec}"
    echo
}

# 主测试流程
main() {
    setup
    trap cleanup EXIT
    
    echo -e "${YELLOW}开始性能测试...${NC}"
    echo
    
    # 测试不同文件大小和块大小的写入性能
    echo -e "${GREEN}=== 写入性能测试 ===${NC}"
    for size in "$SMALL_FILE_SIZE" "$MEDIUM_FILE_SIZE" "$LARGE_FILE_SIZE"; do
        for block_size in "${BLOCK_SIZES[@]}"; do
            benchmark_write "$size" "$block_size" "${TEST_FILE}_write"
        done
    done
    
    # 测试不同文件大小和块大小的读取性能
    echo -e "${GREEN}=== 读取性能测试 ===${NC}"
    for size in "$SMALL_FILE_SIZE" "$MEDIUM_FILE_SIZE" "$LARGE_FILE_SIZE"; do
        for block_size in "${BLOCK_SIZES[@]}"; do
            benchmark_read "$size" "$block_size" "${TEST_FILE}_read"
        done
    done
    
    # 随机访问测试
    echo -e "${GREEN}=== 随机访问性能测试 ===${NC}"
    benchmark_random_access "${TEST_FILE}_random"
    
    # 日志操作性能测试
    echo -e "${GREEN}=== 日志操作性能测试 ===${NC}"
    benchmark_log_operations "${TEST_FILE}_log"
    
    echo -e "${GREEN}性能测试完成！${NC}"
}

# 检查依赖
check_dependencies() {
    if ! command -v bc >/dev/null 2>&1; then
        echo "错误：需要安装 bc 计算器"
        echo "Ubuntu/Debian: sudo apt-get install bc"
        echo "CentOS/RHEL: sudo yum install bc"
        exit 1
    fi
    
    if ! command -v numfmt >/dev/null 2>&1; then
        echo "错误：需要 numfmt 命令（通常在 coreutils 包中）"
        exit 1
    fi
}

check_dependencies
main "$@"
