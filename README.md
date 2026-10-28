# OffWork

陪你等下班的 Windows 悬浮窗：下班倒计时、工作进度和今日已赚金额。

[官网](https://offwork.ziton-ai.cc) · [下载最新版](https://github.com/Token-Rap/OffWork/releases/latest)

## 功能

- 拖入打卡截图，本机 OCR 自动填写上班时间；失败原因会显示在设置中。
- 默认使用「时长」模式：下班 = 上班 + 净工作时长 + 午休时长，也可指定固定下班时间。
- 午休时收入和进度暂停；可选「距离下班时间扣除午休」，默认关闭。
- 支持金额隐藏、窗口置顶、缩放、透明度、托盘、开机自启和下班提醒。
- 支持应用内更新；v0.4.5 在设置折叠时也会在右下角显示版本号。

<img src="docs/drag-drop.gif" width="360" alt="拖入打卡截图，自动填写上班时间">

## 下载与使用

在 [GitHub Releases](https://github.com/Token-Rap/OffWork/releases/latest) 下载 `OffWork-Setup.exe`（安装版）或 `OffWork.exe`（免安装版），支持 Windows 10/11 x64。

运行后展开「设置」，填写上班时间、工作时长、午休和日薪，自动保存。拖入截图即可更新上班时间，时长模式会重算下班时间；固定下班时间模式保持原值。关闭窗口后驻留托盘，右键托盘图标可退出。

## 构建

安装 Visual Studio C++ 桌面开发工具、Windows SDK 和 CMake 3.24+，在 Developer PowerShell 中运行：

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --target OffWork --parallel
```

产物：`build\Release\OffWork.exe`。
