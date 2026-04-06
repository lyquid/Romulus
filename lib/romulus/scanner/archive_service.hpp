#pragma once

/// @file archive_service.hpp
/// @brief In-memory archive reading for zip/7z/tar via libarchive.
/// Never extracts to disk — all reading is streamed.

#include "romulus/core/error.hpp"
#include "romulus/core/types.hpp"

#include <filesystem>
#include <functional>
#include <span>
#include <vector>

namespace romulus::scanner {

using romulus::core::Result;

/// Callback type for streaming archive entry data in chunks.
/// @param data Pointer to the chunk data.
/// @param size Number of bytes in the chunk.
using StreamCallback = std::function<void(const std::byte* data, std::size_t size)>;

/// In-memory archive reader supporting zip, 7z, and tar formats.
/// Uses libarchive under the hood. Never writes to disk.
class ArchiveService final {
public:
  /// Checks if a file is a supported archive format based on extension.
  [[nodiscard]] static auto is_archive(const std::filesystem::path& path) -> bool;

  /// Lists all entries (files) inside an archive.
  /// @param path Path to the archive file.
  /// @return Vector of archive entries with name and uncompressed size.
  [[nodiscard]] static auto list_entries(const std::filesystem::path& path)
      -> Result<std::vector<core::ArchiveEntry>>;

  /// Streams a specific entry's data in chunks to the callback.
  /// Used by HashService to hash archive contents without extraction.
  /// @param path Path to the archive file.
  /// @param entry_index Zero-based index of the entry within the archive (from
  /// ArchiveEntry::index).
  /// @param callback Called with each chunk of data.
  [[nodiscard]] static auto stream_entry(const std::filesystem::path& path,
                                         std::size_t entry_index,
                                         const StreamCallback& callback) -> Result<void>;
};

} // namespace romulus::scanner
