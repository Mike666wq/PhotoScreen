# AGENTS.md

PhotoScreen 的共享事实基线。规划方（Codex 等规划模型）与执行方（MiniMax Code 等执行模型）都以此文件为准，避免每次交接靠对话复述。

## 项目定位

轻量、离线的双平台截图工具。支持 macOS 15.2+ Apple Silicon 与 Windows 11 x64。截图只在内存中编辑并复制到系统剪贴板，**软件不保存截图历史或文件**。

## 平台支持范围

| 平台 | 最低版本 | 架构 | 截图 API |
|---|---|---|---|
| macOS | 15.2 | arm64 | ScreenCaptureKit（`SCScreenshotManager`） |
| Windows | 11（build 22000） | x64 | DXGI Desktop Duplication |

其他平台不支持，`CMakeLists.txt` 会直接 `FATAL_ERROR`。

## 目录职责

```
src/
  main.cpp               入口：单实例闸门 + App 启动
  app.{h,cpp}            托盘、全局热键、截图生命周期、设置对话框
  captureoverlay.{h,cpp} 覆盖层：选区 / 工具栏 / 标注交互
  core/                  纯逻辑层，不得依赖平台 API 或 Widgets
    geometry.{h,cpp}     逻辑↔像素换算、坐标映射、放大镜取样
    annotation.{h,cpp}   稳定 ID、ToolStyle、Undo、文字排版、标注合成与选区渲染
    interaction.{h,cpp}  实际屏幕布局、控制点与约束几何
  platform/
    platform.h           平台抽象接口
    macos.mm / windows.cpp  平台实现
```

**硬约束**：

- `src/core/` 只依赖 Qt Core/Gui 的非 Widget 部分，不引入 `QWidget`、不引入平台 API。命中判定、坐标换算、绘制逻辑都必须落在 core，才能被 `tests/core_tests.cpp` 覆盖。
- `src/platform/` 内部可以使用原生坐标，但**对外一律交出 Qt 逻辑全局坐标**。
- `src/captureoverlay.cpp` 是最大的交互文件；新增工具时优先把可测逻辑下沉到 core。

## 坐标契约

`ScreenFrame` 内所有矩形统一为 **Qt 逻辑全局坐标**（虚拟桌面空间，与 `QScreen::geometry()` 同一坐标系）：

- `ScreenImage::geometry` — 该屏在虚拟桌面中的逻辑矩形
- `ScreenImage::image` — 该屏的**设备像素**图像
- `ScreenFrame::virtualGeometry` — 全部屏的并集
- `ScreenFrame::windowRects` — 窗口矩形，逻辑全局坐标

原生坐标（CG 全局显示坐标、DXGI 物理坐标）只允许出现在 `src/platform/` 内部，转换通过 `core/geometry.h` 的 `logicalMapFromNative()` / `AffineMap` 完成。

## 设置存储约定

`QSettings` 的落盘位置与类型保留方式**因后端而异**：Windows 走注册表，macOS 走 plist。注册表后端会把 `QColor`、`QStringList` 降级成普通字符串，读回来的 `QVariant` 实际类型与 plist 不一致，同一份代码在两个平台上的行为因此不同。

- 颜色一律以 **十六进制字符串**（`#rrggbb`）存放，读回时用 `QColor(...)` 解析；旧版本存的 `QColor` 由 `colorFromSettings()` 兼容。
- 颜色列表以 **逗号分隔的字符串** 存放，读回时 `split(',')`；旧版本存的 `QStringList` 由 `colorNamesFromSettings()` 兼容。
- 禁止直接依赖 `QVariant::value<QColor>()` / `value().toStringList()` 从设置里取回复杂类型。
- 进程启动必须设置组织名与应用名（`src/main.cpp`）。测试二进制同样要在 `initTestCase()` 里设置，否则 `QSettings` 落到与正式程序不同的位置。

## 构建与验证

**本地只改代码。所有测试由线上 GitHub Actions 执行。**

本机不安装 cmake / ninja / Qt6，本地只做静态自检（逐行读 diff、核对 Qt API 签名、include 完整性），**不把本地预检当作验证结论**。

```bash
# 本地：只允许静态检查，不允许构建
cmake -S . -B build && cmake --build build   # ❌ 不要在本地跑
```

线上 CI（`.github/workflows/build.yml`）：

