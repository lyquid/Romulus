#include "romulus/scanner/archive_service.hpp"

#include "romulus/core/logging.hpp"

#include <archive.h>
#include <archive_entry.h>

#include <algorithm>
#include <array>
#include <memory>

namespace romulus::scanner {

namespace {

/// RAII wrapper for libarchive's archive* handle.
struct ArchiveDeleter {
  void operator()(struct archive* a) const {
    if (a != nullptr) {
      archive_read_free(a);
    }
  }
};
using ArchivePtr = std::unique_ptr<struct archive, ArchiveDeleter>;

auto create_archive_reader() -> ArchivePtr {
  auto* a = archive_read_new();
  archive_read_support_format_all(a);
  archive_read_support_filter_all(a);
  return ArchivePtr{a};
}

// Extensions recognized as archives
constexpr std::array k_ArchiveExtensions = {
    ".zip",
    ".7z",
    ".tar",
    ".tar.gz",
    ".tgz",
    ".tar.bz2",
    ".tbz2",
    ".tar.xz",
};

} // namespace

auto ArchiveService::is_archive(const std::filesystem::path& path) -> bool {
  auto ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });

  // Check simple single-component extensions first (.zip, .7z, .tar, .tgz, etc.)
  if (std::ranges::any_of(k_ArchiveExtensions, [&ext](std::string_view ae) { return ext == ae; })) {
    return true;
  }

  // Handle double extensions like .tar.gz / .tar.bz2 / .tar.xz — but only when the
  // stem itself ends with ".tar".  A naive has_extension() check would incorrectly
  // treat version strings like "(v1.1)" as a stem extension, producing ".1).zip".
  auto stem_ext = path.stem().extension().string();
  std::transform(stem_ext.begin(), stem_ext.end(), stem_ext.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });

  if (stem_ext == ".tar") {
    auto combined = stem_ext + ext;
    return std::ranges::any_of(k_ArchiveExtensions,
                               [&combined](std::string_view ae) { return combined == ae; });
  }

  return false;
}

auto ArchiveService::list_entries(const std::filesystem::path& path)
    -> Result<std::vector<core::ArchiveEntry>> {
  auto reader = create_archive_reader();

  if (archive_read_open_filename(reader.get(), path.string().c_str(), 10240) != ARCHIVE_OK) {
    return std::unexpected(core::Error{core::ErrorCode::ArchiveOpenError,
                                       "Cannot open archive '" + path.string() +
                                           "': " + archive_error_string(reader.get())});
  }

  std::vector<core::ArchiveEntry> entries;
  struct archive_entry* entry = nullptr;

  while (archive_read_next_header(reader.get(), &entry) == ARCHIVE_OK) {
    // Skip directories
    if (archive_entry_filetype(entry) != AE_IFREG) {
      archive_read_data_skip(reader.get());
      continue;
    }

    const char* name = archive_entry_pathname(entry);
    entries.push_back({
        .name = name != nullptr ? name : "",
        .size = archive_entry_size(entry),
    });

    archive_read_data_skip(reader.get());
  }

  ROMULUS_DEBUG("Listed {} entries in archive '{}'", entries.size(), path.string());
  return entries;
}

auto ArchiveService::stream_entry(const std::filesystem::path& path,
                                  std::string_view entry_name,
                                  const StreamCallback& callback) -> Result<void> {
  auto reader = create_archive_reader();

  if (archive_read_open_filename(reader.get(), path.string().c_str(), 10240) != ARCHIVE_OK) {
    return std::unexpected(core::Error{core::ErrorCode::ArchiveOpenError,
                                       "Cannot open archive '" + path.string() +
                                           "': " + archive_error_string(reader.get())});
  }

  struct archive_entry* entry = nullptr;
  bool found = false;

  while (archive_read_next_header(reader.get(), &entry) == ARCHIVE_OK) {
    const char* name = archive_entry_pathname(entry);
    if (name == nullptr || entry_name != name) {
      archive_read_data_skip(reader.get());
      continue;
    }

    found = true;

    // Stream data in chunks
    const void* buff = nullptr;
    std::size_t size = 0;
    la_int64_t offset = 0;

    while (archive_read_data_block(reader.get(), &buff, &size, &offset) == ARCHIVE_OK) {
      callback(static_cast<const std::byte*>(buff), size);
    }

    break;
  }

  if (!found) {
    return std::unexpected(core::Error{core::ErrorCode::ArchiveReadError,
                                       "Entry '" + std::string(entry_name) +
                                           "' not found in archive '" + path.string() + "'"});
  }

  return {};
}

} // namespace romulus::scanner
