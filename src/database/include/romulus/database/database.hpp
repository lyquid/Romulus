#pragma once
#include <expected>
#include <filesystem>
#include <string>
#include <vector>

#include "romulus/common.hpp"

struct sqlite3;
struct sqlite3_stmt;

namespace romulus {

class Database {
 public:
  explicit Database(std::filesystem::path db_path);
  ~Database();

  Database(const Database&) = delete;
  Database& operator=(const Database&) = delete;
  Database(Database&&) noexcept;
  Database& operator=(Database&&) noexcept;

  [[nodiscard]] std::expected<void, Error> initialize();

  // Transaction control
  [[nodiscard]] std::expected<void, Error> begin_transaction();
  [[nodiscard]] std::expected<void, Error> commit();
  [[nodiscard]] std::expected<void, Error> rollback();

  // DatVersion operations
  [[nodiscard]] std::expected<Id, Error> insert_dat_version(const DatVersion& dv);
  [[nodiscard]] std::expected<std::vector<DatVersion>, Error> query_dat_versions();

  // Game operations
  [[nodiscard]] std::expected<Id, Error> insert_game(const Game& game);
  [[nodiscard]] std::expected<std::vector<Game>, Error> query_games_by_dat_version(
      Id dat_version_id);
  [[nodiscard]] std::expected<std::vector<Game>, Error> query_all_games();

  // Rom operations
  [[nodiscard]] std::expected<Id, Error> insert_rom(const Rom& rom);
  [[nodiscard]] std::expected<std::vector<Rom>, Error> query_roms_by_game(Id game_id);
  [[nodiscard]] std::expected<std::vector<Rom>, Error> query_all_roms();

  // ScannedFile operations
  [[nodiscard]] std::expected<Id, Error> upsert_scanned_file(const ScannedFile& sf);
  [[nodiscard]] std::expected<std::vector<ScannedFile>, Error> query_scanned_files();

  // FileMatch operations
  [[nodiscard]] std::expected<void, Error> insert_file_match(const FileMatch& fm);
  [[nodiscard]] std::expected<std::vector<FileMatch>, Error> query_file_matches();

  // RomStatus operations
  [[nodiscard]] std::expected<void, Error> upsert_rom_status(const RomStatusRecord& rs);
  [[nodiscard]] std::expected<std::vector<RomStatusRecord>, Error> query_rom_statuses();

 private:
  std::filesystem::path db_path_;
  sqlite3* db_{nullptr};

  [[nodiscard]] std::expected<void, Error> exec_sql(const char* sql);
};

}  // namespace romulus
