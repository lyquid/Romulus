#include "romulus/service/romulus_service.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace {

const std::filesystem::path k_FixturesDir{ROMULUS_TEST_FIXTURES_DIR};

class FullScanTest : public ::testing::Test {
protected:
  void SetUp() override {
    db_path_ = std::filesystem::temp_directory_path() / "romulus_integration_test.db";
    rom_dir_ = std::filesystem::temp_directory_path() / "romulus_test_roms";
    std::filesystem::remove(db_path_);
    std::filesystem::create_directories(rom_dir_);

    // Create some fake ROM files
    {
      std::ofstream f(rom_dir_ / "test.bin", std::ios::binary);
      f << "ROM content here";
    }
  }

  void TearDown() override {
    std::filesystem::remove(db_path_);
    std::filesystem::remove(db_path_.string() + "-wal");
    std::filesystem::remove(db_path_.string() + "-shm");
    std::filesystem::remove_all(rom_dir_);
  }

  std::filesystem::path db_path_;
  std::filesystem::path rom_dir_;
};

TEST_F(FullScanTest, ImportDatAndScanDirectory) {
  romulus::service::RomulusService svc(db_path_);

  // Import DAT
  auto dat = svc.import_dat(k_FixturesDir / "sample.dat");
  ASSERT_TRUE(dat.has_value()) << dat.error().message;
  EXPECT_EQ(dat->name, "Test System - Sample");

  // Scan directory
  auto scan = svc.scan_directory(rom_dir_);
  ASSERT_TRUE(scan.has_value()) << scan.error().message;
  EXPECT_GT(scan->files_scanned, 0);

  // Verify
  auto verify = svc.verify();
  ASSERT_TRUE(verify.has_value()) << verify.error().message;

  // Check summary
  auto summary = svc.get_summary();
  ASSERT_TRUE(summary.has_value()) << summary.error().message;
  EXPECT_EQ(summary->total_roms, 3); // 3 ROMs in sample.dat
}

TEST_F(FullScanTest, ListSystemsAfterImport) {
  romulus::service::RomulusService svc(db_path_);

  auto dat = svc.import_dat(k_FixturesDir / "sample.dat");
  ASSERT_TRUE(dat.has_value());

  auto systems = svc.list_systems();
  ASSERT_TRUE(systems.has_value());
  EXPECT_EQ(systems->size(), 1);
  EXPECT_EQ(systems->front().name, "Test System - Sample");
}

TEST_F(FullScanTest, GetAllFilesReturnsScannedFiles) {
  romulus::service::RomulusService svc(db_path_);

  // Before scan, no files
  auto files_before = svc.get_all_files();
  ASSERT_TRUE(files_before.has_value()) << files_before.error().message;
  EXPECT_TRUE(files_before->empty());

  // Scan directory
  auto scan = svc.scan_directory(rom_dir_);
  ASSERT_TRUE(scan.has_value()) << scan.error().message;

  // After scan, files are present
  auto files_after = svc.get_all_files();
  ASSERT_TRUE(files_after.has_value()) << files_after.error().message;
  EXPECT_EQ(files_after->size(), scan->files_scanned);
}

TEST_F(FullScanTest, PurgeDatabaseClearsAllData) {
  romulus::service::RomulusService svc(db_path_);

  // Import and scan to populate DB
  auto dat = svc.import_dat(k_FixturesDir / "sample.dat");
  ASSERT_TRUE(dat.has_value()) << dat.error().message;
  auto scan = svc.scan_directory(rom_dir_);
  ASSERT_TRUE(scan.has_value()) << scan.error().message;

  // Verify data exists
  auto systems = svc.list_systems();
  ASSERT_TRUE(systems.has_value());
  EXPECT_FALSE(systems->empty());

  // Purge
  auto purge = svc.purge_database();
  ASSERT_TRUE(purge.has_value()) << purge.error().message;

  // Verify all data is gone
  auto systems_after = svc.list_systems();
  ASSERT_TRUE(systems_after.has_value());
  EXPECT_TRUE(systems_after->empty());

  auto files_after = svc.get_all_files();
  ASSERT_TRUE(files_after.has_value());
  EXPECT_TRUE(files_after->empty());
}

} // namespace
