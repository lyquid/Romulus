#pragma once
#include <cstdint>
#include <filesystem>
#include <string>

namespace romulus {

using Id = std::int64_t;

struct DatVersion {
  Id id{};
  std::string name;
  std::string version;
  std::string source_url;
  std::string checksum;
  std::string imported_at;
};

struct Game {
  Id id{};
  std::string name;
  std::string system;
  Id dat_version_id{};
};

struct Rom {
  Id id{};
  Id game_id{};
  std::string name;
  std::string crc32;
  std::string md5;
  std::string sha1;
  std::string region;
};

struct ScannedFile {
  Id id{};
  std::filesystem::path path;
  std::int64_t size{};
  std::string crc32;
  std::string md5;
  std::string sha1;
  std::string last_scanned;
};

enum class MatchType { Exact, CrcOnly, Renamed };

struct FileMatch {
  Id file_id{};
  Id rom_id{};
  MatchType match_type{MatchType::Exact};
};

enum class RomStatus { Have, Missing, Duplicate, BadDump };

struct RomStatusRecord {
  Id rom_id{};
  RomStatus status{RomStatus::Missing};
  std::string last_updated;
};

enum class Error {
  // Database errors
  DatabaseOpenFailed,
  DatabaseSchemaMigrationFailed,
  DatabaseQueryFailed,
  DatabaseTransactionFailed,
  DatabaseNotFound,
  // Parse errors
  ParseInvalidXml,
  ParseMissingElement,
  ParseInvalidFormat,
  // Hash errors
  HashFileNotFound,
  HashReadError,
  HashComputationFailed,
  // Scan errors
  ScanDirectoryNotFound,
  ScanAccessDenied,
  ScanUnexpectedError,
  // Fetch errors
  FetchFileNotFound,
  FetchReadError,
};

}  // namespace romulus
