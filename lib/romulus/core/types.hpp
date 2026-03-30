#pragma once

/// @file types.hpp
/// @brief Shared data structures used across all ROMULUS modules.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace romulus::core
{

// ── Hash Digest ──────────────────────────────────────────────

/// CRC32 + MD5 + SHA1 hash triplet computed from a ROM file.
struct HashDigest
{
    std::string crc32;
    std::string md5;
    std::string sha1;
};

// ── System ───────────────────────────────────────────────────

/// Represents a gaming system/platform (e.g. "Nintendo - Game Boy").
struct SystemInfo
{
    std::int64_t id = 0;
    std::string name;        ///< Full No-Intro name, e.g. "Nintendo - Game Boy"
    std::string short_name;  ///< Short alias, e.g. "GB"
    std::string extensions;  ///< Default scan extensions, e.g. ".gb,.gbc"
};

// ── DAT Version ──────────────────────────────────────────────

/// Metadata about an imported DAT file version.
struct DatVersion
{
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
struct RomInfo
{
    std::int64_t id = 0;
    std::int64_t game_id = 0;
    std::string name;
    std::int64_t size = 0;
    std::string crc32;
    std::string md5;
    std::string sha1;
    std::string region;
};

/// A game entry from a DAT file, containing one or more ROMs.
struct GameInfo
{
    std::int64_t id = 0;
    std::string name;
    std::string description;
    std::int64_t system_id = 0;
    std::int64_t dat_version_id = 0;
    std::vector<RomInfo> roms;
};

// ── DAT File ─────────────────────────────────────────────────

/// Header metadata parsed from a DAT file's <header> element.
struct DatHeader
{
    std::string name;         ///< System name from <name>
    std::string description;  ///< From <description>
    std::string version;      ///< From <version>
    std::string author;       ///< From <author> (optional)
    std::string homepage;     ///< From <homepage> (optional)
    std::string url;          ///< From <url> (optional)
};

/// Complete parsed DAT file — header + all games.
struct DatFile
{
    DatHeader header;
    std::vector<GameInfo> games;
};

// ── Scanned File ─────────────────────────────────────────────

/// A file discovered during filesystem scanning.
struct FileInfo
{
    std::int64_t id = 0;
    std::string path;            ///< Filesystem path (or archive_path::entry_name)
    std::int64_t size = 0;
    std::string crc32;
    std::string md5;
    std::string sha1;
    std::string last_scanned;
    bool is_archive_entry = false;  ///< True if this file is inside an archive
};

// ── Matching ─────────────────────────────────────────────────

/// How a file was matched to a ROM.
enum class MatchType
{
    Exact,     ///< CRC32 + MD5 + SHA1 all match
    Sha1Only,  ///< Only SHA1 matches
    Md5Only,   ///< Only MD5 matches
    Crc32Only, ///< Only CRC32 matches
    SizeOnly,  ///< Only file size matches
    NoMatch,   ///< No match found
};

/// Result of matching a scanned file against the ROM database.
struct MatchResult
{
    std::int64_t file_id = 0;
    std::int64_t rom_id = 0;
    MatchType match_type = MatchType::NoMatch;
};

// ── Classification ───────────────────────────────────────────

/// Status of a ROM in the collection.
enum class RomStatusType
{
    Verified,    ///< Exact match exists in scanned files
    Missing,     ///< No matching file found
    Unverified,  ///< Partial match only
    Mismatch,    ///< File exists but hashes don't fully match
};

/// ROM status record.
struct RomStatusEntry
{
    std::int64_t rom_id = 0;
    RomStatusType status = RomStatusType::Missing;
    std::string last_updated;
};

// ── Archive ──────────────────────────────────────────────────

/// An entry inside an archive file (zip/7z).
struct ArchiveEntry
{
    std::string name;       ///< Entry name within the archive
    std::int64_t size = 0;  ///< Uncompressed size
};

// ── Reports ──────────────────────────────────────────────────

/// Collection summary statistics.
struct CollectionSummary
{
    std::string system_name;
    std::int64_t total_roms = 0;
    std::int64_t verified = 0;
    std::int64_t missing = 0;
    std::int64_t unverified = 0;
    std::int64_t mismatch = 0;

    [[nodiscard]] auto verified_percent() const -> double
    {
        if (total_roms == 0)
        {
            return 0.0;
        }
        return static_cast<double>(verified) / static_cast<double>(total_roms) * 100.0;
    }
};

/// A ROM that is missing from the collection.
struct MissingRom
{
    std::string game_name;
    std::string rom_name;
    std::string system_name;
    std::string sha1;
};

/// A file that matches a ROM already matched by another file.
struct DuplicateFile
{
    std::string file_path;
    std::string rom_name;
    std::string game_name;
};

/// Scan operation summary.
struct ScanReport
{
    std::int64_t files_scanned = 0;
    std::int64_t archives_processed = 0;
    std::int64_t files_hashed = 0;
    std::int64_t files_skipped = 0;  ///< Already scanned, unchanged
    std::int64_t matches_found = 0;
};

/// Report output format.
enum class ReportFormat
{
    Text,
    Csv,
    Json,
};

/// Report type.
enum class ReportType
{
    Summary,
    Missing,
    Duplicates,
    Unverified,
};

} // namespace romulus::core
