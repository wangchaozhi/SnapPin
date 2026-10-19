# SnapPin

用 Qt 6 复刻 [Snipaste](https://www.snipaste.com/) 的截图 + 贴图工具（Windows / macOS / Linux）。

## 功能

- **截图**：自动识别窗口；窗口内的按钮、输入框、文字段落、图标只按截图像素识别（不查询 UI Automation，远程桌面 / 虚拟机里同样有效），拖拽框选时边缘自动吸附窗口 / 控件（按住 Alt 暂不吸附），放大镜取色，多屏 / 混合 DPI，选区可以跨屏幕；Windows 开启 HDR 时截图颜色、亮度和屏幕一致
- **标注**：矩形、椭圆、直线、箭头、画笔、荧光笔、马赛克、橡皮擦、文字、序号；预设色板、填充（实心图形 / 文字底色）、滚轮调粗细；可选中已画标注移动 / 删除 / 改样式，撤销重做
- **长截图**：选区内上下滚动，自动拼接（识别吸顶标题栏 / 底部工具栏）
- **贴图**：截图或剪贴板内容贴在屏幕上，缩放、透明度、旋转 / 翻转、缩略图、鼠标穿透、再次编辑标注；关闭的贴图可从托盘恢复
- **识别文字（OCR）**：截图工具栏、贴图右键菜单或“截图识字”快捷键，识别选区里的中英文，结果可编辑、复制。内置 [PP-OCRv5](https://github.com/PaddlePaddle/PaddleOCR) 模型，用 ONNX Runtime 离线识别，三个平台效果一致，不需要联网或安装语言包
- **截图历史**、设置（快捷键、开机自启（托盘菜单里也能直接开关）、截屏方式、截图带鼠标指针、文件名格式 / 默认格式 / JPG 质量 / 快速保存目录）、托盘、自动更新
- 每个标注工具各自记住上次用的颜色、粗细和填充；在资源管理器里复制图片文件后按 F3 直接贴出来

## 快捷键（默认）

| 快捷键 | 功能 |
|---|---|
| F1 | 截图 |
| F3 | 贴剪贴板 |
| Shift+F3 | 隐藏 / 显示所有贴图 |
| Ctrl+F3 | 光标下贴图的鼠标穿透开 / 关（光标不在贴图上时取消所有贴图的穿透） |

截图时：双击 / 回车 / Ctrl+C 复制，Ctrl+S 保存，Ctrl+Shift+S 快速保存到“图片/SnapPin”（不弹对话框），Ctrl+T 贴图，Ctrl+L 长截图，Ctrl+R 识别文字，Esc 取消；
Ctrl+A 全屏，`,` / `.` 切换到之前截过的区域；未选区时 Shift 切换颜色格式（RGB / HEX），C 复制颜色。
标注时：滚轮调粗细（选中已画的标注时调它的粗细），按住 Shift 画正方形 / 圆、直线和箭头吸附 45°，Delete 删除选中的标注。
macOS 上文中的 Ctrl 均为 Command；F 键在 Mac 键盘上可能需要按住 fn。
文字工具：回车换行，Ctrl+回车或点击别处完成，Esc 取消。
贴图上：滚轮缩放，Ctrl+滚轮透明度，0 原始大小，1 / 2 旋转，3 / 4 水平 / 垂直翻转，Shift+双击缩略图，空格编辑，R 识别文字，双击 / Esc 关闭；
右键菜单或 Ctrl+F3 设置鼠标穿透（光标移到贴图上再按 Ctrl+F3 恢复），托盘“恢复关闭的贴图”找回误关的贴图（可在设置里绑定快捷键）。
托盘图标：Windows 左键截图、中键贴剪贴板、右键菜单；macOS / Linux 单击只弹出菜单。

## 安装

从 [Releases](https://github.com/wangchaozhi/SnapPin/releases) 下载 `SnapPin-x.y.z-windows-x64.zip`，解压后运行 `SnapPin.exe`。
新版本发布后程序会提示更新，确认后自动下载、替换并重启（三个平台都支持，重启后会提示更新结果）。

macOS（14 及以上，Apple 芯片）下载 `SnapPin-x.y.z-macos-arm64.zip`，解压后把 `SnapPin.app` 拖到“应用程序”。
文字识别用的 ONNX Runtime 新版不再提供 Intel 版 macOS 库，所以从 0.2.41 起不再发布 Intel 版本；
0.2.40 及更早的通用版（`macos-universal`）不会自动更新到 Apple 芯片版本，需要手动下载一次。
安装包用固定的自签名证书签名（不是苹果开发者证书），第一次打开需要在“系统设置 > 隐私与安全性”里允许；之后升级时已授予的权限保持有效。程序需要两项权限（系统设置 > 隐私与安全性）：

- **屏幕录制**：截屏必需，没有时只能截到桌面壁纸
- **辅助功能**：识别窗口内的控件、长截图时接管滚动；不授权时这两项退化为只识别窗口、直接滚动

自动更新会校验新版本的签名与当前程序一致后再替换 `SnapPin.app`，失败时保留原版本；`.app` 所在目录没有写权限（或直接从“下载”里打开）时改为打开发布页。

Linux 下载 `SnapPin-x.y.z-linux-x86_64.AppImage`，`chmod +x` 后直接运行。自动更新会原地替换这个 AppImage 文件（文件名不变，快捷键命令继续有效），放在没有写权限的目录时改为打开发布页。X11 和 Wayland 会话的支持程度不同：

| | X11 | Wayland |
|---|---|---|
| 截屏 | ✅ | ✅ 通过 xdg-desktop-portal，首次使用桌面会询问是否允许 |
| 识别窗口 | ✅ | ❌ 系统不允许读取其他窗口位置，只能手动框选 |
| 全局快捷键 | ✅ | ❌ 需要在系统设置里自己绑定（见下） |
| 长截图 | ✅ | ❌ 无法连续截屏和摆放选区边框，不提供 |
| 识别控件、长截图接管滚动 | ❌ | ❌ |
| 贴图自由摆放 | ✅ | ⚠️ 初始位置由合成器决定，之后可以拖动 |
| 识别文字 | ✅ | ✅ |

Wayland 下在系统设置的键盘快捷键里添加自定义快捷键，命令填 `/路径/SnapPin.AppImage capture`。
可用的动作：`capture`（截图）、`ocr_capture`（截图识字）、`pin_clipboard`（贴剪贴板）、`toggle_pins`（隐藏 / 显示贴图）、`history`（截图历史）。
SnapPin 已在运行时，这条命令只会把动作交给正在运行的实例。

## 构建

需要 Qt 6.7+（Widgets、Network）和 CMake 3.21+。配置时会下载 ONNX Runtime 和 PP-OCRv5 模型（约 30～80MB，校验 SHA256），放在构建目录的 `_deps` 里，之后不再重复下载。

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=<Qt 安装目录>/msvc2022_64
cmake --build build --config Release
ctest --test-dir build -C Release
```

macOS 上可以用 Homebrew 的 Qt：

```bash
brew install qt cmake ninja
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=$(brew --prefix qt)
cmake --build build
open build/SnapPin.app
```

Linux（Ubuntu 26.04 为例）：

```bash
sudo apt install build-essential cmake ninja-build qt6-base-dev libxcb1-dev libgl-dev
cmake -S . -B build -G Ninja
cmake --build build
```

macOS 的屏幕录制、辅助功能权限跟签名绑定。默认的 ad-hoc 签名每次编译都会变，权限随之失效；
本机开发时用固定的签名身份编译，授权一次即可长期有效：

```bash
cmake -S . -B build -DSNAPPIN_CODESIGN_IDENTITY="SnapPin Release"
```

推送 `v*` 标签会触发 GitHub Actions 构建并发布 Release。

## 代码结构

| 目录 | 内容 |
|---|---|
| `src/platform` | 平台接口；Windows 实现（DXGI / GDI 截屏、UI Automation、全局快捷键）、macOS 实现（ScreenCaptureKit、CGWindowList + 辅助功能、Carbon 快捷键、CGEventTap）与 Linux 实现（X11：EWMH 窗口列表、XGrabKey；Wayland：xdg-desktop-portal 截屏） |
| `src/capture` | 截图遮罩（状态模式）、工具栏、长截图拼接 |
| `src/annotation` | 标注图形、工具（策略模式）、撤销栈（命令模式） |
| `src/pin` | 贴图窗口与管理 |
| `src/history` `src/update` `src/app` | 截图历史、自动更新、设置与应用上下文 |
