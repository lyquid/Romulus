#pragma once

/// @file gui_logic.hpp
/// @brief Pure GUI utility functions with no ImGui/GLFW dependencies.
/// These functions are extracted from gui_app_shared.hpp so that they can be
/// unit-tested without requiring an ImGui or GLFW context.

#include "romulus/core/types.hpp"

#include <cstdint>
#include <cstdio>
#include <string>

namespace romulus::gui {

// ── Status label / icon string constants ─────────────────────
// Single source of truth for all ROM status display strings.
// gui_app_shared.hpp references these so both headers stay consistent.

inline constexpr const char* k_StatusLabelVerified = "[OK] Verified";
inline constexpr const char* k_StatusLabelMissing = "[--] Missing";
inline constexpr const char* k_StatusLabelCrcMatch = "[~] CRC Match";
inline constexpr const char* k_StatusLabelMd5Match = "[~] MD5 Match";
inline constexpr const char* k_StatusLabelHashConflict = "[!] Hash Conflict";
inline constexpr const char* k_StatusLabelMismatch = "! Mismatch";
inline constexpr const char* k_StatusLabelUnknown = "? Unknown";

inline constexpr const char* k_StatusIconVerified = "[OK]";
inline constexpr const char* k_StatusIconMissing = "[--]";
inline constexpr const char* k_StatusIconCrcMatch = "[~]";
inline constexpr const char* k_StatusIconMd5Match = "[~~]";
inline constexpr const char* k_StatusIconHashConflict = "[!!]";
inline constexpr const char* k_StatusIconMismatch = "[!!]";

/// ASCII-only character case fold: maps [A-Z] → [a-z], all other bytes pass through unchanged.
/// Safe for UTF-8 strings because non-ASCII bytes are always ≥ 0x80 and never in [A-Z].
/// Intended to be used with std::ranges::transform to fold an entire string.
[[nodiscard]] inline char ascii_lower(char c) noexcept {
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
}

/// Formats a byte count as a human-readable string (B, KB, MB, GB).
inline std::string format_size(std::int64_t bytes) {
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

/// Returns a human-readable label string for the given ROM status.
inline const char* status_label(core::RomStatusType status) {
  switch (status) {
    case core::RomStatusType::Verified:
      return k_StatusLabelVerified;
    case core::RomStatusType::Missing:
      return k_StatusLabelMissing;
    case core::RomStatusType::CrcMatch:
      return k_StatusLabelCrcMatch;
    case core::RomStatusType::Md5Match:
      return k_StatusLabelMd5Match;
    case core::RomStatusType::HashConflict:
      return k_StatusLabelHashConflict;
    case core::RomStatusType::Mismatch:
      return k_StatusLabelMismatch;
  }
  return k_StatusLabelUnknown;
}

/// Compact single-badge icon for a status — used in the games table Status column.
inline const char* status_icon(core::RomStatusType status) {
  switch (status) {
    case core::RomStatusType::Verified:
      return k_StatusIconVerified;
    case core::RomStatusType::Missing:
      return k_StatusIconMissing;
    case core::RomStatusType::CrcMatch:
      return k_StatusIconCrcMatch;
    case core::RomStatusType::Md5Match:
      return k_StatusIconMd5Match;
    case core::RomStatusType::HashConflict:
      return k_StatusIconHashConflict;
    case core::RomStatusType::Mismatch:
      return k_StatusIconMismatch;
  }
  return k_StatusIconCrcMatch;
}

/// Returns a numeric sort order for ROM status — lower values appear first in sort.
/// Order: Missing < Mismatch < HashConflict < CrcMatch < Md5Match < Verified.
inline int status_sort_order(core::RomStatusType status) {
  switch (status) {
    case core::RomStatusType::Missing:
      return 0;
    case core::RomStatusType::Mismatch:
      return 1;
    case core::RomStatusType::HashConflict:
      return 2;
    case core::RomStatusType::CrcMatch:
      return 3;
    case core::RomStatusType::Md5Match:
      return 4;
    case core::RomStatusType::Verified:
      return 5;
  }
  return 6;
}

} // namespace romulus::gui
