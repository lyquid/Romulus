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
    auto sys_id = db_->get_or_create_system("Test System");
    romulus::core::DatVersion dat{.system_id = *sys_id,
                                  .name = "Test",
                                  .version = "1.0",
                                  .source_url = {},
                                  .checksum = "abc",
                                  .imported_at = {}};
    auto dat_id = db_->insert_dat_version(dat);

    romulus::core::GameInfo game{.name = "Test Game",
                                 .description = {},
                                 .system_id = *sys_id,
                                 .dat_version_id = *dat_id,
                                 .roms = {}};
    auto game_id = db_->insert_game(game);

    // Insert a ROM
    romulus::core::RomInfo rom{.game_id = *game_id,
                               .name = "test.bin",
                               .size = 100,
                               .crc32 = "aabb0011",
                               .md5 = "md5hash",
                               .sha1 = "sha1hash",
                               .sha256 = {},
                               .region = {}};
    auto rom_id = db_->insert_rom(rom);

    // Insert a matching file (exact match)
    romulus::core::FileInfo file{.filename = "test.bin",
                                 .path = "/roms/test.bin",
                                 .size = 100,
                                 .crc32 = "aabb0011",
                                 .md5 = "md5hash",
                                 .sha1 = "sha1hash",
                                 .sha256 = "sha256hash",
                                 .last_scanned = {}};
    auto file_id = db_->upsert_file(file);

    // Insert a non-matching file
    romulus::core::FileInfo other{.filename = "unknown.bin",
                                  .path = "/roms/unknown.bin",
                                  .size = 200,
                                  .crc32 = "00000000",
                                  .md5 = "nomatch",
                                  .sha1 = "nomatch",
                                  .sha256 = "sha256nomatch",
                                  .last_scanned = {}};
    auto other_id = db_->upsert_file(other);
  }

  std::filesystem::path db_path_;
  std::unique_ptr<romulus::database::Database> db_;
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

TEST_F(MatcherTest, ReportsNoMatchForUnknownFile) {
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

} // namespace
