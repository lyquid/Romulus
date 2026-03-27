#include <catch2/catch_test_macros.hpp>
#include <filesystem>

#include "romulus/database/database.hpp"
#include "romulus/report_generator/report_generator.hpp"

TEST_CASE("ReportGenerator generates empty report when no data", "[report_generator]") {
  const std::string db_name = "test_report_empty.db";
  std::filesystem::remove(db_name);
  romulus::Database db(db_name);
  REQUIRE(db.initialize().has_value());

  romulus::ReportGenerator rg(db);
  auto result = rg.generate_text_report();
  REQUIRE(result.has_value());
  REQUIRE(result->find("ROMULUS") != std::string::npos);
  REQUIRE(result->find("No data available") != std::string::npos);

  std::filesystem::remove(db_name);
}

TEST_CASE("ReportGenerator generates report with ROM data", "[report_generator]") {
  const std::string db_name = "test_report_data.db";
  std::filesystem::remove(db_name);
  romulus::Database db(db_name);
  REQUIRE(db.initialize().has_value());

  // Insert minimal data
  romulus::DatVersion dv;
  dv.name = "NES";
  dv.version = "1.0";
  dv.source_url = "";
  dv.checksum = "";
  dv.imported_at = "2024-01-01T00:00:00Z";
  auto dv_id = db.insert_dat_version(dv);
  REQUIRE(dv_id.has_value());

  romulus::Game game;
  game.name = "Super Mario Bros.";
  game.system = "NES";
  game.dat_version_id = *dv_id;
  auto game_id = db.insert_game(game);
  REQUIRE(game_id.has_value());

  romulus::Rom rom;
  rom.game_id = *game_id;
  rom.name = "Super Mario Bros. (World).nes";
  rom.crc32 = "e7a717aa";
  rom.md5 = "abc123";
  rom.sha1 = "def456";
  rom.region = "World";
  auto rom_id = db.insert_rom(rom);
  REQUIRE(rom_id.has_value());

  // Mark ROM as Have
  romulus::RomStatusRecord rs;
  rs.rom_id = *rom_id;
  rs.status = romulus::RomStatus::Have;
  rs.last_updated = "2024-01-01T00:00:00Z";
  REQUIRE(db.upsert_rom_status(rs).has_value());

  romulus::ReportGenerator rg(db);
  auto result = rg.generate_text_report();
  REQUIRE(result.has_value());
  REQUIRE(result->find("ROMULUS") != std::string::npos);
  REQUIRE(result->find("NES") != std::string::npos);

  std::filesystem::remove(db_name);
}
