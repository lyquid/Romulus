#include <catch2/catch_test_macros.hpp>
#include <filesystem>

#include "romulus/database/database.hpp"

TEST_CASE("Database initializes successfully", "[database]") {
  const std::filesystem::path db_path = "test_db_init.db";
  std::filesystem::remove(db_path);

  romulus::Database db(db_path);
  auto res = db.initialize();
  REQUIRE(res.has_value());

  std::filesystem::remove(db_path);
}

TEST_CASE("Database insert and query DatVersion", "[database]") {
  const std::filesystem::path db_path = "test_db_datversion.db";
  std::filesystem::remove(db_path);

  romulus::Database db(db_path);
  REQUIRE(db.initialize().has_value());

  romulus::DatVersion dv;
  dv.name = "Nintendo - Game Boy";
  dv.version = "2024-01-01";
  dv.source_url = "https://nointro.org";
  dv.checksum = "abc123";
  dv.imported_at = "2024-01-01T00:00:00Z";

  auto insert_res = db.insert_dat_version(dv);
  REQUIRE(insert_res.has_value());
  REQUIRE(*insert_res == 1);

  auto query_res = db.query_dat_versions();
  REQUIRE(query_res.has_value());
  REQUIRE(query_res->size() == 1);
  REQUIRE(query_res->at(0).name == "Nintendo - Game Boy");

  std::filesystem::remove(db_path);
}

TEST_CASE("Database insert and query Game and Rom", "[database]") {
  const std::filesystem::path db_path = "test_db_game_rom.db";
  std::filesystem::remove(db_path);

  romulus::Database db(db_path);
  REQUIRE(db.initialize().has_value());

  romulus::DatVersion dv;
  dv.name = "SNES";
  dv.version = "1.0";
  dv.source_url = "";
  dv.checksum = "";
  dv.imported_at = "2024-01-01T00:00:00Z";
  auto dv_id = db.insert_dat_version(dv);
  REQUIRE(dv_id.has_value());

  romulus::Game game;
  game.name = "Super Mario World";
  game.system = "SNES";
  game.dat_version_id = *dv_id;
  auto game_id = db.insert_game(game);
  REQUIRE(game_id.has_value());

  romulus::Rom rom;
  rom.game_id = *game_id;
  rom.name = "Super Mario World (USA).sfc";
  rom.crc32 = "b19ed489";
  rom.md5 = "d72e22c9cd3a6f3e5f1e";
  rom.sha1 = "6b47bb75d16514b6a476aa0c73a683a";
  rom.region = "USA";
  auto rom_id = db.insert_rom(rom);
  REQUIRE(rom_id.has_value());

  auto roms = db.query_roms_by_game(*game_id);
  REQUIRE(roms.has_value());
  REQUIRE(roms->size() == 1);
  REQUIRE(roms->at(0).name == "Super Mario World (USA).sfc");

  std::filesystem::remove(db_path);
}

TEST_CASE("Database transactions: commit", "[database]") {
  const std::filesystem::path db_path = "test_db_txn.db";
  std::filesystem::remove(db_path);

  romulus::Database db(db_path);
  REQUIRE(db.initialize().has_value());

  REQUIRE(db.begin_transaction().has_value());
  romulus::DatVersion dv;
  dv.name = "Test";
  dv.version = "1";
  dv.source_url = "";
  dv.checksum = "";
  dv.imported_at = "2024-01-01T00:00:00Z";
  REQUIRE(db.insert_dat_version(dv).has_value());
  REQUIRE(db.commit().has_value());

  auto versions = db.query_dat_versions();
  REQUIRE(versions.has_value());
  REQUIRE(versions->size() == 1);

  std::filesystem::remove(db_path);
}

TEST_CASE("Database transactions: rollback", "[database]") {
  const std::filesystem::path db_path = "test_db_rollback.db";
  std::filesystem::remove(db_path);

  romulus::Database db(db_path);
  REQUIRE(db.initialize().has_value());

  REQUIRE(db.begin_transaction().has_value());
  romulus::DatVersion dv;
  dv.name = "ToRollback";
  dv.version = "1";
  dv.source_url = "";
  dv.checksum = "";
  dv.imported_at = "2024-01-01T00:00:00Z";
  REQUIRE(db.insert_dat_version(dv).has_value());
  REQUIRE(db.rollback().has_value());

  auto versions = db.query_dat_versions();
  REQUIRE(versions.has_value());
  REQUIRE(versions->empty());

  std::filesystem::remove(db_path);
}