- 矩阵：macOS 15 (arm64) + Windows 2025 (x64)
- Qt 6.12.0，MSVC 工具集固定 14.44
- `ctest` 四项：`photoscreen_core` / `photoscreen_overlay` / `photoscreen_visual_report` / `photoscreen_mac_image`（macOS 才有第四项）
- 每个 QTest 目标通过 `-o <build>/reports/<name>.txt,txt` 把逐条 PASS/FAIL 落盘；**Windows 上 `ctest` 不会回显 QTest 的标准输出，定位失败用例只能看这些日志文件**（CI 的 "Show QTest logs" 步骤会把它们贴进构建日志，证据包里也有一份）
- 每次构建上传 `Regression-*` 证据包（测试结果 + 合成图像报告 + QTest 日志）
- `main` / PR / 手动触发产出试用包；`vX.Y.Z` 标签或填写 `release_tag` 的手动触发**直接发布** Release（不再使用草稿）。发布后仍需按 `docs/acceptance.md` 做实机验收，未验证项不得标注为通过，发现问题发新版本。

**报告口径**：CI 通过 ≠ 真机通过。真机截图、系统屏幕录制授权、跨应用粘贴、真实桌面缩放对齐、开机启动、升级卸载、连续截图资源占用，**一律标注为「未验证」**，直到 `docs/acceptance.md` 记录填写完毕。

## 分支与 PR 节奏

```
本地改码 → 推 codex/<slug> 分支 → 开 PR → 等双平台 Actions 全绿 → 合并
```

- 分支命名：`codex/<slug>`，例如 `codex/fix-p0-core`
- 一次 PR 只做 1–2 个缺陷，便于回归定位
- 不直接向 `main` 推送
- 不改动远端已有分支和已发布 Release
- 每个 PR 描述必须写三段：**改了什么 / 用什么命令验证的 / 哪些仍未验证**

## 代码风格

- C++20，Qt 6。命名：类型 `PascalCase`，函数 `camelCase`，成员 `m_` 前缀，常量用 `QStringLiteral`。
- 注释与用户可见字符串用中文，与现有代码保持一致。
- 现有代码倾向紧凑写法（同一行多条语句）。**新增代码优先可读性**，但同一文件内保持风格统一。
- 头文件用 `#pragma once`，include 顺序按字母。
- 禁止：静默失败（吞掉错误）、在 core 层引入 Widgets、引入未在计划中说明的新依赖。

## 测试约定

- `tests/core_tests.cpp` — 纯逻辑：坐标换算、命中判定、绘制几何、Undo。CI 上跑，**必须有断言**。
- `tests/overlay_tests.cpp` — 覆盖层交互：用 `QTest::mousePress/mouseRelease/keyClick` 驱动，`QT_QPA_PLATFORM=offscreen`。
- `tests/visual_report.cpp` — 生成合成图像证据到 `PHOTOSCREEN_REPORT_DIR`。
- `tests/mac_image_tests.mm` — macOS 图像方向与色彩空间。
- `tests/fake_platform.cpp` — 为 overlay 测试提供失败的 `writeClipboard`。

新增交互功能时，**测试必须通过 `findChild` 用 `objectName` 定位控件**（例如 `widthControl` / `fontControl` / `doneButton` / `tool_3`），不要依赖指针成员。

## 验收文档

`docs/acceptance.md` 是实机验收清单，覆盖截图权限、窗口识别、多显示器与缩放、标注与中文输入、剪贴板、升级卸载、开机启动、资源占用。

**Actions 的自动测试不替代真实桌面验收。** 发布不再等待实机验收：每次发布后仍需在 Mac 和 Windows 实机走完清单并填写表格，未验证项保持「未验证」口径。

## v1.2 交互与渲染契约

- 标注以会话内稳定 `AnnotationId` 标识，选中对象与文字草稿不得保存可失效的数组索引。
- 工具默认样式与选中对象的当前样式分离；仅显式修改的字段更新该工具偏好，旧全局设置仅作兼容读取。
- 预览和导出共用选区底图、统一输出比例和绘制顺序；马赛克必须包含此前标注。局部重绘涉及马赛克时扩展到整块取样区域。
- 字体 Noto Sans SC 只由 CI 下载并验证、由测试进程加载；不修改本地或 runner 系统字体，不进入应用安装包。
- Regression 中的 UI 图片都是合成画面，不能据此宣称真机或输入法验收通过。

## 已知未修项（不要重复实现）

- 单实例获取失败时静默退出，无提示
- 覆盖层关闭后不恢复原应用键盘焦点
- Windows 截图线程 `detach()` 无收束，`AcquireNextFrame` 无重试
- macOS `windowRects()` 未过滤最小化窗口
- `annotationContains` 对文字用矩形外扩命中，范围偏大
