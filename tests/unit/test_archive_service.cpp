#include "romulus/scanner/archive_service.hpp"

#include <gtest/gtest.h>

#include <filesystem>

namespace {

const std::filesystem::path k_FixturesDir{ROMULUS_TEST_FIXTURES_DIR};

// ── is_archive() — extension detection ─────────────────────────────────────

TEST(ArchiveService, recognises_zip_extension) {
  EXPECT_TRUE(romulus::scanner::ArchiveService::is_archive("game.zip"));
}

TEST(ArchiveService, recognises_7z_extension) {
  EXPECT_TRUE(romulus::scanner::ArchiveService::is_archive("game.7z"));
}

TEST(ArchiveService, recognises_tar_extension) {
  EXPECT_TRUE(romulus::scanner::ArchiveService::is_archive("archive.tar"));
}

TEST(ArchiveService, recognises_tar_gz_double_extension) {
  EXPECT_TRUE(romulus::scanner::ArchiveService::is_archive("archive.tar.gz"));
}

TEST(ArchiveService, recognises_tar_bz2_double_extension) {
  EXPECT_TRUE(romulus::scanner::ArchiveService::is_archive("archive.tar.bz2"));
}

TEST(ArchiveService, recognises_tar_xz_double_extension) {
  EXPECT_TRUE(romulus::scanner::ArchiveService::is_archive("archive.tar.xz"));
}

TEST(ArchiveService, recognises_tgz_extension) {
  EXPECT_TRUE(romulus::scanner::ArchiveService::is_archive("archive.tgz"));
}

TEST(ArchiveService, rejects_nes_extension) {
  EXPECT_FALSE(romulus::scanner::ArchiveService::is_archive("game.nes"));
}

TEST(ArchiveService, rejects_bin_extension) {
  EXPECT_FALSE(romulus::scanner::ArchiveService::is_archive("game.bin"));
}

// ── Regression: filenames with dots inside parentheses (e.g. version strings)
// Previously, "(v1.1)" caused is_archive() to build ".1).zip" as the combined
// extension, which is not in the archive list → returned false for ZIP files.

TEST(ArchiveService, zip_with_version_in_parens_is_recognised_as_archive) {
  EXPECT_TRUE(romulus::scanner::ArchiveService::is_archive("Game Title (v1.1).zip"));
}

TEST(ArchiveService, zip_with_complex_no_intro_name_is_recognised_as_archive) {
  EXPECT_TRUE(romulus::scanner::ArchiveService::is_archive(
      "Ai Senshi Nicol (Asia) (Ja) (v1.1) (Kaiser) (KS-7050) (Pirate).zip"));
}

TEST(ArchiveService, zip_with_multiple_dots_in_name_is_recognised_as_archive) {
  EXPECT_TRUE(romulus::scanner::ArchiveService::is_archive("Some.Game.With.Dots.zip"));
}

TEST(ArchiveService, case_insensitive_ZIP_extension) {
  EXPECT_TRUE(romulus::scanner::ArchiveService::is_archive("Game (v1.0).ZIP"));
}

// ── list_entries() — index-based entry identification ──────────────────────

TEST(ArchiveService, list_entries_populates_index) {
  auto result = romulus::scanner::ArchiveService::list_entries(k_FixturesDir / "test_archive.zip");
  ASSERT_TRUE(result.has_value()) << result.error().message;
  ASSERT_EQ(result->size(), 2);

  // Indices must be distinct and stable (0-based sequential position)
  EXPECT_EQ(result->at(0).index, 0);
  EXPECT_EQ(result->at(1).index, 1);
}

TEST(ArchiveService, list_entries_preserves_names_for_display) {
  auto result = romulus::scanner::ArchiveService::list_entries(k_FixturesDir / "test_archive.zip");
  ASSERT_TRUE(result.has_value()) << result.error().message;
  ASSERT_EQ(result->size(), 2);

  EXPECT_EQ(result->at(0).name, "alpha.bin");
  EXPECT_EQ(result->at(1).name, "beta.bin");
}

// Directory entries (and other non-regular headers) are skipped in list_entries
// but still counted in the index, so file indices reflect their true header position.
TEST(ArchiveService, list_entries_index_accounts_for_directory_headers) {
  // Fixture structure: [0]=subdir/ (directory header, skipped), [1]=subdir/gamma.bin,
  // [2]=subdir/delta.bin
  auto result =
      romulus::scanner::ArchiveService::list_entries(k_FixturesDir / "test_archive_with_dir.zip");
  ASSERT_TRUE(result.has_value()) << result.error().message;
  // Only regular files are returned
  ASSERT_EQ(result->size(), 2);

  // Index 0 (the directory header) must be skipped; first file is at header position 1
  EXPECT_EQ(result->at(0).index, 1);
  EXPECT_EQ(result->at(0).name, "subdir/gamma.bin");
  EXPECT_EQ(result->at(1).index, 2);
  EXPECT_EQ(result->at(1).name, "subdir/delta.bin");
}

// ── stream_entry() — index-based streaming ─────────────────────────────────

TEST(ArchiveService, stream_entry_by_index_returns_correct_data) {
  auto entries = romulus::scanner::ArchiveService::list_entries(k_FixturesDir / "test_archive.zip");
  ASSERT_TRUE(entries.has_value()) << entries.error().message;
  ASSERT_EQ(entries->size(), 2);

  std::string data0;
  auto r0 = romulus::scanner::ArchiveService::stream_entry(
      k_FixturesDir / "test_archive.zip",
      entries->at(0).index,
      [&data0](const std::byte* d, std::size_t s) {
        data0.append(reinterpret_cast<const char*>(d), s);
      });
  ASSERT_TRUE(r0.has_value()) << r0.error().message;
  EXPECT_EQ(data0, "Hello");

  std::string data1;
  auto r1 = romulus::scanner::ArchiveService::stream_entry(
      k_FixturesDir / "test_archive.zip",
      entries->at(1).index,
      [&data1](const std::byte* d, std::size_t s) {
        data1.append(reinterpret_cast<const char*>(d), s);
      });
  ASSERT_TRUE(r1.has_value()) << r1.error().message;
  EXPECT_EQ(data1, "World");
}

// stream_entry resolves by full header position, so file index 1 (past the directory) works.
TEST(ArchiveService, stream_entry_resolves_file_past_directory_by_index) {
  auto entries =
      romulus::scanner::ArchiveService::list_entries(k_FixturesDir / "test_archive_with_dir.zip");
  ASSERT_TRUE(entries.has_value()) << entries.error().message;
  ASSERT_EQ(entries->size(), 2);

  std::string data;
  auto result = romulus::scanner::ArchiveService::stream_entry(
      k_FixturesDir / "test_archive_with_dir.zip",
      entries->at(0).index, // index == 1 (header 0 was the dir)
      [&data](const std::byte* d, std::size_t s) {
        data.append(reinterpret_cast<const char*>(d), s);
      });
  ASSERT_TRUE(result.has_value()) << result.error().message;
  EXPECT_EQ(data, "Gamma");
}

TEST(ArchiveService, stream_entry_returns_error_for_non_regular_file_index) {
  // Index 0 in test_archive_with_dir.zip is a directory entry — streaming it must fail.
  auto result = romulus::scanner::ArchiveService::stream_entry(
      k_FixturesDir / "test_archive_with_dir.zip", 0, [](const std::byte*, std::size_t) {});
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, romulus::core::ErrorCode::ArchiveReadError);
}

TEST(ArchiveService, stream_entry_returns_error_for_out_of_range_index) {
  auto result = romulus::scanner::ArchiveService::stream_entry(
      k_FixturesDir / "test_archive.zip", 999, [](const std::byte*, std::size_t) {});
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, romulus::core::ErrorCode::ArchiveReadError);
}

} // namespace
