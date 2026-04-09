#pragma once

/// @file types.hpp
/// @brief Shared data structures used across all ROMULUS modules.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace romulus::core {

// ── Hash Digest ──────────────────────────────────────────────

namespace detail {

/// Converts a fixed-size byte array to a lowercase hex string.
template <std::size_t N>
[[nodiscard]] inline auto bytes_to_hex(const std::array<std::byte, N>& bytes) -> std::string {
  static constexpr std::string_view k_Hex = "0123456789abcdef";
  std::string result;
  result.reserve(N * 2);
  for (const auto b : bytes) {
    const auto v = std::to_integer<std::uint8_t>(b);
    result += k_Hex[v >> 4U];
    result += k_Hex[v & 0xFU];
  }
  return result;
}

} // namespace detail

/// CRC32 + MD5 + SHA1 + SHA256 hash set computed from a ROM file.
/// Fields store raw bytes for efficient comparison and compact storage.
/// Use the to_hex_*() accessors to obtain display-ready lowercase hex strings.
struct HashDigest {
  std::array<std::byte, 4> crc32;   ///< 4-byte CRC32 (big-endian)
  std::array<std::byte, 16> md5;    ///< 16-byte MD5 digest
  std::array<std::byte, 20> sha1;   ///< 20-byte SHA-1 digest
  std::array<std::byte, 32> sha256; ///< 32-byte SHA-256 digest

  /// Returns the CRC32 as a zero-padded 8-character lowercase hex string.
  [[nodiscard]] auto to_hex_crc32() const -> std::string {
    return detail::bytes_to_hex(crc32);
  }

  /// Returns the MD5 as a 32-character lowercase hex string.
  [[nodiscard]] auto to_hex_md5() const -> std::string {
    return detail::bytes_to_hex(md5);
  }

  /// Returns the SHA-1 as a 40-character lowercase hex string.
  [[nodiscard]] auto to_hex_sha1() const -> std::string {
    return detail::bytes_to_hex(sha1);
  }

  /// Returns the SHA-256 as a 64-character lowercase hex string.
  [[nodiscard]] auto to_hex_sha256() const -> std::string {
    return detail::bytes_to_hex(sha256);
  }
};

// ── System ───────────────────────────────────────────────────

/// Represents a gaming system/platform (e.g. "Nintendo - Game Boy").
struct SystemInfo {
  std::int64_t id = 0;
  std::string name;       ///< Full No-Intro name, e.g. "Nintendo - Game Boy"
  std::string short_name; ///< Short alias, e.g. "GB"
  std::string extensions; ///< Default scan extensions, e.g. ".gb,.gbc"
};

// ── DAT Version ──────────────────────────────────────────────

/// Metadata about an imported DAT file version.
struct DatVersion {
  std::int64_t id = 0;
  std::int64_t system_id = 0;
  std::string name;
  std::string version;
  std::string source_url;
  std::string checksum;
  std::string imported_at;
};

// ── ROM & Game ───────────────────────────────────────────────

/// A single ROM entry from a DAT file.
struct RomInfo {
  std::int64_t id = 0;
  std::int64_t game_id = 0;
  std::string name;
  std::int64_t size = 0;
  std::string crc32;
  std::string md5;
  std::string sha1;
  std::string sha256;
  std::string region;
};

/// A game entry from a DAT file, containing one or more ROMs.
struct GameInfo {
  std::int64_t id = 0;
  std::string name;
  std::string description;
  std::int64_t system_id = 0;
  std::int64_t dat_version_id = 0;
  std::vector<RomInfo> roms;
};

// ── DAT File ─────────────────────────────────────────────────

/// Header metadata parsed from a DAT file's <header> element.
struct DatHeader {
  std::string name;        ///< System name from <name>
  std::string description; ///< From <description>
  std::string version;     ///< From <version>
  std::string author;      ///< From <author> (optional)
  std::string homepage;    ///< From <homepage> (optional)
  std::string url;         ///< From <url> (optional)
};

/// Complete parsed DAT file — header + all games.
struct DatFile {
  DatHeader header;
  std::vector<GameInfo> games;
};

// ── Global ROM ───────────────────────────────────────────────

