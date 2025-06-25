# LoggerFS Makefile

# 内核模块对象文件
obj-m += loggerfs.o
loggerfs-objs := src/loggerfs_core.o src/loggerfs_file.o src/loggerfs_inode.o src/loggerfs_super.o

# 内核构建目录
KDIR := /lib/modules/$(shell uname -r)/build

# 编译标志
ccflags-y := -I$(PWD)/include

# 默认目标
all: module userspace

# 编译内核模块
module:
	make -C $(KDIR) M=$(PWD) modules

# 编译用户空间工具
userspace:
	gcc -o logctl src/logctl.c

# 清理编译文件
clean:
	make -C $(KDIR) M=$(PWD) clean
	rm -f logctl
	rm -f src/*.o

# 安装内核模块
install: module
	sudo insmod loggerfs.ko

# 卸载内核模块
uninstall:
	sudo rmmod loggerfs

# 挂载文件系统
mount:
	sudo mkdir -p /mnt/loggerfs
	sudo mount -t loggerfs none /mnt/loggerfs

# 卸载文件系统
umount:
	sudo umount /mnt/loggerfs

# 运行基本测试
test: install mount userspace
	@echo "=== LoggerFS 基本功能测试 ==="
	@echo "1. 创建测试文件..."
	sudo touch /mnt/loggerfs/testfile
	
	@echo "2. 在偏移0处写入20字节..."
	sudo dd if=/dev/zero of=/mnt/loggerfs/testfile bs=20 count=1 seek=0 2>/dev/null
	
	@echo "3. 在偏移90处写入20字节..."
	sudo dd if=/dev/zero of=/mnt/loggerfs/testfile bs=1 count=20 seek=90 2>/dev/null
	
	@echo "4. 在偏移100处写入20字节..."
	sudo dd if=/dev/zero of=/mnt/loggerfs/testfile bs=1 count=20 seek=100 2>/dev/null
	
	@echo "5. 在文件末尾写入20字节..."
	sudo dd if=/dev/zero of=/mnt/loggerfs/testfile bs=20 count=1 oflag=append 2>/dev/null
	
	@echo "6. 在偏移2MB处写入20字节..."
	sudo dd if=/dev/zero of=/mnt/loggerfs/testfile bs=1 count=20 seek=2097152 2>/dev/null
	
	@echo "7. 从偏移0读取20字节..."
	sudo dd if=/mnt/loggerfs/testfile of=/dev/null bs=20 count=1 skip=0 2>/dev/null
	
	@echo "8. 从偏移90读取20字节..."
	sudo dd if=/mnt/loggerfs/testfile of=/dev/null bs=1 count=20 skip=90 2>/dev/null
	
	@echo "9. 检查文件大小..."
	sudo ls -la /mnt/loggerfs/testfile
	
	@echo "10. 读取日志..."
	sudo ./logctl /mnt/loggerfs/testfile readlog
	
	@echo "11. 测试撤销操作..."
	sudo ./logctl /mnt/loggerfs/testfile revert
	
	@echo "12. 读取撤销后的日志..."
	sudo ./logctl /mnt/loggerfs/testfile readlog
	
	@echo "基本测试完成！"

# 运行单元测试
unittest: userspace
	chmod +x tests/unit_test.sh
	sudo tests/unit_test.sh

# 运行性能测试
perftest: userspace
	chmod +x tests/performance_test.sh
	sudo tests/performance_test.sh

# 运行完整测试（功能测试）
fulltest: userspace
	chmod +x tests/test_loggerfs.sh
	sudo tests/test_loggerfs.sh

# 帮助信息
help:
	@echo "LoggerFS Makefile 使用说明:"
	@echo ""
	@echo "编译目标:"
	@echo "  all         - 编译内核模块和用户空间工具"
	@echo "  module      - 只编译内核模块"
	@echo "  userspace   - 只编译用户空间工具"
	@echo "  clean       - 清理编译文件"
	@echo ""
	@echo "安装/卸载:"
	@echo "  install     - 加载内核模块"
	@echo "  uninstall   - 卸载内核模块"
	@echo "  mount       - 挂载文件系统到 /mnt/loggerfs"
	@echo "  umount      - 卸载文件系统"
	@echo ""
	@echo "测试:"
	@echo "  test        - 运行基本功能测试"
	@echo "  unittest    - 运行单元测试"
	@echo "  perftest    - 运行性能测试"
	@echo "  fulltest    - 运行完整功能测试"
	@echo ""
	@echo "其他:"
	@echo "  help        - 显示此帮助信息"

.PHONY: all module userspace clean install uninstall mount umount test unittest perftest fulltest help
