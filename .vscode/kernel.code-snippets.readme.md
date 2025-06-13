# kernel.code-snippets 说明

本文件为 VSCode 工作区级别的代码片段（snippets）集合，主要用于 Linux 内核模块开发。包含以下片段：

## 片段列表

- **Linux Kernel Module Init**
  - 用于快速生成标准的 Linux 内核模块初始化模板，包含参数定义、pr_fmt、SPDX、作者、描述等。
- **Linux Kernel Module with /proc entry**
  - 用于生成带有 /proc 文件接口的内核模块模板，支持读写、锁保护、现代 proc_ops。
- **Linux Kernel Module with Simple Kprobe**
  - 用于生成简单 kprobe（函数入口探测）内核模块模板，便于监控函数调用。
- **Linux Kernel Module with Kretprobe**
  - 用于生成带有 kretprobe（函数返回探测，含时延分析）的内核模块模板。
- **Linux Kernel Module Makefile**
  - 用于生成功能完善的内核模块 Makefile，支持 checkpatch、编译、加载、卸载、状态、日志、帮助等操作。

## 使用方法

在 VSCode 编辑 C 或 Makefile 文件时，输入对应前缀（如 `kmodinit`、`kmodproc`、`kmodsimplekprobe`、`kmodkretprobe`、`kmodmake`）即可快速插入模板代码。

每个片段均遵循内核代码规范，便于快速开发和调试内核模块。
