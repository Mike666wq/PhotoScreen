# 第三方组件

PhotoScreen 使用动态链接的 Qt 6.12.0 库。GNU LGPL 3.0 和 GPL 3.0 许可文本随安装包存放在 `Qt-Licenses` 目录。Qt 开源许可证说明和对应版本源代码见：
https://www.qt.io/licensing/open-source-lgpl-obligations 、
https://github.com/qt/qtbase/tree/v6.12.0 和 https://github.com/qt/qtsvg/tree/v6.12.0。

Windows 版本包含 Microsoft Visual C++ 可再分发运行库。其许可随 Microsoft Visual Studio 的可再分发组件提供：
https://learn.microsoft.com/cpp/windows/redistributing-visual-cpp-files

## CI 回归测试字体

合成界面证据使用 Google Fonts 的 Noto Sans SC（SIL Open Font License 1.1）。
Actions 从固定提交 `a85815a42757630ce188fdad368c2dfc444d4773` 下载并验证 Git blob，
在 Regression 证据包中记录来源、SHA-256 与许可证。字体只由测试进程加载，
不安装到系统、不进入 PhotoScreen 应用或 EXE/DMG。
来源：https://github.com/google/fonts/tree/a85815a42757630ce188fdad368c2dfc444d4773/ofl/notosanssc
