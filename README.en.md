# lw.PDF

English | [简体中文](README.md)

[![CI](https://github.com/lxw112190/lw.PDF/actions/workflows/ci.yml/badge.svg)](https://github.com/lxw112190/lw.PDF/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/lxw112190/lw.PDF?display_name=tag)](https://github.com/lxw112190/lw.PDF/releases/latest)

A lightweight Windows desktop PDF viewer powered by Vue 3, TypeScript, PDF.js, and native C++ WebView2.

## Features

- Open PDF files in browser and desktop modes
- Continuous scrolling, text selection, and link navigation
- Page navigation, zoom, fit-to-width, and fit-to-page modes
- Full-text search, lazy-loaded thumbnails, and document outlines
- Recent files plus page, zoom, and in-page reading-position restoration by PDF fingerprint
- One-click eye-care mode with subtly warmer PDF pages and reduced white glare
- Reading annotations: highlights, Chinese free text, ink, undo, redo, delete, and Save As
- Visual scanned-PDF page organizer with multi-select thumbnails, drag-to-reorder, reverse, rotation, undo/redo, and Save As
- Reliable printing with a dedicated PDF.js print pipeline, unsaved annotations, progress, and cancellation during preparation
- Native FileGrant with HTTP Range support for large PDFs without IPC or Base64 copies
- Bundled PDF.js CMaps and standard fonts for PDFs with non-embedded Chinese fonts
- About dialog, application icon, dynamic document titles, and Windows file associations

> Page organization is currently supported in the Windows desktop app only.

## Local Development

```powershell
npm install
npm run dev
```

Browser mode loads PDFs through the browser file picker. In desktop mode, file selection, command-line opening, and drag-and-drop all create temporary FileGrant URLs in the native C++ host. The web layer never reads a desktop file or copies an entire PDF into JavaScript memory.

## Build the Native Windows Application

```powershell
cmake -S . -B build-native -DBUILD_TESTING=ON
cmake --build build-native --config Release --target lw_pdf
```

The executable is generated at `build-native\Release\lw.PDF.exe`. Frontend assets are embedded into the executable during the build and extracted to the current user's LocalAppData cache at runtime. `lw.Web2Exe` and other packaging runtimes are not required.

Page organization is built on [qpdf](https://qpdf.readthedocs.io/) (libqpdf 12.2.0) and performs structural PDF transformations inside the native process: page content is never re-encoded, the whole PDF is never copied into JavaScript, and results are always saved as a new file without overwriting the original. Rotations are relative, stacking on top of any existing `/Rotate` entries. Password-protected or assembly-restricted PDFs are not supported yet.

Annotations are created by the PDF.js Annotation Editor. Native code only issues a short-lived, one-time SaveGrant and streams the saved bytes to a temporary file before an atomic replacement. Annotation saving also always uses Save As, does not involve qpdf page transforms, and reopens the resulting file while restoring the reading position.

## Windows Integration

The native lw.PDF host registers `.pdf` Open With entries and a context-menu command under HKEY_CURRENT_USER. It never changes the current default PDF application. If the executable is moved, the Windows Integration dialog detects the stale path and offers to repair the registration.

## Verification

```powershell
npm test
npm run build
ctest --test-dir build-native -C Release --output-on-failure -R "^lw_pdf"
```

GitHub Actions runs dependency installation, builds, and tests for every push and pull request.

Native tests focus on HTTP Range boundaries, bounded file streams, a 128 MiB PDF, and continuous FileGrant switching so the desktop data path cannot regress to whole-file copying.


Native code alone stores recent file paths in `%LocalAppData%\lw.PDF\recent.json`; the web layer receives only opaque IDs. Reopening a recent file validates it again and issues a fresh FileGrant. Reading positions are stored in WebView2 local storage by PDF fingerprint, with at most 100 document records retained.

## Diagnostic Logs

Native logs are written to `%LocalAppData%\lw.PDF\logs\lw.PDF.log`. Release builds record only key lifecycle, desktop-drop, Bridge failure, and WebView2 events. Debug builds also record FileGrant, Range, and HRESULT details. At 2 MiB, the log rotates to `lw.PDF.previous.log`; only the current and previous logs are retained.

## CI and Releases

- Every push and pull request verifies both the frontend and Windows x64 native builds.
- Windows packages can be downloaded from the corresponding [GitHub Actions](https://github.com/lxw112190/lw.PDF/actions/workflows/ci.yml) run and are retained for 30 days.
- Pushing a `v*` tag that matches the version in `package.json` automatically creates a GitHub Release with the Windows x64 ZIP and SHA256 checksum.
- `package.json` is the single version source for the frontend, CMake, executable metadata, and release package.
- Stable builds are available from the [Latest Release](https://github.com/lxw112190/lw.PDF/releases/latest).

## Architecture

```text
Windows file dialog / command line / drop
                    ↓
       C++ Native FileGrant
                    ↓
PDF.js ← HTTPS Range responses from bounded native streams
                    ↓
PDF.js Annotation Editor → SaveGrant PUT → Native atomic save
```

## Contact and Support

- Author: 天天代码码天天
- QQ: 819069052
- QQ Group: C# 人工智能实践 | Group ID: 758616458

If this project helps you, you can scan the QR code to support its maintenance:

<img src="docs/assets/sponsor.jpg" alt="WeChat sponsorship QR code" width="260">
