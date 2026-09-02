# lw.PDF 启动诊断

正常双击 `lw.PDF.exe` 不会打开控制台。遇到“双击没有反应”时，在 PowerShell 中运行：

```powershell
.\lw.PDF.exe --console
```

如果需要确保 PowerShell 等待程序结束，可运行：

```powershell
cmd /c start "" /wait lw.PDF.exe --console
```

也可以同时打开指定文件：

```powershell
.\lw.PDF.exe --console "D:\test.pdf"
```

请保存控制台内容，并同时提供：

- `%LocalAppData%\lw.PDF\logs\lw.PDF.log`（如果存在）
- `lw.PDF.exe` 的 SHA256
- Windows 版本和 WebView2 Runtime 版本

诊断日志不会输出 PDF 的完整本地路径。若 `--console` 没有任何输出且日志文件也不存在，问题可能发生在应用进入 `wWinMain` 之前，应继续检查 Windows 安全策略、文件隔离和签名状态。
