#include "romulus/service/romulus_service.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

const std::filesystem::path k_FixturesDir{ROMULUS_TEST_FIXTURES_DIR};

class FullScanTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Use unique names per test to avoid collisions under parallel CTest runs.
    const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
    const std::string base =
        std::string("romulus_int_") + info->test_suite_name() + "_" + info->name();
    db_path_ = std::filesystem::temp_directory_path() / (base + ".db");
    rom_dir_ = std::filesystem::temp_directory_path() / (base + "_roms");
    other_dir_ = std::filesystem::temp_directory_path() / (base + "_other_roms");

    std::filesystem::remove(db_path_);
    std::filesystem::remove(db_path_.string() + "-wal");
    std::filesystem::remove(db_path_.string() + "-shm");
    std::filesystem::create_directories(rom_dir_);
    // other_dir_ is created on demand by the test that needs it

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
    std::filesystem::remove_all(other_dir_); // no-op if not created
  }

  std::filesystem::path db_path_;
  std::filesystem::path rom_dir_;
  std::filesystem::path other_dir_; ///< Secondary scan directory; created on demand by individual tests
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

  // Register a scan directory so we can verify it is purged
  auto add_dir = svc.add_scan_directory(rom_dir_);
  ASSERT_TRUE(add_dir.has_value()) << add_dir.error().message;

  // Verify data exists
  auto dats = svc.list_dat_versions();
  ASSERT_TRUE(dats.has_value());
  EXPECT_FALSE(dats->empty());

  auto dirs_before = svc.get_scan_directories();
  ASSERT_TRUE(dirs_before.has_value());
  EXPECT_FALSE(dirs_before->empty());

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

  auto dirs_after = svc.get_scan_directories();
  ASSERT_TRUE(dirs_after.has_value());
  EXPECT_TRUE(dirs_after->empty());
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

TEST_F(FullScanTest, FullSyncPipelineProducesExpectedSummary) {
  romulus::service::RomulusService svc(db_path_);

  auto result = svc.full_sync(k_FixturesDir / "sample.dat", rom_dir_);
  ASSERT_TRUE(result.has_value()) << result.error().message;

  // DAT must be imported
  auto dats = svc.list_dat_versions();
  ASSERT_TRUE(dats.has_value());
  EXPECT_EQ(dats->size(), 1u);

  // Files must be scanned
  auto files = svc.get_all_files();
  ASSERT_TRUE(files.has_value());
  EXPECT_GT(files->size(), 0u);

  // Summary must reflect the DAT's ROM count (3 ROMs in sample.dat)
  auto summary = svc.get_summary();
  ASSERT_TRUE(summary.has_value()) << summary.error().message;
  EXPECT_EQ(summary->total_roms, 3);
}

TEST_F(FullScanTest, FullSyncFailsFastOnInvalidDatWithoutScanning) {
  romulus::service::RomulusService svc(db_path_);

  // Pass a non-existent DAT — fail-fast validation should prevent the scan.
  auto result = svc.full_sync(rom_dir_ / "nonexistent.dat", rom_dir_);
  EXPECT_FALSE(result.has_value());

  // Because the DAT was validated before scanning, no files should have been persisted.
  auto files = svc.get_all_files();
  ASSERT_TRUE(files.has_value());
  EXPECT_TRUE(files->empty());
}

TEST_F(FullScanTest, DeleteDatRemovesDatGamesAndRoms) {
  romulus::service::RomulusService svc(db_path_);

  // Import DAT and scan so we have matches to verify cascade
  auto dat = svc.import_dat(k_FixturesDir / "sample.dat");
  ASSERT_TRUE(dat.has_value()) << dat.error().message;
  auto scan = svc.scan_directory(rom_dir_);
  ASSERT_TRUE(scan.has_value()) << scan.error().message;
  auto verify = svc.verify();
  ASSERT_TRUE(verify.has_value()) << verify.error().message;

  // Verify DAT is present
  auto dats_before = svc.list_dat_versions();
  ASSERT_TRUE(dats_before.has_value());
  ASSERT_EQ(dats_before->size(), 1u);

  const std::int64_t dat_id = dats_before->front().id;

  // Delete the DAT version
  auto del = svc.delete_dat(dat_id);
  ASSERT_TRUE(del.has_value()) << del.error().message;

  // DAT should be gone
  auto dats_after = svc.list_dat_versions();
  ASSERT_TRUE(dats_after.has_value());
  EXPECT_TRUE(dats_after->empty());

  // Summary should show zero ROMs
  auto summary = svc.get_summary();
  ASSERT_TRUE(summary.has_value()) << summary.error().message;
  EXPECT_EQ(summary->total_roms, 0);

  // Scanned files should still be present (delete-dat does not purge scan data)
  auto files = svc.get_all_files();
  ASSERT_TRUE(files.has_value());
  EXPECT_GT(files->size(), 0u);
}

