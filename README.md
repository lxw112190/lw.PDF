# lw.PDF

[English](README.en.md) | 简体中文

[![CI](https://github.com/lxw112190/lw.PDF/actions/workflows/ci.yml/badge.svg)](https://github.com/lxw112190/lw.PDF/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/lxw112190/lw.PDF?display_name=tag)](https://github.com/lxw112190/lw.PDF/releases/latest)

轻量的 Windows 桌面 PDF 查看器，基于 Vue 3、TypeScript、PDF.js 与 C++ Native WebView2。

## 功能

- 浏览器和桌面模式打开 PDF
- 连续滚动阅读、文本选择和链接跳转
- 页码导航、缩放、适合宽度和适合页面
- 关键词搜索、缩略图懒加载和文档目录
- 最近文件列表，以及按 PDF 指纹恢复页码、缩放和页内位置
- 单开关护眼模式：轻微暖化 PDF 页面并降低纯白背景刺激
- 阅读型批注：高亮、中文文字、手写，以及撤销、重做、删除和另存为
- 扫描 PDF 视觉化页面整理：缩略图多选、拖拽排序、反转、旋转、撤销/重做和另存为
- 可靠打印：PDF.js 专用打印渲染、未保存批注、进度显示、取消准备和可打印区域安全适配
- 大型 PDF 首屏优先：按目标页结束加载，目录、最近文件和缩略图后台初始化
- Native FileGrant + HTTP Range：大型 PDF 不经过 IPC/Base64 复制
- 内置 PDF.js CMap 与标准字体资源，兼容未嵌入中文字体的 PDF
- 关于窗口、应用图标、动态文档标题与 Windows 文件关联

> 页面整理目前仅 Windows 桌面版支持。

## 本地开发

```powershell
npm install
npm run dev
```

浏览器模式通过文件选择器载入 PDF；桌面模式的文件选择、命令行打开和拖放全部由 C++ Native 宿主生成临时 FileGrant URL。网页层不会读取桌面文件，也不会把整份 PDF 复制进 JavaScript 内存。

## 构建 Windows 原生程序

```powershell
cmake -S . -B build-native -DBUILD_TESTING=ON
cmake --build build-native --config Release --target lw_pdf
```

生成文件为 `build-native\Release\lw.PDF.exe`。前端资源会在构建时嵌入 EXE，运行时自动释放到当前用户的 LocalAppData 缓存；无需 `lw.Web2Exe` 或其他打包运行时。

页面整理基于 [qpdf](https://qpdf.readthedocs.io/)（libqpdf 12.2.0）在 Native 进程内做 PDF 结构级变换，不重编码页面内容、不把整份 PDF 复制进 JavaScript，并始终“另存为新 PDF”，不会覆盖原文件。旋转采用相对方向，已有 `/Rotate` 的页面会在其基础上叠加。整理受密码保护或禁止页面组装的 PDF 暂不支持。

批注由 PDF.js Annotation Editor 生成，Native 只提供限时一次性的 SaveGrant，并将保存数据流式写入临时文件后原子替换。批注保存同样始终另存为新 PDF，不参与 qpdf 页面变换；保存后会重新打开生成的文件并恢复阅读位置。

## Windows 集成

lw.PDF 的 Native 宿主使用 HKEY_CURRENT_USER 注册 `.pdf` 打开方式与右键菜单，不会修改 Windows 的默认 PDF 应用。移动 EXE 后，Windows 集成窗口会提示修复关联。

## 验证

```powershell
npm test
npm run build
ctest --test-dir build-native -C Release --output-on-failure -R "^lw_pdf"
```

GitHub Actions 会在推送和拉取请求时自动执行依赖安装、构建和测试。

Native 测试重点覆盖 HTTP Range 边界、受限文件流、128 MiB 大 PDF 和连续 FileGrant 切换，防止桌面文件通路退回整文件复制。


最近文件路径仅由 Native 保存在 `%LocalAppData%\lw.PDF\recent.json`，网页层只接收不透明 ID；重新打开时会重新校验文件并签发新的 FileGrant。阅读位置保存在 WebView2 本地存储中，以 PDF 指纹区分文档，最多保留 100 份记录。

## 诊断日志

Native 日志位于 `%LocalAppData%\lw.PDF\logs\lw.PDF.log`。Release 版本仅记录关键生命周期、桌面拖放、Bridge 失败和 WebView2 错误；Debug 版本额外记录 FileGrant、Range 与 HRESULT 细节。日志达到 2 MiB 后会轮换为 `lw.PDF.previous.log`，最多保留当前和上一份日志。

## CI 与发布

- 每次推送和拉取请求都会验证前端与 Windows x64 Native 构建。
- Windows 构建包可在对应的 [GitHub Actions](https://github.com/lxw112190/lw.PDF/actions/workflows/ci.yml) 运行页面下载，保留 30 天。
- 推送与 `package.json` 版本一致的 `v*` 标签后，CI 会自动创建 GitHub Release，并上传 Windows x64 ZIP 与 SHA256 校验文件。
- `package.json` 是前端、CMake、EXE 资源信息和发布包的统一版本来源。
- 稳定版可从 [Latest Release](https://github.com/lxw112190/lw.PDF/releases/latest) 直接下载。

## 架构

```text
Windows file dialog / command line / drop
                    ↓
       C++ Native FileGrant
                    ↓
PDF.js ← HTTPS Range responses from bounded native streams
                    ↓
PDF.js Annotation Editor → SaveGrant PUT → Native atomic save
```

## 联系与支持

- 作者：天天代码码天天
- QQ：819069052
- QQ Group：C# 人工智能实践 | 群号：758616458

如果项目对你有帮助，可以扫码支持维护：

<img src="docs/assets/sponsor.jpg" alt="微信赞助二维码" width="260">
