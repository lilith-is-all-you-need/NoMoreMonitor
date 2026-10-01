# 👁️ NoMoreMonitor

**NoMoreMonitor** 是一款旨在保护用户本地隐私的底层防御工具。它通过 DLL 注入技术，深入系统底层拦截“希沃（Seewo）”相关进程对摄像头的调用请求。当检测到摄像头被意外唤醒时，工具会强制挂起调用，给予用户可配置的缓冲与提醒时间，让你对硬件状态了如指掌。

项目由两部分组成：

* `NoMoreMonitor.exe` —— 宿主程序（控制台 + 全屏 OSD 覆盖层 + 托盘图标 + 配置面板 + 统计分析）。
* `NoMoreMonitor_Dll.dll` —— 被注入到 `media_capture.exe` 的 Hook 模块（MinHook + IPC）。

## ✨ 核心特性 (Features)

### 底层拦截
* **底层拦截**：基于 MinHook 框架，直接 Hook 目标进程的 `media_framework_device.dll` 核心函数。
* **强制延时缓冲**：当检测到摄像头捕获启动（Capture Start）时，程序会自动强制挂起可配置秒数（默认 5 秒），防止被瞬间偷拍。
* **全面覆盖**：同时拦截 DirectShow (DS) 和 Media Foundation (MF) 两种主流媒体框架的调用请求。
* **高效 IPC 通信**：底层 DLL 与前端 UI 之间采用**共享内存 + 互斥锁 + 事件通知**的机制进行通信，低延迟且无性能损耗。
* **纯粹安全**：纯本地运行，无网络请求，不收集任何用户数据。

