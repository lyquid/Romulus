#pragma once

/// @file types.hpp
/// @brief Shared data structures used across all ROMULUS modules.

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
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

// ── DAT Version ──────────────────────────────────────────────

/// Metadata about an imported DAT file version.
struct DatVersion {
  std::int64_t id = 0;
  std::string name;
  std::string version;
  std::string system = {};     ///< Human-readable system description from the DAT <description> tag
  std::string source_url;
  std::string checksum;
  std::string imported_at;
};

// ── Game ─────────────────────────────────────────────────────

/// A game entry in the database — one game contains one or more ROMs.
/// Normalizes the game name so it is stored exactly once per DAT version.
/// During parsing, GameInfo is the transient in-memory form; GameEntry is the persisted form.
struct GameEntry {
  std::int64_t id = 0;
  std::int64_t dat_version_id = 0; ///< FK to dat_versions.id
  std::string name;
};

// ── ROM ──────────────────────────────────────────────────────

/// A single ROM entry from a DAT file.
/// expected_sha1 (stored as `expected_sha1` in the DB) is the authoritative identity
/// hash declared by the DAT, distinct from global_roms.sha1 which is the actual file hash.
///
/// Storage fields (written on INSERT): game_id, name, size, crc32, md5, sha1, sha256, region.
/// Display fields (populated via JOIN on SELECT, not stored in roms): dat_version_id, game_name.
struct RomInfo {
  std::int64_t id = 0;
  std::int64_t game_id = 0;        ///< FK to games.id
  std::string name;
  std::int64_t size = 0;
  std::string crc32;
  std::string md5;
  std::string sha1;   ///< Expected SHA-1 from the DAT (stored as expected_sha1 in DB)
  std::string sha256;
  std::string region;
  // Display-only fields — populated via JOIN on SELECT, never stored directly in roms:
  std::int64_t dat_version_id = 0; ///< From games.dat_version_id
  std::string game_name = {};      ///< From games.name
};

/// A game entry parsed from a DAT file, containing one or more ROMs.
/// Used only during DAT parsing; not stored as a separate table.
struct GameInfo {
  std::string name;
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
  std::string path;                                ///< Virtual path — unique storage key ("archive.zip::entry.rom" or "/bare/path.rom")
  std::optional<std::string> archive_path;         ///< Physical archive path; absent (NULL) for bare files — derive path from \c path field
  std::optional<std::string> entry_name;           ///< Set when this file lives inside an archive; absent for bare files
  std::int64_t size = 0;
  std::string crc32;
  std::string md5;
  std::string sha1; ///< Primary anchor linking to global_roms.sha1
  std::string sha256;
  std::int64_t last_scanned = 0;    ///< Unix epoch seconds (strftime('%s','now'))
  std::int64_t last_write_time = 0; ///< Filesystem mtime at scan time (Unix epoch seconds)

  /// Returns true when this file was extracted from an archive entry.
  [[nodiscard]] auto is_archive_entry() const noexcept -> bool { return entry_name.has_value(); }
};

/// Lightweight fingerprint used for skip-checking during scans.
/// Keyed by virtual path; used to decide whether a file needs re-hashing.
/// A file is skipped only if its current filesystem size and last_write_time
/// both match the stored values — otherwise it is re-hashed even if the path
/// is already in the database.
struct FileFingerprint {
  std::int64_t size = 0;            ///< Stored file / uncompressed-entry size
  std::int64_t last_write_time = 0; ///< Stored mtime of the physical file (Unix epoch seconds)
};

/// Transparent string hasher — enables heterogeneous lookup on std::unordered_map<string, ...>
/// using std::string_view keys without constructing a temporary std::string.
struct StringViewHash {
  using is_transparent = void;
  [[nodiscard]] auto operator()(std::string_view sv) const noexcept -> std::size_t {
    return std::hash<std::string_view>{}(sv);
  }
};

/// Map from virtual file path to its stored fingerprint.
/// Uses a transparent hash + equal so callers can look up by std::string_view without
/// constructing a temporary std::string.
using FingerprintMap =
    std::unordered_map<std::string, FileFingerprint, StringViewHash, std::equal_to<>>;

// ── Matching ─────────────────────────────────────────────────

