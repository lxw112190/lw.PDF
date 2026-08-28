# Third-Party Notices

lw.PDF bundles or statically links the following third-party components.
The license text of each component applies to that component only.

## qpdf 12.2.0

- Source: https://github.com/qpdf/qpdf
- License: Apache License 2.0
- Used for structural PDF transformations (reverse page order, page rotation) inside the native process.

## zlib 1.3.1

- Source: https://zlib.net/
- License: zlib License
- Statically linked as a dependency of libqpdf.

## libjpeg-turbo 3.0.4

- Source: https://libjpeg-turbo.org/
- License: IJG (Independent JPEG Group) License / modified (3-clause) BSD License
- Statically linked as a dependency of libqpdf.

## miniz 3.1.0

- Source: https://github.com/richgel999/miniz
- License: MIT
- Used by the native host.

## nlohmann/json 3.12.0

- Source: https://github.com/nlohmann/json
- License: MIT
- Used by the native host for the Bridge JSON protocol.

## Microsoft Edge WebView2 SDK

- Source: https://www.nuget.org/packages/Microsoft.Web.WebView2/
- License: BSD-style license, see the SDK package.
- Hosts the frontend and serves the embedded PDF.js assets.

## PDF.js 4.10.38

- Source: https://github.com/mozilla/pdf.js
- License: Apache License 2.0
- Renders PDFs in the frontend.

## Vue 3

- Source: https://github.com/vuejs/core
- License: MIT
- Frontend UI framework.

---

This file is not an exhaustive list of every transitive npm dependency; see `package-lock.json` and the npm packages themselves for the complete dependency tree and license information.
