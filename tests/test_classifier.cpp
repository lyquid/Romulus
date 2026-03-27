#include <catch2/catch_test_macros.hpp>
#include <filesystem>

#include "romulus/classifier/classifier.hpp"
#include "romulus/database/database.hpp"

static romulus::Id setup_rom(romulus::Database& db, const std::string& name) {
  romulus::DatVersion dv;
  dv.name = "Sys";
  dv.version = "1";
  dv.source_url = "";
  dv.checksum = "";
  dv.imported_at = "2024-01-01T00:00:00Z";
  auto dv_id = db.insert_dat_version(dv);
  if (!dv_id) throw std::runtime_error("insert dv failed");

  romulus::Game game;
  game.name = name;
  game.system = "Sys";
  game.dat_version_id = *dv_id;
  auto game_id = db.insert_game(game);
  if (!game_id) throw std::runtime_error("insert game failed");

  romulus::Rom rom;
  rom.game_id = *game_id;
  rom.name = name + ".rom";
  rom.crc32 = "aabbccdd";
  rom.md5 = "md5x";
  rom.sha1 = "sha1x";
  rom.region = "";
  auto rom_id = db.insert_rom(rom);
  if (!rom_id) throw std::runtime_error("insert rom failed");
  return *rom_id;
}

TEST_CASE("Classifier marks ROM as Missing when no match exists", "[classifier]") {
  const std::string db_name = "test_classifier_missing.db";
  std::filesystem::remove(db_name);
  romulus::Database db(db_name);
  REQUIRE(db.initialize().has_value());

  const romulus::Id rom_id = setup_rom(db, "GameMissing");

  romulus::Classifier classifier(db);
  auto result = classifier.classify();
  REQUIRE(result.has_value());
  REQUIRE(result->size() == 1);
  REQUIRE(result->at(0).rom_id == rom_id);
  REQUIRE(result->at(0).status == romulus::RomStatus::Missing);

  std::filesystem::remove(db_name);
}

TEST_CASE("Classifier marks ROM as Have when exact match exists", "[classifier]") {
  const std::string db_name = "test_classifier_have.db";
  std::filesystem::remove(db_name);
  romulus::Database db(db_name);
  REQUIRE(db.initialize().has_value());

  const romulus::Id rom_id = setup_rom(db, "GameHave");

  // Insert a scanned file and an exact match
  romulus::ScannedFile sf;
  sf.path = "GameHave.rom";
  sf.size = 100;
  sf.crc32 = "aabbccdd";
  sf.md5 = "md5x";
  sf.sha1 = "sha1x";
  sf.last_scanned = "2024-01-01T00:00:00Z";
  auto file_id = db.upsert_scanned_file(sf);
  REQUIRE(file_id.has_value());

  romulus::FileMatch fm;
  fm.file_id = *file_id;
  fm.rom_id = rom_id;
  fm.match_type = romulus::MatchType::Exact;
  REQUIRE(db.insert_file_match(fm).has_value());

  romulus::Classifier classifier(db);
  auto result = classifier.classify();
  REQUIRE(result.has_value());
  REQUIRE(result->size() == 1);
  REQUIRE(result->at(0).status == romulus::RomStatus::Have);

  std::filesystem::remove(db_name);
}

TEST_CASE("Classifier marks ROM as BadDump for CRC-only match", "[classifier]") {
  const std::string db_name = "test_classifier_baddump.db";
  std::filesystem::remove(db_name);
  romulus::Database db(db_name);
  REQUIRE(db.initialize().has_value());

  const romulus::Id rom_id = setup_rom(db, "GameBad");

  romulus::ScannedFile sf;
  sf.path = "GameBad_bad.rom";
  sf.size = 100;
  sf.crc32 = "aabbccdd";
  sf.md5 = "differentmd5";
  sf.sha1 = "differentsha1";
  sf.last_scanned = "2024-01-01T00:00:00Z";
  auto file_id = db.upsert_scanned_file(sf);
  REQUIRE(file_id.has_value());

  romulus::FileMatch fm;
  fm.file_id = *file_id;
  fm.rom_id = rom_id;
  fm.match_type = romulus::MatchType::CrcOnly;
  REQUIRE(db.insert_file_match(fm).has_value());

  romulus::Classifier classifier(db);
  auto result = classifier.classify();
  REQUIRE(result.has_value());
  REQUIRE(result->size() == 1);
  REQUIRE(result->at(0).status == romulus::RomStatus::BadDump);

  std::filesystem::remove(db_name);
}
