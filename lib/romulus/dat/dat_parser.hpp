#pragma once

/// @file dat_parser.hpp
/// @brief Parses No-Intro LogiqX XML DAT files into structured data.

#include "romulus/core/error.hpp"
#include "romulus/core/types.hpp"

#include <filesystem>

namespace romulus::dat {

using romulus::core::Result;

/// Parses LogiqX-format XML DAT files used by No-Intro.
/// Extracts system info, game entries, and ROM metadata with hashes.
class DatParser final {
public:
  /// Parses a DAT file and returns the complete structured representation.
  /// @param dat_path Path to the .dat XML file.
  /// @return Parsed DatFile containing header + all games/ROMs.
  [[nodiscard]] auto parse(const std::filesystem::path& dat_path) -> Result<core::DatFile>;

private:
  /// Extracts header metadata from the <header> XML element.
  [[nodiscard]] static auto parse_header(const void* node) -> Result<core::DatHeader>;

  /// Extracts a single game and its ROMs from a <game> XML element.
  [[nodiscard]] static auto parse_game(const void* node) -> Result<core::GameInfo>;

  /// Normalizes hash strings: trims whitespace, converts to lowercase.
  [[nodiscard]] static auto normalize_hash(std::string_view hash) -> std::string;
};

} // namespace romulus::dat
