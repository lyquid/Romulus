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
  // If we got here without throwing, the schema was created
  SUCCEED();
}

TEST_F(DatabaseTest, InsertsAndFindsSystem) {
  romulus::core::SystemInfo sys{
      .name = "Nintendo - Game Boy",
      .short_name = "GB",
      .extensions = ".gb,.gbc",
  };

  auto id = db_->insert_system(sys);
  ASSERT_TRUE(id.has_value()) << id.error().message;
  EXPECT_GT(*id, 0);

  auto found = db_->find_system_by_name("Nintendo - Game Boy");
  ASSERT_TRUE(found.has_value());
  ASSERT_TRUE(found->has_value());
  EXPECT_EQ(found->value().name, "Nintendo - Game Boy");
  EXPECT_EQ(found->value().short_name, "GB");
}

TEST_F(DatabaseTest, GetOrCreateSystemIsIdempotent) {
  auto id1 = db_->get_or_create_system("Sega - Mega Drive");
  auto id2 = db_->get_or_create_system("Sega - Mega Drive");

  ASSERT_TRUE(id1.has_value());
  ASSERT_TRUE(id2.has_value());
  EXPECT_EQ(*id1, *id2);
}

TEST_F(DatabaseTest, InsertAndRetrieveRom) {
  auto sys_id = db_->get_or_create_system("Test System");
  ASSERT_TRUE(sys_id.has_value());

  romulus::core::DatVersion dat{
      .dat_id = {},
      .system_id = *sys_id,
      .name = "Test System",
      .version = "1.0",
      .source_url = {},
      .checksum = "abc123",
      .imported_at = {},
  };
  auto dat_id = db_->insert_dat_version(dat);
  ASSERT_TRUE(dat_id.has_value());

  romulus::core::GameInfo game{
      .dat_game_id = {},
      .name = "Test Game",
      .description = {},
      .clone_of = {},
      .category = {},
      .game_id_text = {},
      .system_id = *sys_id,
      .dat_version_id = *dat_id,
      .roms = {},
  };
  auto game_id = db_->insert_game(game);
  ASSERT_TRUE(game_id.has_value());

  romulus::core::RomInfo rom{
      .game_id = *game_id,
      .name = "test.bin",
      .size = 1024,
      .crc32 = "deadbeef",
      .md5 = "d41d8cd98f00b204e9800998ecf8427e",
      .sha1 = "da39a3ee5e6b4b0d3255bfef95601890afd80709",
      .sha256 = {},
      .region = "USA",
      .status = {},
      .serial = {},
      .header = {},
  };
  auto rom_id = db_->insert_rom(rom);
  ASSERT_TRUE(rom_id.has_value());

  auto found = db_->find_rom_by_sha1("da39a3ee5e6b4b0d3255bfef95601890afd80709");
  ASSERT_TRUE(found.has_value());
  ASSERT_TRUE(found->has_value());
  EXPECT_EQ(found->value().name, "test.bin");
  EXPECT_EQ(found->value().size, 1024);
}

TEST_F(DatabaseTest, UpsertFileUpdatesExisting) {
  romulus::core::FileInfo file{
      .path = "/roms/test.bin",
      .size = 1024,
      .crc32 = "aabbccdd",
      .md5 = "12345678901234567890123456789012",
      .sha1 = "1234567890123456789012345678901234567890",
      .last_scanned = {},
  };

  auto id1 = db_->upsert_file(file);
  ASSERT_TRUE(id1.has_value());

  file.crc32 = "updated!";
  auto id2 = db_->upsert_file(file);
  ASSERT_TRUE(id2.has_value());
  EXPECT_EQ(*id1, *id2);

  auto found = db_->find_file_by_path("/roms/test.bin");
  ASSERT_TRUE(found.has_value());
  ASSERT_TRUE(found->has_value());
  EXPECT_EQ(found->value().crc32, "updated!");
}

TEST_F(DatabaseTest, TransactionRollsBackOnScopeExit) {
  {
    auto txn = db_->begin_transaction();
    auto id = db_->get_or_create_system("Should Be Rolled Back");
    // txn goes out of scope without commit -> rollback
  }

  auto found = db_->find_system_by_name("Should Be Rolled Back");
  ASSERT_TRUE(found.has_value());
  EXPECT_FALSE(found->has_value()); // Should not exist
}

