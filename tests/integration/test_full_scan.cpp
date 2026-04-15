#include "romulus/service/romulus_service.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <mutex>

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

TEST_F(FullScanTest, ListDatVersionsAfterImport) {
  romulus::service::RomulusService svc(db_path_);

  auto dat = svc.import_dat(k_FixturesDir / "sample.dat");
  ASSERT_TRUE(dat.has_value());

  auto dats = svc.list_dat_versions();
  ASSERT_TRUE(dats.has_value());
  EXPECT_EQ(dats->size(), 1u);
  EXPECT_EQ(dats->front().name, "Test System - Sample");
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
  auto dats = svc.list_dat_versions();
  ASSERT_TRUE(dats.has_value());
  EXPECT_FALSE(dats->empty());

  // Purge
  auto purge = svc.purge_database();
  ASSERT_TRUE(purge.has_value()) << purge.error().message;

  // Verify all data is gone
  auto dats_after = svc.list_dat_versions();
  ASSERT_TRUE(dats_after.has_value());
  EXPECT_TRUE(dats_after->empty());

  auto files_after = svc.get_all_files();
  ASSERT_TRUE(files_after.has_value());
  EXPECT_TRUE(files_after->empty());
}

TEST_F(FullScanTest, RescanSkipsUnchangedFiles) {
  romulus::service::RomulusService svc(db_path_);

  // First scan — all files should be hashed
  auto scan1 = svc.scan_directory(rom_dir_);
  ASSERT_TRUE(scan1.has_value()) << scan1.error().message;
  EXPECT_GT(scan1->files_hashed, 0);
  EXPECT_EQ(scan1->files_skipped, 0);

  // Second scan without touching any file — all files should be skipped
  auto scan2 = svc.scan_directory(rom_dir_);
  ASSERT_TRUE(scan2.has_value()) << scan2.error().message;
  EXPECT_EQ(scan2->files_hashed, 0);
  EXPECT_EQ(scan2->files_skipped, scan1->files_hashed);
}

TEST_F(FullScanTest, RescanRehashesModifiedFile) {
  romulus::service::RomulusService svc(db_path_);

  // First scan
  auto scan1 = svc.scan_directory(rom_dir_);
  ASSERT_TRUE(scan1.has_value()) << scan1.error().message;
  EXPECT_GT(scan1->files_hashed, 0);

  // Overwrite the ROM file with different same-size content and bump its mtime.
  // Same size isolates the mtime-fingerprint path: the re-hash is triggered solely because
  // last_write_time changed, not because the file grew or shrank.
  const auto rom_file = rom_dir_ / "test.bin";
  const auto original_size = std::filesystem::file_size(rom_file);
  const auto original_mtime = std::filesystem::last_write_time(rom_file);
  {
    std::ofstream f(rom_file, std::ios::binary | std::ios::trunc);
    f << "ROM content herd"; // same 16 bytes, different bits
  }
  ASSERT_EQ(std::filesystem::file_size(rom_file), original_size);
  // Bump mtime explicitly so the fingerprint check sees a changed timestamp.
  const auto new_mtime = original_mtime + std::chrono::seconds(2);
  std::filesystem::last_write_time(rom_file, new_mtime);
  EXPECT_NE(std::filesystem::last_write_time(rom_file), original_mtime);

  // Second scan — the modified file must be re-hashed, not skipped
  auto scan2 = svc.scan_directory(rom_dir_);
  ASSERT_TRUE(scan2.has_value()) << scan2.error().message;
  EXPECT_GT(scan2->files_hashed, 0);
}

TEST_F(FullScanTest, ScanDirectoryProgressCallbackReportsProgress) {
  romulus::service::RomulusService svc(db_path_);

  std::vector<romulus::core::ScanProgress> progress_updates;
  std::mutex progress_mutex;

  auto scan = svc.scan_directory(
      rom_dir_,
      {},
      [&progress_updates, &progress_mutex](const romulus::core::ScanProgress& progress) {
        std::lock_guard lock(progress_mutex);
        progress_updates.push_back(progress);
      });
  ASSERT_TRUE(scan.has_value()) << scan.error().message;

  ASSERT_FALSE(progress_updates.empty());
  const auto& final_update = progress_updates.back();
  EXPECT_EQ(final_update.files_discovered, scan->files_scanned);
  EXPECT_EQ(final_update.files_hashed, scan->files_hashed);
  EXPECT_DOUBLE_EQ(final_update.estimated_percent, 100.0);
  EXPECT_TRUE(final_update.current_file.empty());

  const auto saw_current_file = std::any_of(progress_updates.begin(),
                                            progress_updates.end(),
                                            [](const auto& update) {
    return !update.current_file.empty();
  });
  EXPECT_TRUE(saw_current_file);

  for (std::size_t i = 1; i < progress_updates.size(); ++i) {
    EXPECT_GE(progress_updates[i].files_hashed, progress_updates[i - 1].files_hashed);
    EXPECT_GE(progress_updates[i].estimated_percent, progress_updates[i - 1].estimated_percent);
  }
}

} // namespace
