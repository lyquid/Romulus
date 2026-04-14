#pragma once

#include "romulus/core/types.hpp"

// ImGui and backends — included as system headers to avoid third-party warnings.
// The SYSTEM include paths are set in CMakeLists.txt.
#if defined(__GNUC__) || defined(__clang__)
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wold-style-cast"
#endif
#include <imgui.h>
#if defined(__GNUC__) || defined(__clang__)
  #pragma GCC diagnostic pop
#endif

#include <cstdint>
#include <cstdio>
#include <string>

namespace romulus::gui {

inline constexpr int k_WindowWidth = 1280;
inline constexpr int k_WindowHeight = 720;
inline constexpr auto* k_WindowTitle = "ROMULUS — ROM Collection Verifier";
inline constexpr auto* k_GlslVersion = "#version 130";
inline constexpr float k_ToastDuration = 2.5F;
inline constexpr float k_ToastPaddingH = 16.0F; // horizontal padding on each side
inline constexpr float k_ToastPaddingV = 10.0F; // vertical padding on each side
inline constexpr float k_ToastMarginRight = 10.0F;
inline constexpr float k_ToastMarginBottom = 50.0F;
inline constexpr float k_ToastRounding = 5.0F;
// Toast colours — encoded as per-channel alpha multipliers (0–255 range before scaling).
inline constexpr float k_ToastBgAlpha = 220.0F;
inline constexpr float k_ToastBorderAlpha = 180.0F;
inline constexpr float k_ToastTextAlpha = 255.0F;

// Active DAT banner — extra vertical padding (px) added to the computed text height.
inline constexpr float k_BannerExtraPadding = 6.0F;

// ROM checklist / detail panel column indices
inline constexpr int k_ColStatus = 0;
inline constexpr int k_ColRomName = 1;
inline constexpr int k_ColSize = 2;
inline constexpr int k_ColSha1 = 3;
inline constexpr int k_ColMd5 = 4;
inline constexpr int k_ColCrc32 = 5;

// Game panel column indices
inline constexpr int k_GameColStatus = 0;
inline constexpr int k_GameColName = 1;

// Status colours
inline constexpr ImVec4 k_ColorVerified{0.2F, 0.9F, 0.3F, 1.0F};   // green
inline constexpr ImVec4 k_ColorMissing{1.0F, 0.3F, 0.3F, 1.0F};    // red
inline constexpr ImVec4 k_ColorUnverified{1.0F, 0.9F, 0.2F, 1.0F}; // yellow
inline constexpr ImVec4 k_ColorMismatch{1.0F, 0.5F, 0.0F, 1.0F};   // orange

// Log panel colour scheme (RGBA)
inline constexpr ImVec4 k_ColorLogWarn{1.0F, 0.75F, 0.1F, 1.0F};   // amber  — warnings
inline constexpr ImVec4 k_ColorLogError{1.0F, 0.3F, 0.3F, 1.0F};   // red    — errors / critical
inline constexpr ImVec4 k_ColorLogDebug{0.6F, 0.6F, 0.6F, 1.0F};   // grey   — debug / trace
inline constexpr ImVec4 k_ColorLogDefault{1.0F, 1.0F, 1.0F, 1.0F}; // white  — info

// Status icon labels — ASCII symbols compatible with ImGui's default font.
// The Unicode checkmarks (✓ U+2713, ✗ U+2717) are not in the built-in ProggyClean
// font and show as '?' on most systems.  Use plain ASCII equivalents instead.
inline constexpr auto* k_IconVerified = "[OK] Verified";
inline constexpr auto* k_IconMissing = "[--] Missing";
inline constexpr auto* k_SymbolMissing = "[--]"; // standalone, for summary badges

/// ASCII-only character case fold: maps [A-Z] → [a-z], all other bytes pass through unchanged.
/// Safe for UTF-8 strings because non-ASCII bytes are always ≥ 0x80 and never in [A-Z].
/// Intended to be used with std::ranges::transform to fold an entire string.
[[nodiscard]] inline auto ascii_lower(char c) noexcept -> char {
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
}

inline void glfw_error_callback(int error, const char* description) {
  std::fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

/// Formats a byte count as a human-readable string (B, KB, MB, GB).
inline auto format_size(std::int64_t bytes) -> std::string {
  constexpr std::int64_t k_Kilo = 1024;
  constexpr std::int64_t k_Mega = 1024 * 1024;
  constexpr std::int64_t k_Giga = 1024 * 1024 * 1024;

  char buf[32];
  if (bytes >= k_Giga) {
    std::snprintf(
        buf, sizeof(buf), "%.1f GB", static_cast<double>(bytes) / static_cast<double>(k_Giga));
  } else if (bytes >= k_Mega) {
    std::snprintf(
        buf, sizeof(buf), "%.1f MB", static_cast<double>(bytes) / static_cast<double>(k_Mega));
  } else if (bytes >= k_Kilo) {
    std::snprintf(
        buf, sizeof(buf), "%.1f KB", static_cast<double>(bytes) / static_cast<double>(k_Kilo));
  } else {
    std::snprintf(buf, sizeof(buf), "%lld B", static_cast<long long>(bytes));
  }
  return buf;
}

inline auto status_label(core::RomStatusType status) -> const char* {
  switch (status) {
    case core::RomStatusType::Verified:
      return k_IconVerified;
    case core::RomStatusType::Missing:
      return k_IconMissing;
    case core::RomStatusType::Unverified:
      return "? Unverified";
    case core::RomStatusType::Mismatch:
      return "! Mismatch";
  }
  return "? Unknown";
}

inline auto status_color(core::RomStatusType status) -> ImVec4 {
  switch (status) {
    case core::RomStatusType::Verified:
      return k_ColorVerified;
    case core::RomStatusType::Missing:
      return k_ColorMissing;
    case core::RomStatusType::Unverified:
      return k_ColorUnverified;
    case core::RomStatusType::Mismatch:
      return k_ColorMismatch;
  }
  return k_ColorMissing;
}

/// Compact single-badge icon for a status — used in the games table Status column.
inline auto status_icon(core::RomStatusType status) -> const char* {
  switch (status) {
    case core::RomStatusType::Verified:
      return "[OK]";
    case core::RomStatusType::Missing:
      return "[--]";
    case core::RomStatusType::Unverified:
      return "[??]";
    case core::RomStatusType::Mismatch:
      return "[!!]";
  }
  return "[??]";
}

inline auto status_sort_order(core::RomStatusType status) -> int {
  switch (status) {
    case core::RomStatusType::Missing:
      return 0;
    case core::RomStatusType::Mismatch:
      return 1;
    case core::RomStatusType::Unverified:
      return 2;
    case core::RomStatusType::Verified:
      return 3;
  }
  return 4;
}

} // namespace romulus::gui