TEST_F(FullScanTest, DeleteDatReturnsErrorForUnknownId) {
  romulus::service::RomulusService svc(db_path_);

  auto result = svc.delete_dat(9999);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, romulus::core::ErrorCode::NotFound);
  EXPECT_NE(result.error().message.find("9999"), std::string::npos);
}

TEST_F(FullScanTest, ScanPrunesDeletedFiles) {
  romulus::service::RomulusService svc(db_path_);

  // First scan — file is indexed in the database.
  auto scan1 = svc.scan_directory(rom_dir_);
  ASSERT_TRUE(scan1.has_value()) << scan1.error().message;
  EXPECT_EQ(scan1->files_scanned, 1);
  EXPECT_EQ(scan1->files_pruned, 0);

  // Verify the file is in the DB.
  auto files_before = svc.get_all_files();
  ASSERT_TRUE(files_before.has_value());
  ASSERT_EQ(files_before->size(), 1u);

  // Delete the file from disk.
  ASSERT_TRUE(std::filesystem::remove(rom_dir_ / "test.bin"));

  // Rescan — deleted file should be pruned from the DB automatically.
  auto scan2 = svc.scan_directory(rom_dir_);
  ASSERT_TRUE(scan2.has_value()) << scan2.error().message;
  EXPECT_EQ(scan2->files_scanned, 0);
  EXPECT_EQ(scan2->files_pruned, 1);

  // The DB should no longer contain the deleted file.
  auto files_after = svc.get_all_files();
  ASSERT_TRUE(files_after.has_value());
  EXPECT_TRUE(files_after->empty());
}

TEST_F(FullScanTest, ScanPreservesFilesFromOtherDirectories) {
  // Create a second directory with its own ROM file (cleaned up by TearDown via other_dir_).
  std::filesystem::create_directories(other_dir_);
  {
    std::ofstream f(other_dir_ / "other.bin", std::ios::binary);
    f << "Other ROM content";
  }

  romulus::service::RomulusService svc(db_path_);

  // Scan both directories so both files are in the DB.
  auto scan_main = svc.scan_directory(rom_dir_);
  ASSERT_TRUE(scan_main.has_value()) << scan_main.error().message;
  EXPECT_EQ(scan_main->files_scanned, 1);

  auto scan_other = svc.scan_directory(other_dir_);
  ASSERT_TRUE(scan_other.has_value()) << scan_other.error().message;
  EXPECT_EQ(scan_other->files_scanned, 1);

  // Two files in DB total.
  auto files = svc.get_all_files();
  ASSERT_TRUE(files.has_value());
  EXPECT_EQ(files->size(), 2u);

  // Delete the file from the main directory only.
  ASSERT_TRUE(std::filesystem::remove(rom_dir_ / "test.bin"));

  // Rescanning only the main directory prunes the deleted file but preserves the other dir entry.
  auto scan_prune = svc.scan_directory(rom_dir_);
  ASSERT_TRUE(scan_prune.has_value()) << scan_prune.error().message;
  EXPECT_EQ(scan_prune->files_pruned, 1);

  // The other directory's file must still be in the DB.
  auto files_after = svc.get_all_files();
  ASSERT_TRUE(files_after.has_value());
  ASSERT_EQ(files_after->size(), 1u);
  // get_all_files() does not guarantee a stable row order, so search rather than index front().
  EXPECT_TRUE(std::ranges::any_of(*files_after, [](const auto& f) {
    return f.path.find("other.bin") != std::string::npos;
  }));
}

} // namespace
