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
    seed_data();
  }

  void TearDown() override {
    db_.reset();
    std::filesystem::remove(db_path_);
    std::filesystem::remove(db_path_.string() + "-wal");
    std::filesystem::remove(db_path_.string() + "-shm");
  }

  void seed_data() {
    auto sys_id = db_->get_or_create_system("Test System");
    romulus::core::DatVersion dat{.dat_id = {},
                                  .system_id = *sys_id,
                                  .name = "Test",
                                  .version = "1.0",
                                  .source_url = {},
                                  .checksum = "abc",
                                  .imported_at = {}};
    auto dat_id = db_->insert_dat_version(dat);

    romulus::core::GameInfo game{.dat_game_id = {},
                                 .name = "Game A",
                                 .description = {},
                                 .clone_of = {},
                                 .category = {},
                                 .game_id_text = {},
                                 .system_id = *sys_id,
                                 .dat_version_id = *dat_id,
                                 .roms = {}};
    auto game_id = db_->insert_game(game);

    // ROM with a matching file
    romulus::core::RomInfo rom1{.game_id = *game_id,
                                .name = "matched.bin",
                                .size = 100,
                                .crc32 = "11111111",
                                .md5 = "m1",
                                .sha1 = "s1",
                                .sha256 = {},
                                .region = {},
                                .status = {},
                                .serial = {},
                                .header = {}};
    auto rom1_id = db_->insert_rom(rom1);

    // ROM without a matching file
    romulus::core::RomInfo rom2{.game_id = *game_id,
                                .name = "missing.bin",
                                .size = 200,
                                .crc32 = "22222222",
                                .md5 = "m2",
                                .sha1 = "s2",
                                .sha256 = {},
                                .region = {},
                                .status = {},
                                .serial = {},
                                .header = {}};
    auto rom2_id = db_->insert_rom(rom2);

    // File that matches rom1
    romulus::core::FileInfo file{.path = "/roms/matched.bin",
                                 .size = 100,
                                 .crc32 = "11111111",
                                 .md5 = "m1",
                                 .sha1 = "s1",
                                 .last_scanned = {}};
    auto file_id = db_->upsert_file(file);
  }

  std::filesystem::path db_path_;
  std::unique_ptr<romulus::database::Database> db_;
};

TEST_F(ClassifierTest, ClassifiesVerifiedAndMissing) {
  // First match
  auto match_result = romulus::engine::Matcher::match_all(*db_);
  ASSERT_TRUE(match_result.has_value());

  // Then classify
  auto result = romulus::engine::Classifier::classify_all(*db_);
  ASSERT_TRUE(result.has_value());

  // Check summary
  auto summary = db_->get_collection_summary();
  ASSERT_TRUE(summary.has_value());
  EXPECT_EQ(summary->total_roms, 2);
  EXPECT_EQ(summary->verified, 1);
  EXPECT_EQ(summary->missing, 1);
}

} // namespace