### 配置系统
* **INI 配置文件**（`NoMoreMonitor\NoMoreMonitor.ini`，位于程序同目录下的隐藏文件夹 `NoMoreMonitor\` 中）：
  * 暂停秒数、倒计时文字。
  * OSD 三状态文字（起风了 / 风好大 / 风停了）与颜色。
  * 气泡通知标题 / 正文、托盘提示。
  * 文字位置（九宫格 + 像素微调）。
  * 三状态图片路径、是否显示图片、图片是否替代文字。
  * 控制台窗口：是否显示、颜色、标题、各日志文字。
  * 数据存储目录。
* **UTF-8 编码**：显式处理 BOM，中文配置在任何代码页下都能正确读写。

### 配置面板
* 托盘右键菜单 →「设置...」打开。
* **双层模式**：默认**简单模式**（暂停秒数、OSD 文字、OSD 颜色）；勾选「专业模式」展开全部选项（图片、气泡通知、数据目录、控制台等）。
* 专业模式状态会被**记住**，下次打开保持。
* 颜色块点击弹系统取色器；图片路径支持浏览与预览。

### 托盘菜单
* 「设置...」完整配置面板。
* 「配置」文字位置（九宫格 + 像素微调，实时预览，取消还原）。
* 「分析」基于历史 spy 数据的统计分析。
* 「退出」。

### OSD 全屏提示
* 全屏分层窗口（`UpdateLayeredWindow` 逐像素 alpha）。
* 文字、颜色、位置均可配置。
* 检测到 spy 结束后，文字**自然淡出**（约 1.5s）。
* 支持三状态各显示一张自定义图片（PNG/JPG/BMP/GIF 等，`stb_image` 纯 C 解码，不依赖 WIC/COM）。

### Spy 数据记录
* 每次 spy 会话记录**起点、终点、时长**，追加写入 `spy_log.csv`（原子追加 + 落盘）。
* 默认存储到程序同目录下的隐藏文件夹 `NoMoreMonitor\`，可在专业模式里自定义目录。

### 统计分析（GDI+）
* 基于历史数据绘制图表：
  * 起点分布（0-24h）：**圆统计核密度估计（circular KDE）** 找最可能的 spy 起点。
  * 时长分布：**中位数** 作为稳健的典型时长。

## 🛠️ 工作原理 (How it works)

当 `NoMoreMonitor_Dll.dll` 被注入到目标进程后：

1. **环境检测**：轮询等待并定位目标模块 `media_framework_device.dll` 加载。
2. **API Hooking**：利用 `MinHook`，通过导出表序号精准替换关键函数：
   * `ord:76` / `ord:86` - DirectShow (DS) 摄像头启动/停止捕获。
   * `ord:77` / `ord:87` - Media Foundation (MF) 摄像头启动/停止捕获。
3. **状态接管与延时**：捕获启动时，Hook 先拦截请求，通过共享内存 `Local\LilithSharedMem` 通知宿主，并执行可配置秒数的倒计时（智能区分 GUI 线程以防卡死），随后才放行原始调用。
4. **状态恢复**：捕获停止时，向宿主发送解除警报信号。

宿主程序会持续监控 `media_capture.exe`，出现后自动把 DLL 注入其中，进程退出后自动重新等待并再次注入。

## 🚀 快速开始 (Getting Started)

### 环境依赖
* Windows 操作系统（32 位目标进程）。
* [MinHook](https://github.com/TsudaKageyu/minhook) 库（已包含 `libMinHook.x86.lib`）。
* C/C++ 编译器（推荐 Visual Studio 2022/2026，平台工具集 v145，Win32 配置）。

### 编译
1. 克隆仓库：
   ```bash
   git clone https://github.com/lilith-is-all-you-need/NoMoreMonitor.git
   ```
2. 用 Visual Studio 打开 `NoMoreMonitor.slnx`，编译 **Win32** 配置，得到 `NoMoreMonitor.exe` 与 `NoMoreMonitor_Dll.dll`。
3. 将两个文件放到同一目录，运行 `NoMoreMonitor.exe`（建议以管理员身份运行）。

### 使用
1. 启动后选择「巨幅文本 + 通知」或「仅通知」。
2. 右键托盘图标 →「设置...」修改文字 / 颜色 / 暂停时间等（默认简单模式，勾选「专业模式」查看全部）。
3. 右键托盘图标 →「配置」调整 OSD 文字位置。
4. 右键托盘图标 →「分析」查看历史 spy 数据统计。
5. 数据与配置保存在程序同目录下的隐藏文件夹 `NoMoreMonitor\` 中。

## 🐛 已修复的问题 (Fixed Bugs)

* 托盘图标左右键无响应：`NOTIFYICON_VERSION_4` 下回调事件在 `lParam` 的 `LOWORD`，此前误判整个 `lParam`。
* 配置面板按钮全部失效：`STN_CLICKED` 与 `BN_CLICKED` 数值均为 0，此前按通知码区分会吞掉所有按钮点击，现改为按控件 ID 区分。
* 配置面板滚动容器上的控件消息到不了面板：新增内容窗口消息转发（`WM_COMMAND`/`WM_CTLCOLORSTATIC` 等）。
* 中文输入失败 / 英文显示异常：`DEFAULT_GUI_FONT` 位图字体对 Unicode/IME 支持差，改为系统消息字体（`lfMessageFont`）。
* 「专业模式」开关状态不持久：新增 `advanced_mode` 配置项并在保存时落盘。
* 配置文件中文乱码：改为显式 UTF-8 读写（含 BOM）。
* 控制台窗口缩放不生效：仅 `SetWindowPos` 缩不动控制台，需先按字体大小设置屏幕缓冲区。
* 自定义 spy 目录后不再记录：目录创建/文件写入失败改为显式报错（错误码），并修正目录存在性判断（需确为目录）。
* 自定义 spy 目录时“新建文件夹”权限不足：目录选择改用跨进程的 `IFileOpenDialog`（`FOS_PICKFOLDERS`），“新建文件夹”由 explorer 处理，不再受本进程 UAC/UIPI 隔离影响。
* 设置保存后控制台颜色/标题/显隐不立即生效：新增 `config_apply_console()`，启动与保存时统一应用。
* 设置保存无成功/失败提示：保存结果改为弹窗提示，失败时不关闭面板。
* 统计分析带宽为固定经验值：圆 KDE 带宽改为数据驱动（圆标准差 + Silverman 法则），时长直方图改用 90 分位数上限，小样本更稳健。

## 📝 待办事项 (TODO)

* **预览/浏览图片时触发 `winrt::hresult_error` 首次机会异常**：这是第三方 shell 扩展（百度网盘 `YunShellExtV1.dll` 等）在 shell 命名空间枚举时抛出的良性异常（`0x80070490`，被系统内部捕获，不影响 Release 运行），但调试器会中断。规避方式：Visual Studio「调试 → 窗口 → 异常设置」中取消勾选 C++ 异常的 `winrt::hresult_error`，或按 F5 继续；若仍想在代码层彻底规避，需换成纯文件系统选择器（不经过 shell，已评估但暂不采用）。
* **自定义 spy 数据目录写入被拒（未解决）**：即使以管理员运行（`TokenElevation` 确认已提权），`spy_data_append` 在自定义目录创建 `spy_log.csv` 仍报 `err=5`（拒绝访问），连手动 `mkdir` 可写的 `D:\test` 也会被“可写探测”判为不可写；但非提权 PowerShell 却能 `mkdir`。已排除普通 NTFS ACL（管理员本可绕过）与 `FILE_APPEND_DATA` 无法建文件的可能（实测可建）。高度怀疑是 Windows Defender「受控文件夹访问」或第三方杀软（360/火绒/电脑管家等）的防勒索/文档保护，按“未签名程序”在拦截（不区分是否管理员）。当前规避：自定义目录写失败会自动回退到程序同目录 `NoMoreMonitor\spy_log.csv`。待办：在安全软件中把 `NoMoreMonitor.exe` 加白（或关闭受控文件夹访问）后复测确认根因。


## ⚠️ 免责声明 (Disclaimer)

本项目仅供**学习操作系统底层机制、API Hook 技术交流与个人隐私保护测试**使用。
1. 本程序通过内存 Hook 技术实现，**不修改**目标软件的任何磁盘文件。
2. 请遵守所在机构的相关规章制度，请勿将本工具用于对抗正常的考试监考或合法合规的管理行为。
3. 开发者对因使用本工具引起的任何纠纷或后果不承担任何责任。

## 🤝 参与贡献 (Contributing)
如果你有更好的建议或发现了 Bug，欢迎提交 Issue 或 Pull Request！

## 📄 开源协议 (License)
本项目采用 [MIT License](LICENSE) 开源协议。
