# OffWork 🐱

一只陪你等下班的 Windows 原生悬浮窗，实时显示下班倒计时、工作进度和今日已赚金额。

[官网](https://offwork.ziton-ai.cc) · [下载](https://github.com/alertform/OffWork/releases)

## 功能

- 自定义上下班时间，或按时长计算下班时间，支持跨午夜班次。
- 自定义午休起止时间，午休期间显示「午休中」，金额和进度暂停累计。
- 按日薪实时计算已赚金额，支持一键隐藏金额。
- 拖入打卡截图，本机 OCR 自动填写上班时间，不上传图片。
- 窗口置顶、缩放、透明度调节，支持系统托盘与开机自启。
- 下班前五分钟及到点提醒，支持自动检查更新、点击下载并安装。

<img src="docs/drag-drop.gif" width="360" alt="拖入打卡截图，自动将上班时间填为 08:55">

## 下载与使用

前往 [GitHub Releases](https://github.com/alertform/OffWork/releases) 下载 `OffWork-Setup.exe`（安装版）或 `OffWork.exe`（免安装版）。支持 Windows 10/11 x64，无需额外运行时。

运行后展开「设置」，填写上班时间、下班时间或时长、日薪和午休时间，设置自动保存。首次使用默认选择「时长」，已有的有效结束方式设置会保留。关闭窗口会隐藏到系统托盘，右键托盘图标可退出。

截图识别使用 Windows 自带 OCR；如提示缺少识别语言，请在系统语言设置中安装简体中文 OCR 组件。

OCR 只填写上班时间：「下班时间」模式保留选定的固定下班时间；「工作时长」模式保留时长并重算下班时间。识别成功后立即保存并刷新界面；失败时会展开设置显示具体原因，并尝试显示托盘提示。设置折叠时，窗口右下角显示当前版本号；展开后可在底部查看。

## 构建

安装 Visual Studio 的 C++ 桌面开发工具、Windows SDK 和 CMake 3.24+，在 Developer PowerShell 中执行：

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --target OffWork --parallel
```

产物：`build\Release\OffWork.exe`（C++20 / Win32，静态 MSVC 运行时）。
