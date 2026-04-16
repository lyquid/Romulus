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
  /// @param skip_check   Optional predicate: given the virtual path, current filesystem size,
  ///                     and physical file last_write_time (Unix epoch seconds), return true to
  ///                     skip re-hashing (e.g. file is already indexed and unchanged). Called
  ///                     concurrently from multiple threads — the predicate must be thread-safe
  ///                     for concurrent read access.
  ///                     For archive entries the last_write_time is the mtime of the archive.
  /// @param extensions   Optional pre-parsed extension filter (e.g. {".nes", ".gb"}).
  ///                     Each entry must be lowercase and include the leading dot.
  ///                     If empty, scans all known ROM and archive extensions.
  /// @return ScanResult containing per-file hashes and summary statistics.
  [[nodiscard]] static Result<core::ScanResult> scan(
      const std::filesystem::path& directory,
      std::function<bool(std::string_view, std::int64_t, std::int64_t)> skip_check = {},
      std::optional<std::vector<std::string>> extensions = {});

private:
  /// Default ROM file extensions to scan for.
  static std::vector<std::string> get_default_extensions();

  /// Checks if a file extension matches the filter.
  [[nodiscard]] static bool matches_extension(const std::filesystem::path& path,
                                              const std::vector<std::string>& extensions);
};

} // namespace romulus::scanner
