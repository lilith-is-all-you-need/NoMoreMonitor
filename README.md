# 👁️ NoMoreMonitor

**NoMoreMonitor** 是一款旨在保护用户本地隐私的底层防御工具。它通过 DLL 注入技术，深入系统底层拦截“希沃（Seewo）”相关进程对摄像头的调用请求。当检测到摄像头被意外唤醒时，工具会强制挂起调用，给予用户 5 秒的缓冲与提醒时间，让你对硬件状态了如指掌。

## ✨ 核心特性 (Features)

* **底层拦截**：基于 MinHook 框架，直接 Hook 目标进程的 `media_framework_device.dll` 核心函数。
* **强制延时缓冲**：当检测到摄像头捕获启动（Capture Start）时，程序会自动强制挂起 5 秒，防止被瞬间偷拍。
* **全面覆盖**：同时拦截 DirectShow (DS) 和 Media Foundation (MF) 两种主流媒体框架的调用请求。
* **高效 IPC 通信**：底层 DLL 与前端 UI 之间采用**共享内存 + 互斥锁 + 事件通知**的机制进行通信，低延迟且无性能损耗。
* **纯粹安全**：纯本地运行，无网络请求，不收集任何用户数据。

## 🛠️ 工作原理 (How it works)

本项目的核心是一个使用 C 编写的动态链接库 (DLL)。当该 DLL 被注入到目标进程后，会执行以下操作：

1. **环境检测**：轮询等待并定位目标模块 `media_framework_device.dll` 加载。
2. **API Hooking**：利用 `MinHook` 库，通过导出表序号（Ordinals）精准替换目标模块的关键函数：
   * `ord:76` / `ord:86` - DirectShow (DS) 摄像头启动/停止捕获。
   * `ord:77` / `ord:87` - Media Foundation (MF) 摄像头启动/停止捕获。
3. **状态接管与延时**：当目标软件尝试开启摄像头时，Hook 函数会优先拦截该请求，通过共享内存 `Local\LilithSharedMem` 发送警告事件给宿主程序，并强制执行 5 秒倒计时延时（智能区分 GUI 线程以防卡死），随后才放行原本的捕获流程。
4. **状态恢复**：拦截到停止捕获请求时，向宿主程序发送解除警报信号。

## 🚀 快速开始 (Getting Started)

### 环境依赖
* Windows 操作系统
* [MinHook](https://github.com/TsudaKageyu/minhook) 库（已包含 `libMinHook.x86.lib`）
* C/C++ 编译器 (推荐 Visual Studio)

### 编译与运行
1.  将本项目克隆到本地：
    ```bash
    git clone [https://github.com/你的用户名/NoMoreMonitor.git](https://github.com/你的用户名/NoMoreMonitor.git)
    ```
2.  使用 Visual Studio 打开项目，编译生成 `NoMoreMonitor.dll`。
3.  搭配配套的 DLL 注入器与宿主 UI 程序（Host Process），将 DLL 注入到目标进程即可开始监控。

## ⚠️ 免责声明 (Disclaimer)

本项目仅供**学习操作系统底层机制、API Hook 技术交流与个人隐私保护测试**使用。
1. 本程序通过内存 Hook 技术实现，**不修改**目标软件的任何磁盘文件。
2. 请遵守所在机构的相关规章制度，请勿将本工具用于对抗正常的考试监考或合法合规的管理行为。
3. 开发者对因使用本工具引起的任何纠纷或后果不承担任何责任。

## 🤝 参与贡献 (Contributing)
如果你有更好的建议或发现了 Bug，欢迎提交 Issue 或 Pull Request！

## 📄 开源协议 (License)
本项目采用 [MIT License](LICENSE) 开源协议。