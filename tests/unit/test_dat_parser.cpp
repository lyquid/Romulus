#include "romulus/dat/dat_parser.hpp"
#include "romulus/scanner/archive_service.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
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

TEST(DatParser, ParsesValidLogiqxXml) {
  romulus::dat::DatParser parser;
  auto result = parser.parse(k_FixturesDir / "sample.dat");

  ASSERT_TRUE(result.has_value()) << result.error().message;

  const auto& dat = result.value();
  EXPECT_EQ(dat.header.name, "Test System - Sample");
  EXPECT_EQ(dat.header.version, "20240101-000000");
  EXPECT_EQ(dat.games.size(), 3);
}

TEST(DatParser, ParsesGameNamesCorrectly) {
  romulus::dat::DatParser parser;
  auto result = parser.parse(k_FixturesDir / "sample.dat");
  ASSERT_TRUE(result.has_value());

  EXPECT_EQ(result->games[0].name, "Test Game Alpha (World)");
  EXPECT_EQ(result->games[1].name, "Test Game Beta (USA)");
  EXPECT_EQ(result->games[2].name, "Test Game Gamma (Europe)");
}

TEST(DatParser, ParsesRomHashesCorrectly) {
  romulus::dat::DatParser parser;
  auto result = parser.parse(k_FixturesDir / "sample.dat");
  ASSERT_TRUE(result.has_value());

  const auto& rom = result->games[0].roms[0];
  EXPECT_EQ(rom.name, "Test Game Alpha (World).bin");
  EXPECT_EQ(rom.size, 1024);
  EXPECT_EQ(rom.crc32, "d87f7e0c"); // Normalized to lowercase
  EXPECT_EQ(rom.sha1, "6367c48dd193d56ea7b0baad25b19455e529f5ee");
}

TEST(DatParser, ExtractsRegionFromGameName) {
  romulus::dat::DatParser parser;
  auto result = parser.parse(k_FixturesDir / "sample.dat");
  ASSERT_TRUE(result.has_value());

  EXPECT_EQ(result->games[0].roms[0].region, "World");
  EXPECT_EQ(result->games[1].roms[0].region, "USA");
  EXPECT_EQ(result->games[2].roms[0].region, "Europe");
}

TEST(DatParser, ReturnsErrorForMissingFile) {
  romulus::dat::DatParser parser;
  auto result = parser.parse("nonexistent.dat");
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, romulus::core::ErrorCode::DatParseError);
}

TEST(DatParser, ReturnsErrorForMalformedXml) {
  // Create a temp file with invalid XML
  auto temp = std::filesystem::temp_directory_path() / "romulus_test_bad.dat";
  {
    std::ofstream f(temp);
    f << "<not-a-datafile><garbage/></not-a-datafile>";
  }

  romulus::dat::DatParser parser;
  auto result = parser.parse(temp);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, romulus::core::ErrorCode::DatInvalidFormat);

  std::filesystem::remove(temp);
}

TEST(DatParser, ParsesRepoArchiveDat) {
  const auto bundled_dat = find_repo_dat();
  ASSERT_TRUE(bundled_dat.has_value()) << "Repository DAT artifact not found in "
                                       << k_RepoDatsDir.string();

  romulus::dat::DatParser parser;
  auto result = parser.parse(*bundled_dat);
  ASSERT_TRUE(result.has_value()) << result.error().message;

  EXPECT_FALSE(result->header.name.empty());
  EXPECT_FALSE(result->header.version.empty());
  EXPECT_FALSE(result->games.empty());
}

} // namespace
