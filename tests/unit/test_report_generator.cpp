#include "romulus/core/types.hpp"
#include "romulus/database/database.hpp"
#include "romulus/report/report_generator.hpp"

#include <gtest/gtest.h>

#include <filesystem>

namespace {

class ReportTest : public ::testing::Test {
protected:
  void SetUp() override {
    db_path_ = std::filesystem::temp_directory_path() / "romulus_report_test.db";
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

TEST_F(ReportTest, SummaryTextFormatContainsROMULUSHeader) {
  auto result = romulus::report::ReportGenerator::generate(
    *db_,
    romulus::core::ReportType::Summary,
    romulus::core::ReportFormat::Text);

  ASSERT_TRUE(result.has_value()) << result.error().message;
  EXPECT_NE(result->find("ROMULUS"), std::string::npos);
}

TEST_F(ReportTest, SummaryJsonIsValidJson) {
  auto result = romulus::report::ReportGenerator::generate(
    *db_,
    romulus::core::ReportType::Summary,
    romulus::core::ReportFormat::Json);

  ASSERT_TRUE(result.has_value()) << result.error().message;
  // Should contain expected JSON keys
  EXPECT_NE(result->find("\"total_roms\""), std::string::npos);
  EXPECT_NE(result->find("\"verified\""), std::string::npos);
}

TEST_F(ReportTest, SummaryCsvHasHeaderRow) {
  auto result = romulus::report::ReportGenerator::generate(
    *db_,
    romulus::core::ReportType::Summary,
    romulus::core::ReportFormat::Csv);

  ASSERT_TRUE(result.has_value()) << result.error().message;
  EXPECT_NE(result->find("system,total_roms"), std::string::npos);
}

} // namespace
