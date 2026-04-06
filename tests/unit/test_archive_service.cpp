#include "romulus/scanner/archive_service.hpp"

#include <gtest/gtest.h>

namespace {

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

} // namespace
