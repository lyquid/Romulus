#include "romulus/scanner/archive_service.hpp"
#include "romulus/service/romulus_service.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <optional>

namespace {

const std::filesystem::path k_FixturesDir{ROMULUS_TEST_FIXTURES_DIR};
const std::filesystem::path k_RepoDatsDir{ROMULUS_REPO_DATS_DIR};

auto find_repo_dat() -> std::optional<std::filesystem::path> {
  if (!std::filesystem::exists(k_RepoDatsDir)) {
    return std::nullopt;
  }

  for (const auto& entry : std::filesystem::directory_iterator(k_RepoDatsDir)) {
    if (!entry.is_regular_file()) {
      continue;
    }

    const auto extension = entry.path().extension().string();
    if (extension == ".dat" || extension == ".xml" ||
        romulus::scanner::ArchiveService::is_archive(entry.path())) {
      return entry.path();
    }
  }

  return std::nullopt;
}

class DatUpdateTest : public ::testing::Test {
protected:
  void SetUp() override {
    db_path_ = std::filesystem::temp_directory_path() / "romulus_dat_update_test.db";
    std::filesystem::remove(db_path_);
  }

  void TearDown() override {
    std::filesystem::remove(db_path_);
    std::filesystem::remove(db_path_.string() + "-wal");
    std::filesystem::remove(db_path_.string() + "-shm");
  }

  std::filesystem::path db_path_;
};

TEST_F(DatUpdateTest, ImportingSameDatTwiceIsIdempotent) {
  romulus::service::RomulusService svc(db_path_);

  auto dat1 = svc.import_dat(k_FixturesDir / "sample.dat");
  ASSERT_TRUE(dat1.has_value()) << dat1.error().message;

  auto dat2 = svc.import_dat(k_FixturesDir / "sample.dat");
  ASSERT_TRUE(dat2.has_value()) << dat2.error().message;

  EXPECT_EQ(dat1->id, dat2->id);
  EXPECT_EQ(dat1->version, dat2->version);
}

TEST_F(DatUpdateTest, SystemCountDoesNotIncreaseOnReimport) {
  romulus::service::RomulusService svc(db_path_);

  auto dat1 = svc.import_dat(k_FixturesDir / "sample.dat");
  auto dat2 = svc.import_dat(k_FixturesDir / "sample.dat");

  auto systems = svc.list_systems();
  ASSERT_TRUE(systems.has_value());
  EXPECT_EQ(systems->size(), 1);
}

TEST_F(DatUpdateTest, RepoArchiveImportIsIdempotent) {
  const auto bundled_dat = find_repo_dat();
  ASSERT_TRUE(bundled_dat.has_value())
      << "Repository DAT artifact not found in " << k_RepoDatsDir.string();

  romulus::service::RomulusService svc(db_path_);

  auto dat1 = svc.import_dat(*bundled_dat);
  ASSERT_TRUE(dat1.has_value()) << dat1.error().message;

  auto dat2 = svc.import_dat(*bundled_dat);
  ASSERT_TRUE(dat2.has_value()) << dat2.error().message;

  EXPECT_EQ(dat1->id, dat2->id);
  EXPECT_EQ(dat1->version, dat2->version);
  EXPECT_FALSE(dat1->name.empty());
}

} // namespace
