#include "romulus/service/romulus_service.hpp"

#include "romulus/core/error.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace {

const std::filesystem::path k_FixturesDir{ROMULUS_TEST_FIXTURES_DIR};

class RomulusServiceTest : public ::testing::Test {
protected:
  void SetUp() override {
    db_path_ = std::filesystem::temp_directory_path() / "romulus_service_unit_test.db";
    std::filesystem::remove(db_path_);
    std::filesystem::remove(db_path_.string() + "-wal");
    std::filesystem::remove(db_path_.string() + "-shm");
  }

  void TearDown() override {
    std::filesystem::remove(db_path_);
    std::filesystem::remove(db_path_.string() + "-wal");
    std::filesystem::remove(db_path_.string() + "-shm");
  }

  std::filesystem::path db_path_;
};

// ── Construction ──────────────────────────────────────────────

TEST_F(RomulusServiceTest, GetDbPathReturnsConstructorPath) {
  romulus::service::RomulusService svc(db_path_);
  EXPECT_EQ(svc.get_db_path(), db_path_);
}

// ── Queries on fresh database ─────────────────────────────────

TEST_F(RomulusServiceTest, ListDatVersionsEmptyOnFreshDatabase) {
  romulus::service::RomulusService svc(db_path_);
  auto result = svc.list_dat_versions();
  ASSERT_TRUE(result.has_value()) << result.error().message;
  EXPECT_TRUE(result->empty());
}

TEST_F(RomulusServiceTest, GetAllFilesEmptyOnFreshDatabase) {
  romulus::service::RomulusService svc(db_path_);
  auto result = svc.get_all_files();
  ASSERT_TRUE(result.has_value()) << result.error().message;
  EXPECT_TRUE(result->empty());
}

TEST_F(RomulusServiceTest, GetSummaryReturnsZerosOnFreshDatabase) {
  romulus::service::RomulusService svc(db_path_);
  auto result = svc.get_summary();
  ASSERT_TRUE(result.has_value()) << result.error().message;
  EXPECT_EQ(result->total_roms, 0);
  EXPECT_EQ(result->verified, 0);
  EXPECT_EQ(result->missing, 0);
}

TEST_F(RomulusServiceTest, GetMissingRomsEmptyOnFreshDatabase) {
  romulus::service::RomulusService svc(db_path_);
  auto result = svc.get_missing_roms();
  ASSERT_TRUE(result.has_value()) << result.error().message;
  EXPECT_TRUE(result->empty());
}

TEST_F(RomulusServiceTest, GetScanDirectoriesEmptyOnFreshDatabase) {
  romulus::service::RomulusService svc(db_path_);
  auto result = svc.get_scan_directories();
  ASSERT_TRUE(result.has_value()) << result.error().message;
  EXPECT_TRUE(result->empty());
}

// ── Import DAT ────────────────────────────────────────────────

TEST_F(RomulusServiceTest, ImportDatReturnsErrorForMissingFile) {
  romulus::service::RomulusService svc(db_path_);
  auto result = svc.import_dat("/nonexistent/path/to.dat");
  ASSERT_FALSE(result.has_value());
}

TEST_F(RomulusServiceTest, ImportDatReturnsErrorForMalformedXml) {
  // Write an invalid XML file
  auto bad_dat = std::filesystem::temp_directory_path() / "romulus_test_bad_service.dat";
  {
    std::ofstream f(bad_dat);
    f << "<not-a-datafile><garbage/></not-a-datafile>";
  }

  romulus::service::RomulusService svc(db_path_);
  auto result = svc.import_dat(bad_dat);
  ASSERT_FALSE(result.has_value());

  std::filesystem::remove(bad_dat);
}

TEST_F(RomulusServiceTest, ImportDatSucceedsWithValidDat) {
  romulus::service::RomulusService svc(db_path_);
  auto result = svc.import_dat(k_FixturesDir / "sample.dat");
  ASSERT_TRUE(result.has_value()) << result.error().message;
  EXPECT_EQ(result->name, "Test System - Sample");
  EXPECT_FALSE(result->version.empty());
}

TEST_F(RomulusServiceTest, ImportDatPopulatesListDatVersions) {
  romulus::service::RomulusService svc(db_path_);
  ASSERT_TRUE(svc.import_dat(k_FixturesDir / "sample.dat").has_value());

  auto versions = svc.list_dat_versions();
  ASSERT_TRUE(versions.has_value()) << versions.error().message;
  ASSERT_EQ(versions->size(), 1u);
  EXPECT_EQ(versions->front().name, "Test System - Sample");
}

TEST_F(RomulusServiceTest, ImportDatDeduplicatesBySha256) {
  romulus::service::RomulusService svc(db_path_);

  // Import the same file twice — the second call should return the cached version
  auto first = svc.import_dat(k_FixturesDir / "sample.dat");
  ASSERT_TRUE(first.has_value()) << first.error().message;

  auto second = svc.import_dat(k_FixturesDir / "sample.dat");
  ASSERT_TRUE(second.has_value()) << second.error().message;

  // Only one DAT version should exist in the database
  auto versions = svc.list_dat_versions();
  ASSERT_TRUE(versions.has_value());
  EXPECT_EQ(versions->size(), 1u);

  // Both calls must return the same record
  EXPECT_EQ(first->id, second->id);
}