TEST_F(DatabaseTest, TransactionCommits) {
  {
    auto txn = db_->begin_transaction();
    auto id = db_->get_or_create_system("Should Persist");
    txn.commit();
  }

  auto found = db_->find_system_by_name("Should Persist");
  ASSERT_TRUE(found.has_value());
  EXPECT_TRUE(found->has_value());
}

TEST_F(DatabaseTest, InsertAndRetrieveGameWithCloneOf) {
  auto sys_id = db_->get_or_create_system("Test System");
  ASSERT_TRUE(sys_id.has_value());

  romulus::core::DatVersion dat{
      .dat_id = "17",
      .system_id = *sys_id,
      .name = "Test System",
      .version = "1.0",
      .source_url = {},
      .checksum = "abc123",
      .imported_at = {},
  };
  auto dat_id = db_->insert_dat_version(dat);
  ASSERT_TRUE(dat_id.has_value());

  // Insert parent game
  romulus::core::GameInfo parent{
      .dat_game_id = "0001",
      .name = "Parent Game (USA)",
      .description = "Parent Game (USA)",
      .clone_of = {},
      .category = "Games",
      .game_id_text = {},
      .system_id = *sys_id,
      .dat_version_id = *dat_id,
      .roms = {},
  };
  auto parent_id = db_->insert_game(parent);
  ASSERT_TRUE(parent_id.has_value());

  // Insert clone game
  romulus::core::GameInfo clone{
      .dat_game_id = "0002",
      .name = "Clone Game (Japan)",
      .description = "Clone Game (Japan)",
      .clone_of = "0001",
      .category = "Games",
      .game_id_text = {},
      .system_id = *sys_id,
      .dat_version_id = *dat_id,
      .roms = {},
  };
  auto clone_id = db_->insert_game(clone);
  ASSERT_TRUE(clone_id.has_value());

  // Retrieve and verify
  auto games = db_->get_games_by_dat_version(*dat_id);
  ASSERT_TRUE(games.has_value());
  ASSERT_EQ(games->size(), 2);

  // Find the parent and clone
  const auto& g1 = (*games)[0];
  const auto& g2 = (*games)[1];

  EXPECT_EQ(g1.dat_game_id, "0001");
  EXPECT_EQ(g1.name, "Parent Game (USA)");
  EXPECT_EQ(g1.category, "Games");
  EXPECT_TRUE(g1.clone_of.empty());

  EXPECT_EQ(g2.dat_game_id, "0002");
  EXPECT_EQ(g2.name, "Clone Game (Japan)");
  EXPECT_EQ(g2.clone_of, "0001");
  EXPECT_EQ(g2.category, "Games");
}

TEST_F(DatabaseTest, InsertAndRetrieveRomWithExtendedFields) {
  auto sys_id = db_->get_or_create_system("Test System");
  ASSERT_TRUE(sys_id.has_value());

  romulus::core::DatVersion dat{
      .dat_id = {},
      .system_id = *sys_id,
      .name = "Test System",
      .version = "2.0",
      .source_url = {},
      .checksum = "def456",
      .imported_at = {},
  };
  auto dat_id = db_->insert_dat_version(dat);
  ASSERT_TRUE(dat_id.has_value());

  romulus::core::GameInfo game{
      .dat_game_id = "0001",
      .name = "NES Game",
      .description = "NES Game",
      .clone_of = {},
      .category = "Games",
      .game_id_text = {},
      .system_id = *sys_id,
      .dat_version_id = *dat_id,
      .roms = {},
  };
  auto game_id = db_->insert_game(game);
  ASSERT_TRUE(game_id.has_value());

  romulus::core::RomInfo rom{
      .game_id = *game_id,
      .name = "test.nes",
      .size = 1024,
      .crc32 = "aabbccdd",
      .md5 = "md5hash",
      .sha1 = "sha1hash",
      .sha256 = "sha256hash",
      .region = "USA",
      .status = "verified",
      .serial = "TST-001",
      .header = "4E 45 53 1A",
  };
  auto rom_id = db_->insert_rom(rom);
  ASSERT_TRUE(rom_id.has_value());

  auto found = db_->find_rom_by_sha1("sha1hash");
  ASSERT_TRUE(found.has_value());
  ASSERT_TRUE(found->has_value());
  EXPECT_EQ(found->value().name, "test.nes");
  EXPECT_EQ(found->value().sha256, "sha256hash");
  EXPECT_EQ(found->value().status, "verified");
  EXPECT_EQ(found->value().serial, "TST-001");
  EXPECT_EQ(found->value().header, "4E 45 53 1A");
}

} // namespace
