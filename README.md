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
- Native FileGrant + HTTP Range：大型 PDF 不经过 IPC/Base64 复制
- 关于窗口、应用图标、动态文档标题与 Windows 文件关联

## 本地开发

```powershell
npm install
npm run dev
```

浏览器模式通过文件选择器载入 PDF；桌面模式通过受控的 C++ Native Bridge 获取临时 FileGrant URL。

## 构建 Windows 原生程序

```powershell
cmake -S . -B build-native -DBUILD_TESTING=ON
cmake --build build-native --config Release --target lw_pdf
```

生成文件为 `build-native\Release\lw.PDF.exe`。前端资源会在构建时嵌入 EXE，运行时自动释放到当前用户的 LocalAppData 缓存；无需 `lw.Web2Exe` 或其他打包运行时。

## Windows 集成

lw.PDF 的 Native 宿主使用 HKEY_CURRENT_USER 注册 `.pdf` 打开方式与右键菜单，不会修改 Windows 的默认 PDF 应用。移动 EXE 后，Windows 集成窗口会提示修复关联。

## 验证

```powershell
npm test
npm run build
ctest --test-dir build-native -C Release --output-on-failure
```

GitHub Actions 会在推送和拉取请求时自动执行依赖安装、构建和测试。

## CI 与发布

- 每次推送和拉取请求都会验证前端与 Windows x64 Native 构建。
- Windows 构建包可在对应的 [GitHub Actions](https://github.com/lxw112190/lw.PDF/actions/workflows/ci.yml) 运行页面下载，保留 30 天。
- 推送与 `package.json` 版本一致的 `v*` 标签后，CI 会自动创建 GitHub Release，并上传 Windows x64 ZIP 与 SHA256 校验文件。
- `package.json` 是前端、CMake、EXE 资源信息和发布包的统一版本来源。
- 稳定版可从 [Latest Release](https://github.com/lxw112190/lw.PDF/releases/latest) 直接下载。

## 架构

```text
Vue UI → PDF.js Viewer → C++ Native Bridge → FileGrant / HTTP Range
```

## 联系与支持

- 作者：天天代码码天天
- QQ：819069052
- QQ Group：C# 人工智能实践 | 群号：758616458

如果项目对你有帮助，可以扫码支持维护：

<img src="docs/assets/sponsor.jpg" alt="微信赞助二维码" width="260">
