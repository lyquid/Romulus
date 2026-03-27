#include <catch2/catch_test_macros.hpp>
#include <filesystem>

#include "romulus/database/database.hpp"
#include "romulus/matcher/matcher.hpp"

static romulus::Database make_db(const std::string& name) {
  std::filesystem::remove(name);
  romulus::Database db(name);
  auto res = db.initialize();
  if (!res) throw std::runtime_error("DB init failed");
  return db;
}

TEST_CASE("Matcher finds exact match by sha1+name", "[matcher]") {
  auto db = make_db("test_matcher_exact.db");

  romulus::DatVersion dv;
  dv.name = "System";
  dv.version = "1";
  dv.source_url = "";
  dv.checksum = "";
  dv.imported_at = "2024-01-01T00:00:00Z";
  auto dv_id = db.insert_dat_version(dv);
  REQUIRE(dv_id.has_value());

  romulus::Game game;
  game.name = "Game A";
  game.system = "System";
  game.dat_version_id = *dv_id;
  auto game_id = db.insert_game(game);
  REQUIRE(game_id.has_value());

  romulus::Rom rom;
  rom.game_id = *game_id;
  rom.name = "game_a.rom";
  rom.crc32 = "aabbccdd";
  rom.md5 = "md5abc";
  rom.sha1 = "sha1abc";
  rom.region = "USA";
  auto rom_id = db.insert_rom(rom);
  REQUIRE(rom_id.has_value());

  romulus::ScannedFile sf;
  sf.id = 1;
  sf.path = "game_a.rom";
  sf.size = 100;
  sf.crc32 = "aabbccdd";
  sf.md5 = "md5abc";
  sf.sha1 = "sha1abc";
  sf.last_scanned = "2024-01-01T00:00:00Z";

  romulus::Matcher matcher(db);
  auto result = matcher.match({sf});
  REQUIRE(result.has_value());
  REQUIRE(result->size() == 1);
  REQUIRE(result->at(0).match_type == romulus::MatchType::Exact);
  REQUIRE(result->at(0).rom_id == *rom_id);

  std::filesystem::remove("test_matcher_exact.db");
}

TEST_CASE("Matcher finds CRC-only match", "[matcher]") {
  auto db = make_db("test_matcher_crc.db");

  romulus::DatVersion dv;
  dv.name = "S";
  dv.version = "1";
  dv.source_url = "";
  dv.checksum = "";
  dv.imported_at = "2024-01-01T00:00:00Z";
  auto dv_id = db.insert_dat_version(dv);
  REQUIRE(dv_id.has_value());

  romulus::Game game;
  game.name = "Game B";
  game.system = "S";
  game.dat_version_id = *dv_id;
  auto game_id = db.insert_game(game);
  REQUIRE(game_id.has_value());

  romulus::Rom rom;
  rom.game_id = *game_id;
  rom.name = "game_b.rom";
  rom.crc32 = "11223344";
  rom.md5 = "md5b";
  rom.sha1 = "sha1b";
  rom.region = "";
  auto rom_id = db.insert_rom(rom);
  REQUIRE(rom_id.has_value());

  romulus::ScannedFile sf;
  sf.id = 1;
  sf.path = "game_b_renamed.rom";
  sf.size = 50;
  sf.crc32 = "11223344";
  sf.md5 = "differentmd5";
  sf.sha1 = "differentsha1";
  sf.last_scanned = "2024-01-01T00:00:00Z";

  romulus::Matcher matcher(db);
  auto result = matcher.match({sf});
  REQUIRE(result.has_value());
  REQUIRE(result->size() == 1);
  REQUIRE(result->at(0).match_type == romulus::MatchType::CrcOnly);

  std::filesystem::remove("test_matcher_crc.db");
}

TEST_CASE("Matcher returns no match for unknown file", "[matcher]") {
  auto db = make_db("test_matcher_nomatch.db");

  romulus::ScannedFile sf;
  sf.id = 1;
  sf.path = "unknown.rom";
  sf.size = 100;
  sf.crc32 = "ffffffff";
  sf.md5 = "ffffffff";
  sf.sha1 = "ffffffff";
  sf.last_scanned = "2024-01-01T00:00:00Z";

  romulus::Matcher matcher(db);
  auto result = matcher.match({sf});
  REQUIRE(result.has_value());
  REQUIRE(result->empty());

  std::filesystem::remove("test_matcher_nomatch.db");
}
