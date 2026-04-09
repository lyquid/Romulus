#pragma once

/// @file rom_scanner.hpp
/// @brief Filesystem scanner that discovers ROM files and archive contents.

#include "romulus/core/error.hpp"
#include "romulus/core/types.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace romulus::scanner {

using romulus::core::Result;

/// Scans directories for ROM files (including inside archives).
/// Supports parallel hashing via std::jthread.
/// The scanner is pure discovery — it does not interact with any storage layer.
class RomScanner final {
public:
  /// Scans a directory recursively for ROM files.
  /// @param directory    Root directory to scan.
  /// @param skip_check   Optional predicate: return true for paths that should be skipped
  ///                     (e.g. already stored in a database). Called concurrently from multiple
  ///                     threads — the predicate must support concurrent read access.
  /// @param extensions   Optional comma-separated extension filter (e.g. ".nes,.gb").
  ///                     If empty, scans all known ROM and archive extensions.
  /// @return ScanResult containing per-file hashes and summary statistics.
  [[nodiscard]] static auto scan(const std::filesystem::path& directory,
                                 std::function<bool(std::string_view)> skip_check = {},
                                 std::optional<std::string> extensions = {})
      -> Result<core::ScanResult>;

private:
  /// Default ROM file extensions to scan for.
  static auto get_default_extensions() -> std::vector<std::string>;

  /// Checks if a file extension matches the filter.
  [[nodiscard]] static auto matches_extension(const std::filesystem::path& path,
                                              const std::vector<std::string>& extensions) -> bool;
};

} // namespace romulus::scanner
