#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <string.h>
#include <errno.h>

#define READLOG_CMD 0x1000
#define REVERT_CMD 0x2000
#define MAX_LOG_SIZE 4096

void print_usage(char *prog_name) {
    printf("用法: %s <file_path> <command>\n", prog_name);
    printf("命令:\n");
    printf("  readlog  - 读取文件的日志\n");
    printf("  revert   - 撤销最后一次写操作\n");
}

int read_log(const char *file_path) {
    int fd;
    char log_buffer[MAX_LOG_SIZE];
    int log_size;
    
    fd = open(file_path, O_RDONLY);
    if (fd < 0) {
        perror("打开文件失败");
        return -1;
    }
    
    log_size = ioctl(fd, READLOG_CMD, log_buffer);
    if (log_size < 0) {
        perror("读取日志失败");
        close(fd);
        return -1;
    }
    
    printf("=== 文件日志内容 ===\n");
    if (log_size == 0) {
        printf("(无日志记录)\n");
    } else {
        printf("日志大小: %d 字节\n", log_size);
        printf("日志内容:\n");
        printf("时间戳       命令路径                 操作类型  偏移     长度\n");
        printf("--------------------------------------------------------\n");
        
        // 确保字符串以null结尾
        if (log_size < MAX_LOG_SIZE) {
            log_buffer[log_size] = '\0';
        } else {
            log_buffer[MAX_LOG_SIZE - 1] = '\0';
        }
        
        printf("%s", log_buffer);
    }
    
    close(fd);
    return 0;
}

int revert_last_write(const char *file_path) {
    int fd;
    int result;
    
    fd = open(file_path, O_WRONLY);
    if (fd < 0) {
        perror("打开文件失败");
        return -1;
    }
    
    result = ioctl(fd, REVERT_CMD, 0);
    if (result < 0) {
        switch (errno) {
            case ENODATA:
                printf("错误: 没有可用的日志数据或备份数据进行回退\n");
                break;
            case ENOENT:
                printf("错误: 日志中没有找到写操作记录\n");
                break;
            default:
                perror("撤销操作失败");
                break;
        }
        close(fd);
        return -1;
    }
    
    printf("成功撤销最后一次写操作\n");
    close(fd);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        print_usage(argv[0]);
        return 1;
    }
    
    char *file_path = argv[1];
    char *command = argv[2];
    
    if (strcmp(command, "readlog") == 0) {
        return read_log(file_path);
    } else if (strcmp(command, "revert") == 0) {
        return revert_last_write(file_path);
    } else {
        printf("未知命令: %s\n", command);
        print_usage(argv[0]);
        return 1;
    }
}