/// A deduplicated global hash index representing unique files.
struct GlobalRom {
  std::string sha1; // 40-char hex string, stored as BLOB(20) in DB
  std::string sha256;
  std::string md5;
  std::string crc32;
  std::int64_t size = 0;
};

// ── Scanned File ─────────────────────────────────────────────

/// A file discovered during filesystem scanning.
struct FileInfo {
  std::int64_t id = 0;
  std::string filename; ///< Filename component (e.g. "game.rom")
  std::string path;     ///< Full filesystem path (or archive_path::entry_name)
  std::int64_t size = 0;
  std::string crc32;
  std::string md5;
  std::string sha1; ///< Primary anchor linking to global_roms.sha1
  std::string sha256;
  std::string last_scanned;
  bool is_archive_entry = false; ///< True if this file is inside an archive
};

// ── Matching ─────────────────────────────────────────────────

/// How a file was matched to a ROM.
enum class MatchType {
  Exact,      ///< CRC32 + MD5 + SHA1 all match
  Sha256Only, ///< Only SHA256 matches
  Sha1Only,   ///< Only SHA1 matches
  Md5Only,    ///< Only MD5 matches
  Crc32Only,  ///< Only CRC32 matches
  SizeOnly,   ///< Only file size matches
  NoMatch,    ///< No match found
};

/// Result of matching a scanned file against the ROM database.
struct MatchResult {
  std::int64_t rom_id = 0;
  std::string global_rom_sha1;
  MatchType match_type = MatchType::NoMatch;
};

// ── Classification ───────────────────────────────────────────

/// Status of a ROM in the collection.
enum class RomStatusType {
  Verified,   ///< Exact match exists in scanned files
  Missing,    ///< No matching file found
  Unverified, ///< Partial match only
  Mismatch,   ///< File exists but hashes don't fully match
};

/// ROM status record.
struct RomStatusEntry {
  std::int64_t rom_id = 0;
  RomStatusType status = RomStatusType::Missing;
  std::string last_updated;
};

// ── Archive ──────────────────────────────────────────────────

/// An entry inside an archive file (zip/7z).
struct ArchiveEntry {
  std::size_t index = 0; ///< Zero-based position in the archive — stable ID for streaming
  std::string name;      ///< Entry name (display only — not a unique key)
  std::int64_t size = 0; ///< Uncompressed size
};

// ── Reports ──────────────────────────────────────────────────

/// Collection summary statistics.
struct CollectionSummary {
  std::string system_name;
  std::int64_t total_roms = 0;
  std::int64_t verified = 0;
  std::int64_t missing = 0;
  std::int64_t unverified = 0;
  std::int64_t mismatch = 0;

  [[nodiscard]] auto verified_percent() const -> double {
    if (total_roms == 0) {
      return 0.0;
    }
    return static_cast<double>(verified) / static_cast<double>(total_roms) * 100.0;
  }
};

/// A ROM that is missing from the collection.
struct MissingRom {
  std::string game_name;
  std::string rom_name;
  std::string system_name;
  std::string sha1;
};

/// A file that matches a ROM already matched by another file.
struct DuplicateFile {
  std::string file_path;
  std::string rom_name;
  std::string game_name;
};

/// Scan operation summary.
struct ScanReport {
  std::int64_t files_scanned = 0;
  std::int64_t archives_processed = 0;
  std::int64_t files_hashed = 0;
  std::int64_t files_skipped = 0; ///< Already scanned, unchanged
  std::int64_t matches_found = 0;
};

/// Result of a scan operation: statistics plus the list of discovered (newly hashed) files.
/// The scanner itself does not interact with storage; callers are responsible for persisting
/// the files vector (e.g. via Database::upsert_file).
struct ScanResult {
  ScanReport report;
  std::vector<FileInfo> files; ///< Files discovered and hashed during this scan (excludes skipped)
};

// ── Scanned Directory ─────────────────────────────────────────

/// A directory registered for ROM scanning.
struct ScannedDirectory {
  std::int64_t id = 0;
  std::string path;     ///< Absolute filesystem path
  std::string added_at; ///< ISO-8601 timestamp of first registration
};

/// Report output format.
enum class ReportFormat {
  Text,
  Csv,
  Json,
};

/// Report type.
enum class ReportType {
  Summary,
  Missing,
  Duplicates,
  Unverified,
};

} // namespace romulus::core
