# lw.PDF

轻量跨平台桌面 PDF 查看器，基于 Vue 3、TypeScript、PDF.js 与 lw.Web2App。

## 功能

- 浏览器和桌面模式打开 PDF
- 连续滚动阅读、文本选择和链接跳转
- 页码导航、缩放、适合宽度和适合页面
- 关键词搜索、缩略图懒加载和文档目录
- Desktop FileGrant + HTTP Range：大型 PDF 不经过 IPC/Base64 复制

## 本地开发

```powershell
npm install
npm run dev
```

浏览器模式通过文件选择器载入 PDF；桌面模式通过 `window.lw.invoke('dialog.openFile')` 获取临时 FileGrant URL。

## 构建与桌面打包

```powershell
npm run build

# 替换为本机 lw.Web2App.exe 的实际位置
lw.Web2App.exe pack .\dist .\lw.PDF.exe `
  --title "lw.PDF" `
  --app-id com.lw.pdf `
  --width 1280 --height 820 --windowed `
  --ipc --ipc-capability dialog.file
```

`lwweb.json` 是正式发布配置。`dist/` 和生成的 EXE 均为构建产物，不提交仓库。

## 架构

```text
Vue UI → PDF.js Viewer → lw.Web2App IPC / FileGrant → Local HTTP Range
```
