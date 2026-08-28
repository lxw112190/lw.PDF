#pragma once

#include <cstdint>
#include <string>

// Native PDF structural transformations backed by libqpdf.
//
// The Bridge layer translates JSON into these typed requests so the UI never
// touches qpdf page-range syntax, and the frontend never receives native
// paths: both input and output stay inside the native process.

enum class PdfTransformKind {
  ReversePages,
  RotatePages,
};

enum class PdfRotation {
  Left90,
  Right90,
  Rotate180,
};

enum class PdfPageSelectionKind {
  All,
  Single,
  Range,
};

struct PdfPageSelection {
  PdfPageSelectionKind kind = PdfPageSelectionKind::All;
  std::uint32_t page = 0;  // 1-based, only for Single
  std::string range;       // e.g. "1,3,5-8", only for Range
};

struct PdfTransformRequest {
  PdfTransformKind kind = PdfTransformKind::ReversePages;
  PdfRotation rotation = PdfRotation::Right90;
  PdfPageSelection pages;
};

enum class PdfTransformError {
  None = 0,
  SourceUnavailable,
  InvalidParams,
  PageRangeInvalid,
  PasswordRequired,
  OutputWriteFailed,
  SameFile,
  Failed,
};

struct PdfTransformResult {
  PdfTransformError error = PdfTransformError::None;
  std::uint32_t page_count = 0;
};

// Transforms input_path into output_path without modifying the input.
// Never overwrites: input and output must resolve to different files.
// Rotations are relative (+90 / -90 / +180), so an existing /Rotate on a
// page stacks with the requested turn.
PdfTransformResult TransformPdf(const std::wstring& input_path,
                                const std::wstring& output_path,
                                const PdfTransformRequest& request);