// ── Scan ──────────────────────────────────────────────────────

TEST_F(RomulusServiceTest, ScanDirectoryReturnsErrorForNonExistentDirectory) {
  romulus::service::RomulusService svc(db_path_);
  auto result = svc.scan_directory("/nonexistent/directory/that/does/not/exist");
  ASSERT_FALSE(result.has_value());
}

TEST_F(RomulusServiceTest, ScanDirectorySucceedsOnEmptyDirectory) {
  auto empty_dir = std::filesystem::temp_directory_path() / "romulus_scan_empty_unit";
  std::filesystem::create_directories(empty_dir);

  romulus::service::RomulusService svc(db_path_);
  auto result = svc.scan_directory(empty_dir);
  ASSERT_TRUE(result.has_value()) << result.error().message;
  EXPECT_EQ(result->files_scanned, 0);

  std::filesystem::remove_all(empty_dir);
}

// ── Verify ────────────────────────────────────────────────────

TEST_F(RomulusServiceTest, VerifySucceedsOnEmptyDatabase) {
  romulus::service::RomulusService svc(db_path_);
  auto result = svc.verify();
  ASSERT_TRUE(result.has_value()) << result.error().message;
}

TEST_F(RomulusServiceTest, VerifyReturnsErrorForUnknownDatName) {
  romulus::service::RomulusService svc(db_path_);
  auto result = svc.verify("nonexistent-dat-name");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, romulus::core::ErrorCode::NotFound);
}

// ── Purge ─────────────────────────────────────────────────────

TEST_F(RomulusServiceTest, PurgeDatabaseSucceedsOnEmptyDatabase) {
  romulus::service::RomulusService svc(db_path_);
  auto result = svc.purge_database();
  ASSERT_TRUE(result.has_value()) << result.error().message;
}

TEST_F(RomulusServiceTest, PurgeDatabaseRemovesImportedDat) {
  romulus::service::RomulusService svc(db_path_);

  ASSERT_TRUE(svc.import_dat(k_FixturesDir / "sample.dat").has_value());

  auto before = svc.list_dat_versions();
  ASSERT_TRUE(before.has_value());
  EXPECT_FALSE(before->empty());

  ASSERT_TRUE(svc.purge_database().has_value());

  auto after = svc.list_dat_versions();
  ASSERT_TRUE(after.has_value());
  EXPECT_TRUE(after->empty());
}

TEST_F(RomulusServiceTest, PurgeDatabaseIsIdempotent) {
  romulus::service::RomulusService svc(db_path_);

  ASSERT_TRUE(svc.purge_database().has_value());
  ASSERT_TRUE(svc.purge_database().has_value());
}

// ── Admin — delete DAT ────────────────────────────────────────

TEST_F(RomulusServiceTest, DeleteDatReturnsErrorForUnknownId) {
  romulus::service::RomulusService svc(db_path_);
  auto result = svc.delete_dat(9999);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, romulus::core::ErrorCode::NotFound);
}

TEST_F(RomulusServiceTest, DeleteDatRemovesImportedDat) {
  romulus::service::RomulusService svc(db_path_);

  auto dat = svc.import_dat(k_FixturesDir / "sample.dat");
  ASSERT_TRUE(dat.has_value()) << dat.error().message;

  auto del = svc.delete_dat(dat->id);
  ASSERT_TRUE(del.has_value()) << del.error().message;

  auto versions = svc.list_dat_versions();
  ASSERT_TRUE(versions.has_value());
  EXPECT_TRUE(versions->empty());
}

// ── Scanned Directories ───────────────────────────────────────

TEST_F(RomulusServiceTest, AddScanDirectoryRegistersPath) {
  romulus::service::RomulusService svc(db_path_);

  auto result = svc.add_scan_directory("/roms/snes");
  ASSERT_TRUE(result.has_value()) << result.error().message;
  EXPECT_EQ(result->path, "/roms/snes");

  auto dirs = svc.get_scan_directories();
  ASSERT_TRUE(dirs.has_value());
  ASSERT_EQ(dirs->size(), 1u);
  EXPECT_EQ(dirs->front().path, "/roms/snes");
}

TEST_F(RomulusServiceTest, RemoveScanDirectoryDeletesRegistration) {
  romulus::service::RomulusService svc(db_path_);

  auto added = svc.add_scan_directory("/roms/gba");
  ASSERT_TRUE(added.has_value()) << added.error().message;

  auto removed = svc.remove_scan_directory(added->id);
  ASSERT_TRUE(removed.has_value()) << removed.error().message;

  auto dirs = svc.get_scan_directories();
  ASSERT_TRUE(dirs.has_value());
  EXPECT_TRUE(dirs->empty());
}

} // namespace