/// How a file was matched to a ROM.
enum class MatchType {
  Exact,      ///< CRC32 + MD5 + SHA1 all match
  Sha256Only, ///< Only SHA256 matches
  Sha1Only,   ///< Only SHA1 matches
  Md5Only,    ///< Only MD5 matches
  Crc32Only,  ///< Only CRC32 matches
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

// ── Archive ──────────────────────────────────────────────────

/// An entry inside an archive file (zip/7z).
struct ArchiveEntry {
  std::size_t index = 0; ///< Zero-based position in the archive — stable ID for streaming
  std::string name;      ///< Entry name (display only — not a unique key)
  std::int64_t size = 0; ///< Uncompressed size
};

// ── Scanned ROM ──────────────────────────────────────────────

/// Canonical separator between an archive file path and an in-archive entry name.
/// Used in virtual paths (scanner output), error messages, and anywhere the
/// "archive::entry" notation is needed.
/// Example: "/roms/game.zip::game.nes"
inline constexpr std::string_view k_ArchiveEntrySeparator = "::";

/// A single ROM discovered during scanning.
/// One archive file can produce many ScannedROMs — one per entry inside the archive.
///
/// For bare ROM files:   archive_path = file path,    entry_name = std::nullopt
/// For archive entries:  archive_path = archive path, entry_name = filename inside the archive
struct ScannedROM {
  std::filesystem::path archive_path;    ///< Physical file on disk (or the archive containing this entry)
  std::optional<std::string> entry_name; ///< Set when this ROM lives inside an archive; absent for bare files
  std::int64_t size = 0;                 ///< Uncompressed ROM size in bytes
  HashDigest hash_digest;                ///< CRC32, MD5, SHA1, and SHA256 computed in a single pass
  std::int64_t last_write_time = 0;      ///< Mtime of the physical file at scan time (Unix epoch seconds)

  /// Returns true when this ROM was extracted from an archive entry.
  [[nodiscard]] auto is_archive_entry() const noexcept -> bool { return entry_name.has_value(); }

  /// Returns the display filename.
  /// For bare files:      the filename component of archive_path.
  /// For archive entries: the leaf filename component of the in-archive path.
  /// @pre archive_path must be a valid file path when entry_name is absent.
  [[nodiscard]] auto filename() const -> std::string {
    if (entry_name) {
      return std::filesystem::path(*entry_name).filename().string();
    }
    return archive_path.filename().string();
  }

  /// Returns the canonical virtual path used as the unique storage key.
  /// For bare files:      "/path/to/game.rom"
  /// For archive entries: "/path/to/archive.zip::game.rom"
  [[nodiscard]] auto virtual_path() const -> std::string {
    if (entry_name) {
      return archive_path.string() + std::string(k_ArchiveEntrySeparator) + *entry_name;
    }
    return archive_path.string();
  }
};

// ── Reports ──────────────────────────────────────────────────

/// Collection summary statistics.
struct CollectionSummary {
  std::string dat_name; ///< DAT version name (empty if aggregating all DATs)
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
  std::string dat_name; ///< DAT version name (replaces system_name)
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

/// Result of a scan operation: statistics plus the list of discovered (newly hashed) ROMs.
/// The scanner itself does not interact with storage; callers are responsible for persisting
/// the files vector (e.g. via Database::upsert_file).
struct ScanResult {
  ScanReport report;
  std::vector<ScannedROM> files; ///< ROMs discovered and hashed during this scan (excludes skipped)
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

// ── DB Explorer ───────────────────────────────────────────────

/// Metadata for a single column in the DB Explorer schema view.
struct ColumnInfo {
  std::string name;             ///< Column name
  std::string type;             ///< Declared SQLite type (e.g. "INTEGER", "TEXT", "BLOB")
  bool not_null = false;        ///< Has NOT NULL constraint
  bool is_primary_key = false;  ///< Part of the primary key
  int pk_order = 0;             ///< Position within a compound PK (1-based); 0 if not PK
  bool is_unique = false;       ///< Has an explicit UNIQUE index (separate from PK)
  std::string fk_table;         ///< Referenced table (empty if not a foreign key)
  std::string fk_column;        ///< Referenced column (empty if not a foreign key)
};

/// Raw result from querying an arbitrary database table.
/// Used by the read-only DB Explorer in the GUI.
struct TableQueryResult {
  std::vector<ColumnInfo> columns;            ///< Column metadata, in declaration order
  std::vector<std::vector<std::string>> rows; ///< Each row as a vector of display strings
};

} // namespace romulus::core
