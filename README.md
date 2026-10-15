# SnapPin

用 Qt 6 复刻 [Snipaste](https://www.snipaste.com/) 的截图 + 贴图工具（Windows）。

## 功能

- **截图**：自动识别窗口和界面控件（UI Automation），拖拽框选，放大镜取色，多屏 / 混合 DPI
- **标注**：矩形、椭圆、箭头、画笔、荧光笔、马赛克、文字；可选中已画标注移动 / 删除，撤销重做
- **长截图**：选区内上下滚动，自动拼接（识别吸顶标题栏 / 底部工具栏）
- **贴图**：截图或剪贴板内容贴在屏幕上，缩放、透明度、再次编辑标注
- **截图历史**、设置（快捷键、开机自启、截屏方式）、托盘、自动更新

## 快捷键（默认）

| 快捷键 | 功能 |
|---|---|
| F1 | 截图 |
| F3 | 贴剪贴板 |
| Shift+F3 | 隐藏 / 显示所有贴图 |

截图时：双击 / 回车 / Ctrl+C 复制，Ctrl+S 保存，Ctrl+T 贴图，Ctrl+L 长截图，Esc 取消。
贴图上：滚轮缩放，Ctrl+滚轮透明度，空格编辑，双击 / Esc 关闭。

## 安装

从 [Releases](https://github.com/wangchaozhi/SnapPin/releases) 下载 `SnapPin-x.y.z-windows-x64.zip`，解压后运行 `SnapPin.exe`。
新版本发布后程序会提示更新，确认后自动下载、替换并重启。

## 构建

需要 Qt 6.7+（Widgets、Network）和 CMake 3.21+。

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=<Qt 安装目录>/msvc2022_64
cmake --build build --config Release
ctest --test-dir build -C Release
```

推送 `v*` 标签会触发 GitHub Actions 构建并发布 Release。

## 代码结构

| 目录 | 内容 |
|---|---|
| `src/platform` | 平台接口与 Windows 实现（DXGI / GDI 截屏、窗口与控件识别、全局快捷键） |
| `src/capture` | 截图遮罩（状态模式）、工具栏、长截图拼接 |
| `src/annotation` | 标注图形、工具（策略模式）、撤销栈（命令模式） |
| `src/pin` | 贴图窗口与管理 |
| `src/history` `src/update` `src/app` | 截图历史、自动更新、设置与应用上下文 |
