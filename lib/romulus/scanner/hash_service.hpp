#pragma once

/// @file hash_service.hpp
/// @brief Computes CRC32 + MD5 + SHA1 + SHA256 hashes in a single pass.
/// Supports regular files, archive entries, and any custom byte-stream source.

#include "romulus/core/error.hpp"
#include "romulus/core/types.hpp"

#include <filesystem>
#include <functional>

namespace romulus::scanner {

using romulus::core::Result;

/// Callback invoked with each chunk of raw bytes during streaming.
/// Signature is compatible with ArchiveService::StreamCallback.
using DataChunkCallback = std::function<void(const std::byte*, std::size_t)>;

/// Reader that drives the stream — calls back with successive data chunks and
/// returns an error if reading fails.  Usable with any byte source
/// (disk file, archive entry, in-memory buffer, …).
using StreamReader = std::function<Result<void>(const DataChunkCallback&)>;

/// Computes cryptographic hashes for ROM files.
/// Reads data once and feeds it to CRC32, MD5, SHA1, and SHA256 simultaneously.
class HashService final {
public:
  /// Core streaming primitive: consumes bytes via reader and computes all four
  /// hashes in a single pass.  Both compute_hashes and compute_hashes_archive
  /// are thin wrappers around this method.
  /// @param reader Callable that delivers data chunks via DataChunkCallback.
  /// @return HashDigest on success, or Error if reading or OpenSSL fails.
  [[nodiscard]] static auto compute_hashes_stream(const StreamReader& reader)
      -> Result<core::HashDigest>;

  /// Computes hashes for a regular file on disk.
  /// @param file_path Absolute path to the file.
  /// @return HashDigest with CRC32, MD5, SHA1, and SHA256 hex strings.
  [[nodiscard]] static auto compute_hashes(const std::filesystem::path& file_path)
      -> Result<core::HashDigest>;

  /// Computes hashes for a file inside an archive (without extracting to disk).
  /// @param archive_path Path to the archive file.
  /// @param entry_index Zero-based index of the entry within the archive (from
  /// ArchiveEntry::index).
  /// @return HashDigest with CRC32, MD5, SHA1, and SHA256 hex strings.
  [[nodiscard]] static auto compute_hashes_archive(const std::filesystem::path& archive_path,
                                                   std::size_t entry_index)
      -> Result<core::HashDigest>;
};

} // namespace romulus::scanner
