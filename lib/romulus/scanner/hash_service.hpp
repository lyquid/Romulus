#pragma once

/// @file hash_service.hpp
/// @brief Computes CRC32 + MD5 + SHA1 hashes in a single pass.
/// Supports both regular files and archive entries (via streaming).

#include "romulus/core/error.hpp"
#include "romulus/core/types.hpp"

#include <filesystem>

namespace romulus::scanner {

using romulus::core::Result;

/// Computes cryptographic hashes for ROM files.
/// Reads data once and feeds it to CRC32, MD5, and SHA1 simultaneously.
class HashService final {
public:
  /// Computes hashes for a regular file on disk.
  /// @param file_path Absolute path to the file.
  /// @return HashDigest with CRC32, MD5, and SHA1 hex strings.
  [[nodiscard]] static auto compute_hashes(const std::filesystem::path& file_path)
    -> Result<core::HashDigest>;

  /// Computes hashes for a file inside an archive (without extracting to disk).
  /// @param archive_path Path to the archive file.
  /// @param entry_name Name of the entry within the archive.
  /// @return HashDigest with CRC32, MD5, and SHA1 hex strings.
  [[nodiscard]] static auto compute_hashes_archive(
    const std::filesystem::path& archive_path,
    std::string_view entry_name) -> Result<core::HashDigest>;
};

} // namespace romulus::scanner
