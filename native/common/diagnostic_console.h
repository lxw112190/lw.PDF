#pragma once

#include <string_view>

// Enables the opt-in troubleshooting console for the current process. Normal
// launches never call this function, so the application remains a GUI-only
// Windows program for regular users.
bool EnableDiagnosticConsole() noexcept;
bool IsDiagnosticConsoleEnabled() noexcept;
void WriteDiagnosticConsole(std::string_view line) noexcept;
