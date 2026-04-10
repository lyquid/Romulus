#include "romulus/database/database.hpp"

#include <gtest/gtest.h>

#include <filesystem>

namespace {

class DatabaseTest : public ::testing::Test {
protected:
  void SetUp() override {
    db_path_ = std::filesystem::temp_directory_path() / "romulus_test.db";
    std::filesystem::remove(db_path_);
    db_ = std::make_unique<romulus::database::Database>(db_path_);
  }

  void TearDown() override {
    db_.reset();
    std::filesystem::remove(db_path_);
    std::filesystem::remove(db_path_.string() + "-wal");
    std::filesystem::remove(db_path_.string() + "-shm");
  }

  std::filesystem::path db_path_;
  std::unique_ptr<romulus::database::Database> db_;
};

TEST_F(DatabaseTest, OpensAndCreatesSchema) {
  SUCCEED();
}

TEST_F(DatabaseTest, InsertAndFindDatVersion) {
  romulus::core::DatVersion dat{
      .name = "Nintendo - Game Boy",
      .version = "2024-01-01",
      .source_url = "test.dat",
      .checksum = "abc123",
      .imported_at = {},
  };

  auto id = db_->insert_dat_version(dat);
  ASSERT_TRUE(id.has_value()) << id.error().message;
  EXPECT_GT(*id, 0);

  auto found = db_->find_dat_version("Nintendo - Game Boy", "2024-01-01");
  ASSERT_TRUE(found.has_value());
  ASSERT_TRUE(found->has_value());
  EXPECT_EQ(found->value().name, "Nintendo - Game Boy");
  EXPECT_EQ(found->value().version, "2024-01-01");

  auto by_checksum = db_->find_dat_version_by_checksum("abc123");
  ASSERT_TRUE(by_checksum.has_value());
  ASSERT_TRUE(by_checksum->has_value());
  EXPECT_EQ(by_checksum->value().name, "Nintendo - Game Boy");
}

TEST_F(DatabaseTest, DatVersionUniqueByChecksum) {
  romulus::core::DatVersion dat{
      .name = "Test",
      .version = "1.0",
      .source_url = {},
      .checksum = "same_checksum",
      .imported_at = {},
  };
  auto id1 = db_->insert_dat_version(dat);
  ASSERT_TRUE(id1.has_value());
  // Inserting the same checksum again should throw (UNIQUE constraint)
  EXPECT_THROW(db_->insert_dat_version(dat), std::runtime_error);
}

TEST_F(DatabaseTest, InsertAndRetrieveRom) {
  romulus::core::DatVersion dat{
      .name = "Test System",
      .version = "1.0",
      .source_url = {},
      .checksum = "abc123",
      .imported_at = {},
  };
  auto dat_id = db_->insert_dat_version(dat);
  ASSERT_TRUE(dat_id.has_value());

  romulus::core::RomInfo rom{
      .id = 0,
      .dat_version_id = *dat_id,
      .game_name = "Test Game",
      .name = "test.bin",
      .size = 1024,
      .crc32 = "deadbeef",
      .md5 = "d41d8cd98f00b204e9800998ecf8427e",
      .sha1 = "da39a3ee5e6b4b0d3255bfef95601890afd80709",
      .sha256 = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
      .region = "USA",
  };
  auto rom_id = db_->insert_rom(rom);
  ASSERT_TRUE(rom_id.has_value());

  auto found = db_->find_rom_by_sha1("da39a3ee5e6b4b0d3255bfef95601890afd80709");
  ASSERT_TRUE(found.has_value());
  ASSERT_TRUE(found->has_value());
  EXPECT_EQ(found->value().name, "test.bin");
  EXPECT_EQ(found->value().size, 1024);
  EXPECT_EQ(found->value().game_name, "Test Game");
  EXPECT_EQ(found->value().dat_version_id, *dat_id);

  auto found_by_sha256 =
      db_->find_rom_by_sha256("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  ASSERT_TRUE(found_by_sha256.has_value());
  ASSERT_TRUE(found_by_sha256->has_value());
  EXPECT_EQ(found_by_sha256->value().name, "test.bin");
  EXPECT_EQ(found_by_sha256->value().sha256,
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST_F(DatabaseTest, UpsertFileUpdatesExisting) {
  romulus::core::FileInfo file{
      .id = 0,
      .filename = "test.bin",
      .path = "/roms/test.bin",
      .size = 1024,
      .crc32 = "aabbccdd",
      .md5 = "12345678901234567890123456789012",
      .sha1 = "1234567890123456789012345678901234567890",
      .sha256 = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
      .last_scanned = {},
      .is_archive_entry = false,
  };

  auto id1 = db_->upsert_file(file);
  ASSERT_TRUE(id1.has_value());

  file.crc32 = "deadbeef";
  auto id2 = db_->upsert_file(file);
  ASSERT_TRUE(id2.has_value());
  EXPECT_EQ(*id1, *id2);

  auto found = db_->find_file_by_path("/roms/test.bin");
  ASSERT_TRUE(found.has_value());
  ASSERT_TRUE(found->has_value());
  EXPECT_EQ(found->value().crc32, "deadbeef");
}

TEST_F(DatabaseTest, TransactionRollsBackOnScopeExit) {
  romulus::core::DatVersion dat{
      .name = "Rollback Test",
      .version = "1.0",
      .source_url = {},
      .checksum = "rb1",
      .imported_at = {},
  };
  {
    auto txn = db_->begin_transaction();
    auto id = db_->insert_dat_version(dat);
    (void)id; // intentionally not committed
    // txn goes out of scope without commit -> rollback
  }

  auto found = db_->find_dat_version("Rollback Test", "1.0");
  ASSERT_TRUE(found.has_value());
  EXPECT_FALSE(found->has_value()); // Should not exist
}

TEST_F(DatabaseTest, TransactionCommits) {
  romulus::core::DatVersion dat{
      .name = "Commit Test",
      .version = "1.0",
      .source_url = {},
      .checksum = "cm1",
      .imported_at = {},
  };
  {
    auto txn = db_->begin_transaction();
    auto id = db_->insert_dat_version(dat);
    (void)id;
    txn.commit();
  }

  auto found = db_->find_dat_version("Commit Test", "1.0");
  ASSERT_TRUE(found.has_value());
  EXPECT_TRUE(found->has_value());
}

TEST_F(DatabaseTest, FindsDuplicateFiles) {
  romulus::core::DatVersion dat{
      .name = "Dup",
      .version = "1.0",
      .source_url = {},
      .checksum = "abc",
      .imported_at = {},
  };
  auto dat_id = db_->insert_dat_version(dat);
  ASSERT_TRUE(dat_id.has_value());

  romulus::core::RomInfo rom{.id = 0,
                             .dat_version_id = *dat_id,
                             .game_name = "Dup Game",
                             .name = "dup.bin",
                             .size = 100,
                             .crc32 = "aaaaaaaa",
                             .md5 = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                             .sha1 = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                             .sha256 = {},
                             .region = {}};
  auto rom_id = db_->insert_rom(rom);
  ASSERT_TRUE(rom_id.has_value());

  romulus::core::FileInfo file1{
      .id = 0,
      .filename = "copy1.bin",
      .path = "/roms/copy1.bin",
      .size = 100,
      .crc32 = "aaaaaaaa",
      .md5 = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
      .sha1 = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
      .sha256 = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
      .last_scanned = {},
      .is_archive_entry = false};
  romulus::core::FileInfo file2{
      .id = 0,
      .filename = "copy2.bin",
      .path = "/roms/copy2.bin",
      .size = 100,
      .crc32 = "aaaaaaaa",
      .md5 = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
      .sha1 = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
      .sha256 = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
      .last_scanned = {},
      .is_archive_entry = false};
  ASSERT_TRUE(db_->upsert_file(file1).has_value());
  ASSERT_TRUE(db_->upsert_file(file2).has_value());

  romulus::core::MatchResult match{.rom_id = *rom_id,
                                   .global_rom_sha1 = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                                   .match_type = romulus::core::MatchType::Exact};
  ASSERT_TRUE(db_->insert_rom_match(match).has_value());

  auto dupes = db_->get_duplicate_files();
  ASSERT_TRUE(dupes.has_value()) << dupes.error().message;
  EXPECT_GE(dupes->size(), 2u);
}

TEST_F(DatabaseTest, FindsUnverifiedFiles) {
  romulus::core::FileInfo orphan{
      .id = 0,
      .filename = "orphan.bin",
      .path = "/roms/orphan.bin",
      .size = 50,
      .crc32 = "bbbbbbbb",
      .md5 = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
      .sha1 = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
      .sha256 = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
      .last_scanned = {},
      .is_archive_entry = false};
  ASSERT_TRUE(db_->upsert_file(orphan).has_value());

  auto unverified = db_->get_unverified_files();
  ASSERT_TRUE(unverified.has_value()) << unverified.error().message;
  EXPECT_EQ(unverified->size(), 1u);
  EXPECT_EQ(unverified->front().path, "/roms/orphan.bin");
}

TEST_F(DatabaseTest, GetAllRomsReturnsAll) {
  romulus::core::DatVersion dat{
      .name = "System A",
      .version = "1.0",
      .source_url = {},
      .checksum = "chk1",
      .imported_at = {},
  };
  auto dat_id = db_->insert_dat_version(dat);
  ASSERT_TRUE(dat_id.has_value());

  for (int i = 0; i < 3; ++i) {
    romulus::core::RomInfo rom{
        .id = 0,
        .dat_version_id = *dat_id,
        .game_name = "Game " + std::to_string(i),
        .name = "rom" + std::to_string(i) + ".bin",
        .size = 100,
        .crc32 = {},
        .md5 = {},
        .sha1 = std::string(40, static_cast<char>('a' + i)),
        .sha256 = {},
        .region = {},
    };
    auto id = db_->insert_rom(rom);
    (void)id;
  }

  auto all = db_->get_all_roms();
  ASSERT_TRUE(all.has_value());
  EXPECT_EQ(all->size(), 3u);
}

TEST_F(DatabaseTest, ComputedRomStatusMissingWhenNoMatch) {
  romulus::core::DatVersion dat{
      .name = "Sys",
      .version = "1.0",
      .source_url = {},
      .checksum = "c1",
      .imported_at = {},
  };
  auto dat_id = db_->insert_dat_version(dat);
  ASSERT_TRUE(dat_id.has_value());

  romulus::core::RomInfo rom{.id = 0,
                             .dat_version_id = *dat_id,
                             .game_name = "G",
                             .name = "r.bin",
                             .size = 0,
                             .crc32 = {},
                             .md5 = {},
                             .sha1 = std::string(40, '1'),
                             .sha256 = {},
                             .region = {}};
  auto rom_id = db_->insert_rom(rom);
  ASSERT_TRUE(rom_id.has_value());

  auto status = db_->get_computed_rom_status(*rom_id);
  ASSERT_TRUE(status.has_value());
  EXPECT_EQ(*status, romulus::core::RomStatusType::Missing);
}

TEST_F(DatabaseTest, ComputedRomStatusVerifiedWhenExactMatchAndFileExists) {
  romulus::core::DatVersion dat{
      .name = "Sys",
      .version = "1.0",
      .source_url = {},
      .checksum = "c2",
      .imported_at = {},
  };
  auto dat_id = db_->insert_dat_version(dat);
  ASSERT_TRUE(dat_id.has_value());

  const std::string sha1 = "cccccccccccccccccccccccccccccccccccccccc";
  romulus::core::RomInfo rom{.id = 0,
                             .dat_version_id = *dat_id,
                             .game_name = "G",
                             .name = "r.bin",
                             .size = 0,
                             .crc32 = {},
                             .md5 = {},
                             .sha1 = sha1,
                             .sha256 = {},
                             .region = {}};
  auto rom_id = db_->insert_rom(rom);
  ASSERT_TRUE(rom_id.has_value());

  romulus::core::FileInfo file{.id = 0,
                               .filename = "r.bin",
                               .path = "/roms/r.bin",
                               .size = 100,
                               .crc32 = {},
                               .md5 = {},
                               .sha1 = sha1,
                               .sha256 = {},
                               .last_scanned = {},
                               .is_archive_entry = false};
  ASSERT_TRUE(db_->upsert_file(file).has_value());

  romulus::core::MatchResult match{
      .rom_id = *rom_id, .global_rom_sha1 = sha1, .match_type = romulus::core::MatchType::Exact};
  ASSERT_TRUE(db_->insert_rom_match(match).has_value());

  auto status = db_->get_computed_rom_status(*rom_id);
  ASSERT_TRUE(status.has_value());
  EXPECT_EQ(*status, romulus::core::RomStatusType::Verified);
}

} // namespace
