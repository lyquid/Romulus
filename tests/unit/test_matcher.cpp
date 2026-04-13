#include "romulus/core/types.hpp"
#include "romulus/database/database.hpp"
#include "romulus/engine/matcher.hpp"

#include <gtest/gtest.h>

#include <filesystem>

namespace {

class MatcherTest : public ::testing::Test {
protected:
  void SetUp() override {
    db_path_ = std::filesystem::temp_directory_path() / "romulus_matcher_test.db";
    std::filesystem::remove(db_path_);
    db_ = std::make_unique<romulus::database::Database>(db_path_);
    seed_data();
  }

  void TearDown() override {
    db_.reset();
    std::filesystem::remove(db_path_);
    std::filesystem::remove(db_path_.string() + "-wal");
    std::filesystem::remove(db_path_.string() + "-shm");
  }

  void seed_data() {
    romulus::core::DatVersion dat{.name = "Test",
                                  .version = "1.0",
                                  .source_url = {}, .checksum = "abc", .imported_at = {}};
    auto dat_id = db_->insert_dat_version(dat);
    ASSERT_TRUE(dat_id.has_value());

    auto game_id1 = db_->find_or_insert_game(*dat_id, "Test Game");
    ASSERT_TRUE(game_id1.has_value());

    // ROM 1: SHA1/MD5/CRC32 known but no SHA256 in DAT — all valid hex
    romulus::core::RomInfo rom{.game_id = *game_id1,
                               .name = "test.bin",
                               .size = 100,
                               .crc32 = "aabb0011",
                               .md5 = "aabb0011aabb0011aabb0011aabb0011",
                               .sha1 = "aabb0011aabb0011aabb0011aabb0011aabb0011",
                               .sha256 = {},
                               .region = {}};
    ASSERT_TRUE(db_->insert_rom(rom).has_value());

    auto game_id2 = db_->find_or_insert_game(*dat_id, "Enriched Game");
    ASSERT_TRUE(game_id2.has_value());

    // ROM 2: has a SHA256 in the DAT (e.g., enriched entry)
    romulus::core::RomInfo rom_enriched{
        .game_id = *game_id2,
        .name = "with_sha256.bin",
        .size = 200,
        .crc32 = "ccdd0022",
        .md5 = "ccdd0022ccdd0022ccdd0022ccdd0022",
        .sha1 = "ccdd0022ccdd0022ccdd0022ccdd0022ccdd0022",
        .sha256 = "ccdd0022ccdd0022ccdd0022ccdd0022ccdd0022ccdd0022ccdd0022ccdd0022",
        .region = {}};
    auto rom_enriched_id = db_->insert_rom(rom_enriched);
    ASSERT_TRUE(rom_enriched_id.has_value());
    rom_enriched_id_ = *rom_enriched_id;

    auto game_id3 = db_->find_or_insert_game(*dat_id, "Missing Game");
    ASSERT_TRUE(game_id3.has_value());

    // ROM 3: missing completely — no matching file will exist
    romulus::core::RomInfo rom_missing{.game_id = *game_id3,
                                       .name = "missing.bin",
                                       .size = 300,
                                       .crc32 = "dead0033",
                                       .md5 = "dead0033dead0033dead0033dead0033",
                                       .sha1 = "dead0033dead0033dead0033dead0033dead0033",
                                       .sha256 = {},
                                       .region = {}};
    ASSERT_TRUE(db_->insert_rom(rom_missing).has_value());

    // File 1: exact match against ROM 1 via SHA1+MD5+CRC32
    romulus::core::FileInfo file{
        .path = "/roms/test.bin",
        .archive_path = std::nullopt,
        .entry_name = std::nullopt,
        .size = 100,
        .crc32 = "aabb0011",
        .md5 = "aabb0011aabb0011aabb0011aabb0011",
        .sha1 = "aabb0011aabb0011aabb0011aabb0011aabb0011",
        .sha256 = "aabb0011aabb0011aabb0011aabb0011aabb0011aabb0011aabb0011aabb0011",
    };
    ASSERT_TRUE(db_->upsert_file(file).has_value());

    // File 2: matches ROM 2 only via SHA256 — lower hashes differ
    romulus::core::FileInfo file_sha256_only{
        .path = "/roms/sha256_only.bin",
        .archive_path = std::nullopt,
        .entry_name = std::nullopt,
        .size = 200,
        .crc32 = "ff110044",
        .md5 = "ff110044ff110044ff110044ff110044",
        .sha1 = "ff110044ff110044ff110044ff110044ff110044",
        .sha256 = "ccdd0022ccdd0022ccdd0022ccdd0022ccdd0022ccdd0022ccdd0022ccdd0022",
    };
    auto sha256_only_id = db_->upsert_file(file_sha256_only);
    ASSERT_TRUE(sha256_only_id.has_value());
    sha256_only_file_id_ = *sha256_only_id;

    // File 3: no match at all
    romulus::core::FileInfo other{
        .path = "/roms/unknown.bin",
        .archive_path = std::nullopt,
        .entry_name = std::nullopt,
        .size = 200,
        .crc32 = "00000000",
        .md5 = "00000000000000000000000000000000",
        .sha1 = "0000000000000000000000000000000000000000",
        .sha256 = "0000000000000000000000000000000000000000000000000000000000000000",
    };
    ASSERT_TRUE(db_->upsert_file(other).has_value());
  }

  std::filesystem::path db_path_;
  std::unique_ptr<romulus::database::Database> db_;
  std::int64_t rom_enriched_id_ = 0;
  std::int64_t sha256_only_file_id_ = 0;
};

TEST_F(MatcherTest, MatchesExactByAllHashes) {
  auto results = romulus::engine::Matcher::match_all(*db_);
  ASSERT_TRUE(results.has_value()) << results.error().message;

  bool found_exact = false;
  for (const auto& r : *results) {
    if (r.match_type == romulus::core::MatchType::Exact) {
      found_exact = true;
    }
  }
  EXPECT_TRUE(found_exact);
}

TEST_F(MatcherTest, ReportsNoMatchForUnknownRom) {
  auto results = romulus::engine::Matcher::match_all(*db_);
  ASSERT_TRUE(results.has_value());

  bool found_no_match = false;
  for (const auto& r : *results) {
    if (r.match_type == romulus::core::MatchType::NoMatch) {
      found_no_match = true;
    }
  }
  EXPECT_TRUE(found_no_match);
}

TEST_F(MatcherTest, MatchesSha256OnlyWhenLowerHashesDiffer) {
  auto results = romulus::engine::Matcher::match_all(*db_);
  ASSERT_TRUE(results.has_value()) << results.error().message;

  // Locate the result for the file whose only match is via SHA256
  const romulus::core::MatchResult* sha256_match = nullptr;
  for (const auto& r : *results) {
    if (r.match_type == romulus::core::MatchType::Sha256Only) {
      sha256_match = &r;
      break;
    }
  }

  ASSERT_NE(sha256_match, nullptr) << "Expected a Sha256Only match result";
  // The matched file should be the sha256_only.bin file and point to rom_enriched
  auto file_info = db_->find_file_by_path("/roms/sha256_only.bin");
  ASSERT_TRUE(file_info.has_value());
  ASSERT_TRUE(file_info->has_value());
  EXPECT_EQ(sha256_match->global_rom_sha1, file_info->value().sha1);
  EXPECT_EQ(sha256_match->rom_id, rom_enriched_id_);
}

} // namespace
