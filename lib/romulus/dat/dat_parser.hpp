#pragma once

/// @file dat_parser.hpp
/// @brief Parses No-Intro LogiqX XML DAT files into structured data.

#include "romulus/core/error.hpp"
#include "romulus/core/types.hpp"

#include <filesystem>

namespace romulus::dat {

using romulus::core::Result;

/// Parses LogiqX-format XML DAT files used by No-Intro.
/// Supports plain `.dat` / `.xml` files and archives containing a single DAT entry.
/// Extracts system info, game entries, and ROM metadata with hashes.
class DatParser final {
public:
  /// Parses a DAT file and returns the complete structured representation.
  /// @param dat_path Path to the DAT artifact on disk.
  /// @return Parsed DatFile containing header + all games/ROMs.
  [[nodiscard]] Result<core::DatFile> parse(const std::filesystem::path& dat_path);

private:
  /// Extracts header metadata from the <header> XML element.
  [[nodiscard]] static Result<core::DatHeader> parse_header(const void* node);

  /// Extracts a single game and its ROMs from a <game> XML element.
  [[nodiscard]] static Result<core::GameInfo> parse_game(const void* node);

  /// Normalizes hash strings: trims whitespace, converts to lowercase.
  [[nodiscard]] static std::string normalize_hash(std::string_view hash);
};

} // namespace romulus::dat
