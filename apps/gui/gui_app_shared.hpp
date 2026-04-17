#pragma once

#include "gui_logic.hpp"
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

// Standalone missing-badge icon (e.g., summary badges in the DAT tab).
// Aliases the single source of truth in gui_logic.hpp.
inline constexpr const char* k_SymbolMissing = k_StatusIconMissing;

inline void glfw_error_callback(int error, const char* description) {
  std::fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

inline ImVec4 status_color(core::RomStatusType status) {
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

} // namespace romulus::gui

