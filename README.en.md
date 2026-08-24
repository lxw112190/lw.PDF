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
- Native FileGrant with HTTP Range support for large PDFs without IPC or Base64 copies
- About dialog, application icon, dynamic document titles, and Windows file associations

## Local Development

```powershell
npm install
npm run dev
```

Browser mode loads PDFs through the browser file picker. Desktop mode obtains a temporary FileGrant URL through the controlled C++ Native Bridge.

## Build the Native Windows Application

```powershell
cmake -S . -B build-native -DBUILD_TESTING=ON
cmake --build build-native --config Release --target lw_pdf
```

The executable is generated at `build-native\Release\lw.PDF.exe`. Frontend assets are embedded into the executable during the build and extracted to the current user's LocalAppData cache at runtime. `lw.Web2Exe` and other packaging runtimes are not required.

## Windows Integration

The native lw.PDF host registers `.pdf` Open With entries and a context-menu command under HKEY_CURRENT_USER. It never changes the current default PDF application. If the executable is moved, the Windows Integration dialog detects the stale path and offers to repair the registration.

## Verification

```powershell
npm test
npm run build
ctest --test-dir build-native -C Release --output-on-failure
```

GitHub Actions runs dependency installation, builds, and tests for every push and pull request.

## CI and Releases

- Every push and pull request verifies both the frontend and Windows x64 native builds.
- Windows packages can be downloaded from the corresponding [GitHub Actions](https://github.com/lxw112190/lw.PDF/actions/workflows/ci.yml) run and are retained for 30 days.
- Pushing a `v*` tag that matches the version in `package.json` automatically creates a GitHub Release with the Windows x64 ZIP and SHA256 checksum.
- `package.json` is the single version source for the frontend, CMake, executable metadata, and release package.
- Stable builds are available from the [Latest Release](https://github.com/lxw112190/lw.PDF/releases/latest).

## Architecture

```text
Vue UI → PDF.js Viewer → C++ Native Bridge → FileGrant / HTTP Range
```

## Contact and Support

- Author: 天天代码码天天
- QQ: 819069052
- QQ Group: C# 人工智能实践 | Group ID: 758616458

If this project helps you, you can scan the QR code to support its maintenance:

<img src="docs/assets/sponsor.jpg" alt="WeChat sponsorship QR code" width="260">
