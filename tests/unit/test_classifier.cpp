#include "romulus/core/types.hpp"
#include "romulus/database/database.hpp"
#include "romulus/engine/classifier.hpp"
#include "romulus/engine/matcher.hpp"

#include <gtest/gtest.h>

#include <filesystem>

namespace {

class ClassifierTest : public ::testing::Test {
protected:
  void SetUp() override {
    db_path_ = std::filesystem::temp_directory_path() / "romulus_classifier_test.db";
    std::filesystem::remove(db_path_);
    db_ = std::make_unique<romulus::database::Database>(db_path_);
  }

  void TearDown() override {
    db_.reset();
    std::filesystem::remove(db_path_);
    std::filesystem::remove(db_path_.string() + "-wal");
    std::filesystem::remove(db_path_.string() + "-shm");
  }

  // Creates a system, DAT, game, and returns (system_id, game_id)
  auto create_base() -> std::pair<std::int64_t, std::int64_t> {
    auto sys_id = db_->get_or_create_system("Test System");
    romulus::core::DatVersion dat{.system_id = *sys_id,
                                  .name = "Test",
                                  .version = "1.0",
                                  .source_url = {},
                                  .checksum = "abc",
                                  .imported_at = {}};
    auto dat_id = db_->insert_dat_version(dat);

    romulus::core::GameInfo game{.name = "Game A",
                                 .description = {},
                                 .system_id = *sys_id,
                                 .dat_version_id = *dat_id,
                                 .roms = {}};
    auto game_id = db_->insert_game(game);
    return {*sys_id, *game_id};
  }

  std::filesystem::path db_path_;
  std::unique_ptr<romulus::database::Database> db_;
};

TEST_F(ClassifierTest, ClassifiesVerifiedAndMissing) {
  auto [sys_id, game_id] = create_base();

  // ROM with a matching file — all valid hex
  romulus::core::RomInfo rom1{.game_id = game_id,
                              .name = "matched.bin",
                              .size = 100,
                              .crc32 = "11111111",
                              .md5 = "11111111111111111111111111111111",
                              .sha1 = "1111111111111111111111111111111111111111",
                              .sha256 = {},
                              .region = {}};
  ASSERT_TRUE(db_->insert_rom(rom1).has_value());

  // ROM without a matching file
  romulus::core::RomInfo rom2{.game_id = game_id,
                              .name = "missing.bin",
                              .size = 200,
                              .crc32 = "22222222",
                              .md5 = "22222222222222222222222222222222",
                              .sha1 = "2222222222222222222222222222222222222222",
                              .sha256 = {},
                              .region = {}};
  ASSERT_TRUE(db_->insert_rom(rom2).has_value());

  // File that matches rom1
  romulus::core::FileInfo file{.filename = "matched.bin",
                               .path = "/roms/matched.bin",
                               .size = 100,
                               .crc32 = "11111111",
                               .md5 = "11111111111111111111111111111111",
                               .sha1 = "1111111111111111111111111111111111111111",
                               .sha256 = "1111111111111111111111111111111111111111111111111111111111111111",
                               .last_scanned = {}};
  ASSERT_TRUE(db_->upsert_file(file).has_value());

  auto match_result = romulus::engine::Matcher::match_all(*db_);
  ASSERT_TRUE(match_result.has_value());

  auto result = romulus::engine::Classifier::classify_all(*db_);
  ASSERT_TRUE(result.has_value());

  auto summary = db_->get_collection_summary();
  ASSERT_TRUE(summary.has_value());
  EXPECT_EQ(summary->total_roms, 2);
  EXPECT_EQ(summary->verified, 1);
  EXPECT_EQ(summary->missing, 1);
}

TEST_F(ClassifierTest, ClassifiesUnverifiedWithPartialMatch) {
  auto [sys_id, game_id] = create_base();

  // ROM defined in DAT with specific hashes
  romulus::core::RomInfo rom{.game_id = game_id,
                             .name = "partial.bin",
                             .size = 100,
                             .crc32 = "aabb0011",
                             .md5 = "aabb0011aabb0011aabb0011aabb0011",
                             .sha1 = "aabb0011aabb0011aabb0011aabb0011aabb0011",
                             .sha256 = {},
                             .region = {}};
  ASSERT_TRUE(db_->insert_rom(rom).has_value());

  // File with SAME CRC32 but DIFFERENT SHA1/MD5 — will be a CRC32-only match
  romulus::core::FileInfo file{.filename = "partial.bin",
                               .path = "/roms/partial.bin",
                               .size = 100,
                               .crc32 = "aabb0011",
                               .md5 = "cc000000cc000000cc000000cc000000",
                               .sha1 = "cc000000cc000000cc000000cc000000cc000000",
                               .sha256 = "cc000000cc000000cc000000cc000000cc000000cc000000cc000000cc000000",
                               .last_scanned = {}};
  ASSERT_TRUE(db_->upsert_file(file).has_value());

  auto match_result = romulus::engine::Matcher::match_all(*db_);
  ASSERT_TRUE(match_result.has_value());

  auto result = romulus::engine::Classifier::classify_all(*db_);
  ASSERT_TRUE(result.has_value());

  auto summary = db_->get_collection_summary();
  ASSERT_TRUE(summary.has_value());
  EXPECT_EQ(summary->total_roms, 1);
  // CRC32-only match with matching file on disk → Unverified
  EXPECT_EQ(summary->unverified, 1);
  EXPECT_EQ(summary->verified, 0);
  EXPECT_EQ(summary->missing, 0);
}

TEST_F(ClassifierTest, ClassifiesMismatchWhenFileDeleted) {
  auto [sys_id, game_id] = create_base();

  // ROM in DAT
  romulus::core::RomInfo rom{.game_id = game_id,
                             .name = "gone.bin",
                             .size = 100,
                             .crc32 = "dd000000",
                             .md5 = "dd000000dd000000dd000000dd000000",
                             .sha1 = "dd000000dd000000dd000000dd000000dd000000",
                             .sha256 = {},
                             .region = {}};
  ASSERT_TRUE(db_->insert_rom(rom).has_value());

  // Insert a global_rom that matches but DON'T insert a file with that sha1.
  // This simulates: the ROM was matched once via global_roms, but the file was deleted.
  romulus::core::GlobalRom gr{.sha1 = "dd000000dd000000dd000000dd000000dd000000",
                              .sha256 = {},
                              .md5 = "dd000000dd000000dd000000dd000000",
                              .crc32 = "dd000000",
                              .size = 100};
  ASSERT_TRUE(db_->upsert_global_rom(gr).has_value());

  // Manually insert a match to simulate a prior run
  romulus::core::MatchResult match{.rom_id = 1,
                                   .global_rom_sha1 = "dd000000dd000000dd000000dd000000dd000000",
                                   .match_type = romulus::core::MatchType::Exact};
  ASSERT_TRUE(db_->insert_rom_match(match).has_value());

  auto result = romulus::engine::Classifier::classify_all(*db_);
  ASSERT_TRUE(result.has_value());

  auto summary = db_->get_collection_summary();
  ASSERT_TRUE(summary.has_value());
  EXPECT_EQ(summary->total_roms, 1);
  // has match but no physical file → Mismatch
  EXPECT_EQ(summary->mismatch, 1);
  EXPECT_EQ(summary->verified, 0);
}

} // namespace
