#include "romulus/service/romulus_service.hpp"

#include <gtest/gtest.h>

#include <filesystem>

namespace {

const std::filesystem::path k_FixturesDir{ROMULUS_TEST_FIXTURES_DIR};

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

  static_cast<void>(svc.import_dat(k_FixturesDir / "sample.dat"));
  static_cast<void>(svc.import_dat(k_FixturesDir / "sample.dat"));

  auto systems = svc.list_systems();
  ASSERT_TRUE(systems.has_value());
  EXPECT_EQ(systems->size(), 1);
}

} // namespace
