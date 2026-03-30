#pragma once

/// @file rom_scanner.hpp
/// @brief Filesystem scanner that discovers ROM files and archive contents.

#include "romulus/core/error.hpp"
#include "romulus/core/types.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace romulus::database {
class Database;
}

namespace romulus::scanner {

using romulus::core::Result;

/// Scans directories for ROM files (including inside archives).
/// Supports parallel hashing via std::jthread.
class RomScanner final {
public:
  /// Scans a directory recursively for ROM files.
  /// @param directory Root directory to scan.
  /// @param db Database for skip-checking (already scanned files).
  /// @param extensions Optional comma-separated extension filter (e.g. ".nes,.gb").
  ///                   If empty, scans all known ROM and archive extensions.
  /// @return ScanReport with statistics.
  [[nodiscard]] static auto scan(const std::filesystem::path& directory,
                                 database::Database& db,
                                 std::optional<std::string> extensions = {})
      -> Result<core::ScanReport>;

private:
  /// Default ROM file extensions to scan for.
  static auto get_default_extensions() -> std::vector<std::string>;

  /// Checks if a file extension matches the filter.
  [[nodiscard]] static auto matches_extension(const std::filesystem::path& path,
                                              const std::vector<std::string>& extensions) -> bool;
};

} // namespace romulus::scanner
