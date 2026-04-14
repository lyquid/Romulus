#pragma once

/// @file dat_fetcher.hpp
/// @brief Local DAT file import and version change detection.
/// v0.1: local files only. HTTP fetching deferred to future version.

#include "romulus/core/error.hpp"
#include "romulus/core/types.hpp"

#include <filesystem>
#include <string>

namespace romulus::database {
class Database;
}

namespace romulus::dat {

using romulus::core::Result;

/// Handles DAT file import and version tracking.
class DatFetcher final {
public:
  /// Validates a local DAT file exists and is readable.
  /// @param path Path to the .dat file on the local filesystem.
  /// @return The validated canonical path on success.
  [[nodiscard]] static auto validate_local(const std::filesystem::path& path)
      -> Result<std::filesystem::path>;

  /// Computes a SHA-256 digest of a DAT file for version tracking.
  /// @param path Path to the DAT file.
  /// @return SHA-256 hex string of the file contents.
  [[nodiscard]] static auto compute_sha256(const std::filesystem::path& path)
      -> Result<std::string>;

  /// Checks if a DAT file has changed compared to the version stored in the database.
  /// @param path Path to the DAT file.
  /// @param dat_name The DAT name (system name from header).
  /// @param db Database to check against.
  /// @return true if the file is new or has changed.
  [[nodiscard]] static auto has_version_changed(const std::filesystem::path& path,
                                                std::string_view dat_name,
                                                database::Database& db) -> Result<bool>;
};

} // namespace romulus::dat
