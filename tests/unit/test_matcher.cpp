#include "romulus/core/types.hpp"
#include "romulus/database/database.hpp"
#include "romulus/engine/matcher.hpp"

#include <gtest/gtest.h>

#include <filesystem>

namespace {

class MatcherTest : public ::testing::Test {
protected:
  void SetUp() override {
    db_path_ = std::filesystem::temp_directory_path() / "romulus_matcher_test.db";
    std::filesystem::remove(db_path_);
    db_ = std::make_unique<romulus::database::Database>(db_path_);
    seed_data();
  }

  void TearDown() override {
    db_.reset();
    std::filesystem::remove(db_path_);
    std::filesystem::remove(db_path_.string() + "-wal");
    std::filesystem::remove(db_path_.string() + "-shm");
  }

  void seed_data() {
    romulus::core::DatVersion dat{.name = "Test",
                                  .version = "1.0",
                                  .source_url = {}, .dat_sha256 = "abc", .imported_at = {}};
    auto dat_id = db_->insert_dat_version(dat);
    ASSERT_TRUE(dat_id.has_value());

    auto game_id1 = db_->find_or_insert_game(*dat_id, "Test Game");
    ASSERT_TRUE(game_id1.has_value());

    // ROM 1: SHA1/MD5/CRC32 known but no SHA256 in DAT — all valid hex
    romulus::core::RomInfo rom{.game_id = *game_id1,
                               .name = "test.bin",
                               .size = 100,
                               .crc32 = "aabb0011",
                               .md5 = "aabb0011aabb0011aabb0011aabb0011",
                               .sha1 = "aabb0011aabb0011aabb0011aabb0011aabb0011",
                               .sha256 = {},
                               .region = {}};
    ASSERT_TRUE(db_->insert_rom(rom).has_value());

    auto game_id2 = db_->find_or_insert_game(*dat_id, "Enriched Game");
    ASSERT_TRUE(game_id2.has_value());

    // ROM 2: has a SHA256 in the DAT (e.g., enriched entry)
    romulus::core::RomInfo rom_enriched{
        .game_id = *game_id2,
        .name = "with_sha256.bin",
        .size = 200,
        .crc32 = "ccdd0022",
        .md5 = "ccdd0022ccdd0022ccdd0022ccdd0022",
        .sha1 = "ccdd0022ccdd0022ccdd0022ccdd0022ccdd0022",
        .sha256 = "ccdd0022ccdd0022ccdd0022ccdd0022ccdd0022ccdd0022ccdd0022ccdd0022",
        .region = {}};
    auto rom_enriched_id = db_->insert_rom(rom_enriched);
    ASSERT_TRUE(rom_enriched_id.has_value());
    rom_enriched_id_ = *rom_enriched_id;

    auto game_id3 = db_->find_or_insert_game(*dat_id, "Missing Game");
    ASSERT_TRUE(game_id3.has_value());

    // ROM 3: missing completely — no matching file will exist
    romulus::core::RomInfo rom_missing{.game_id = *game_id3,
                                       .name = "missing.bin",
                                       .size = 300,
                                       .crc32 = "dead0033",
                                       .md5 = "dead0033dead0033dead0033dead0033",
                                       .sha1 = "dead0033dead0033dead0033dead0033dead0033",
                                       .sha256 = {},
                                       .region = {}};
    ASSERT_TRUE(db_->insert_rom(rom_missing).has_value());

    // File 1: exact match against ROM 1 via SHA1+MD5+CRC32
    romulus::core::FileInfo file{
        .path = "/roms/test.bin",
        .archive_path = std::nullopt,
        .entry_name = std::nullopt,
        .size = 100,
        .crc32 = "aabb0011",
        .md5 = "aabb0011aabb0011aabb0011aabb0011",
        .sha1 = "aabb0011aabb0011aabb0011aabb0011aabb0011",
        .sha256 = "aabb0011aabb0011aabb0011aabb0011aabb0011aabb0011aabb0011aabb0011",
    };
    ASSERT_TRUE(db_->upsert_file(file).has_value());

    // File 2: matches ROM 2 only via SHA256 — lower hashes differ
    romulus::core::FileInfo file_sha256_only{
        .path = "/roms/sha256_only.bin",
        .archive_path = std::nullopt,
        .entry_name = std::nullopt,
        .size = 200,
        .crc32 = "ff110044",
        .md5 = "ff110044ff110044ff110044ff110044",
        .sha1 = "ff110044ff110044ff110044ff110044ff110044",
        .sha256 = "ccdd0022ccdd0022ccdd0022ccdd0022ccdd0022ccdd0022ccdd0022ccdd0022",
    };
    auto sha256_only_id = db_->upsert_file(file_sha256_only);
    ASSERT_TRUE(sha256_only_id.has_value());
    sha256_only_file_id_ = *sha256_only_id;

    // File 3: no match at all
    romulus::core::FileInfo other{
        .path = "/roms/unknown.bin",
        .archive_path = std::nullopt,
        .entry_name = std::nullopt,
        .size = 200,
        .crc32 = "00000000",
        .md5 = "00000000000000000000000000000000",
        .sha1 = "0000000000000000000000000000000000000000",
        .sha256 = "0000000000000000000000000000000000000000000000000000000000000000",
    };
    ASSERT_TRUE(db_->upsert_file(other).has_value());

    auto game_id4 = db_->find_or_insert_game(*dat_id, "Md5 Only Game");
    ASSERT_TRUE(game_id4.has_value());

    romulus::core::RomInfo rom_md5_only{.game_id = *game_id4,
                                        .name = "md5_only.bin",
                                        .size = 444,
                                        .crc32 = {},
                                        .md5 = "99887766998877669988776699887766",
                                        .sha1 = {},
                                        .sha256 = {},
                                        .region = {}};
    auto rom_md5_only_id = db_->insert_rom(rom_md5_only);
    ASSERT_TRUE(rom_md5_only_id.has_value());
    rom_md5_only_id_ = *rom_md5_only_id;

    md5_only_global_sha1_ = "11223344556677889900aabbccddeeff00112233";
    romulus::core::GlobalRom md5_only_global_rom{
        .sha1 = md5_only_global_sha1_,
        .sha256 = {},
        .md5 = "99887766998877669988776699887766",
        .crc32 = "99887766",
        .size = 444,
    };
    ASSERT_TRUE(db_->upsert_global_rom(md5_only_global_rom).has_value());
  }

  std::filesystem::path db_path_;
  std::unique_ptr<romulus::database::Database> db_;
  std::int64_t rom_enriched_id_ = 0;
  std::int64_t rom_md5_only_id_ = 0;
  std::int64_t sha256_only_file_id_ = 0;
  std::string md5_only_global_sha1_;
};

TEST_F(MatcherTest, MatchesExactByAllHashes) {
  auto results = romulus::engine::Matcher::match_all(*db_);
  ASSERT_TRUE(results.has_value()) << results.error().message;

  bool found_exact = false;
  for (const auto& r : *results) {
    if (r.match_type == romulus::core::MatchType::Exact) {
      found_exact = true;
    }
  }
  EXPECT_TRUE(found_exact);
}

TEST_F(MatcherTest, ReportsNoMatchForUnknownRom) {
  auto results = romulus::engine::Matcher::match_all(*db_);
  ASSERT_TRUE(results.has_value());

  bool found_no_match = false;
  for (const auto& r : *results) {
    if (r.match_type == romulus::core::MatchType::NoMatch) {
      found_no_match = true;
    }
  }
  EXPECT_TRUE(found_no_match);
}

TEST_F(MatcherTest, MatchesSha256OnlyWhenLowerHashesDiffer) {
  auto results = romulus::engine::Matcher::match_all(*db_);
  ASSERT_TRUE(results.has_value()) << results.error().message;

  // Locate the result for the file whose only match is via SHA256
  const romulus::core::MatchResult* sha256_match = nullptr;
  for (const auto& r : *results) {
    if (r.match_type == romulus::core::MatchType::Sha256Only) {
      sha256_match = &r;
      break;
    }
  }

  ASSERT_NE(sha256_match, nullptr) << "Expected a Sha256Only match result";
  // The matched file should be the sha256_only.bin file and point to rom_enriched
  auto file_info = db_->find_file_by_path("/roms/sha256_only.bin");
  ASSERT_TRUE(file_info.has_value());
  ASSERT_TRUE(file_info->has_value());
  EXPECT_EQ(sha256_match->global_rom_sha1, file_info->value().sha1);
  EXPECT_EQ(sha256_match->rom_id, rom_enriched_id_);
}

TEST_F(MatcherTest, MatchesMd5OnlyWhenOnlyMd5HashIsAvailable) {
  auto results = romulus::engine::Matcher::match_all(*db_);
  ASSERT_TRUE(results.has_value()) << results.error().message;

  const romulus::core::MatchResult* md5_match = nullptr;
  for (const auto& result : *results) {
    if (result.rom_id == rom_md5_only_id_) {
      md5_match = &result;
      break;
    }
  }

  ASSERT_NE(md5_match, nullptr) << "Expected md5-only ROM match result";
  EXPECT_EQ(md5_match->match_type, romulus::core::MatchType::Md5Only);
  EXPECT_EQ(md5_match->global_rom_sha1, md5_only_global_sha1_);
}

// ── SHA256 cross-validation in the Exact determination ────────────────────────

/// When a DAT ROM declares a SHA256 that disagrees with the scanned file's SHA256,
/// the SHA1 match should be downgraded from Exact to Sha1Only.
TEST_F(MatcherTest, Sha1MatchDegradesToSha1OnlyWhenSha256Disagrees) {
  std::filesystem::path db_path =
      std::filesystem::temp_directory_path() / "romulus_matcher_sha256_exact_test.db";
  std::filesystem::remove(db_path);
  romulus::database::Database db(db_path);

  romulus::core::DatVersion dat{
      .name = "Sha256ExactTest", .version = "1.0", .source_url = {}, .dat_sha256 = "x1",
      .imported_at = {}};
  auto dat_id = db.insert_dat_version(dat);
  ASSERT_TRUE(dat_id.has_value());

  auto game_id = db.find_or_insert_game(*dat_id, "G");
  ASSERT_TRUE(game_id.has_value());

  // DAT ROM: SHA1 matches file, but SHA256 in DAT disagrees with file's SHA256.
  const std::string sha1 = "aaaa0000aaaa0000aaaa0000aaaa0000aaaa0000";
  const std::string dat_sha256 = "dddd0000dddd0000dddd0000dddd0000dddd0000dddd0000dddd0000dddd0000";
  const std::string file_sha256 = "eeee1111eeee1111eeee1111eeee1111eeee1111eeee1111eeee1111eeee1111";

  romulus::core::RomInfo rom{.game_id = *game_id,
                             .name = "mismatch_sha256.bin",
                             .size = 512,
                             .crc32 = "aabb1122",
                             .md5 = "aabb1122aabb1122aabb1122aabb1122",
                             .sha1 = sha1,
                             .sha256 = dat_sha256, // DAT declares this SHA256
                             .region = {}};
  auto rom_id = db.insert_rom(rom);
  ASSERT_TRUE(rom_id.has_value());

  // GlobalRom/File: SHA1 matches but SHA256 is different from what the DAT says.
  romulus::core::FileInfo file{
      .path = "/roms/mismatch_sha256.bin",
      .archive_path = std::nullopt,
      .entry_name = std::nullopt,
      .size = 512,
      .crc32 = "aabb1122",
      .md5 = "aabb1122aabb1122aabb1122aabb1122",
      .sha1 = sha1,
      .sha256 = file_sha256, // file has a different SHA256 than the DAT expects
  };
  ASSERT_TRUE(db.upsert_file(file).has_value());

  auto results = romulus::engine::Matcher::match_all(db);
  ASSERT_TRUE(results.has_value()) << results.error().message;

  ASSERT_EQ(results->size(), 1u);
  // SHA1 matched but DAT SHA256 ≠ file SHA256 → should be Sha1Only, not Exact
  EXPECT_EQ((*results)[0].match_type, romulus::core::MatchType::Sha1Only);

  std::filesystem::remove(db_path);
  std::filesystem::remove(db_path.string() + "-wal");
  std::filesystem::remove(db_path.string() + "-shm");
}

/// When a DAT ROM declares a SHA256 that agrees with the scanned file's SHA256,
/// and SHA1 also matches, the result must be Exact.
TEST_F(MatcherTest, Sha1MatchIsExactWhenSha256AlsoAgrees) {
  std::filesystem::path db_path =
      std::filesystem::temp_directory_path() / "romulus_matcher_sha256_exact2_test.db";
  std::filesystem::remove(db_path);
  romulus::database::Database db(db_path);

  romulus::core::DatVersion dat{
      .name = "Sha256ExactTest2", .version = "1.0", .source_url = {}, .dat_sha256 = "x2",
      .imported_at = {}};
  auto dat_id = db.insert_dat_version(dat);
  ASSERT_TRUE(dat_id.has_value());

  auto game_id = db.find_or_insert_game(*dat_id, "G");
  ASSERT_TRUE(game_id.has_value());

  const std::string sha1 = "bbbb1111bbbb1111bbbb1111bbbb1111bbbb1111";
  const std::string sha256 = "cccc2222cccc2222cccc2222cccc2222cccc2222cccc2222cccc2222cccc2222";

  romulus::core::RomInfo rom{.game_id = *game_id,
                             .name = "full_match.bin",
                             .size = 256,
                             .crc32 = "bbcc1122",
                             .md5 = "bbcc1122bbcc1122bbcc1122bbcc1122",
                             .sha1 = sha1,
                             .sha256 = sha256,
                             .region = {}};
  ASSERT_TRUE(db.insert_rom(rom).has_value());

  romulus::core::FileInfo file{
      .path = "/roms/full_match.bin",
      .archive_path = std::nullopt,
      .entry_name = std::nullopt,
      .size = 256,
      .crc32 = "bbcc1122",
      .md5 = "bbcc1122bbcc1122bbcc1122bbcc1122",
      .sha1 = sha1,
      .sha256 = sha256, // matches DAT SHA256
  };
  ASSERT_TRUE(db.upsert_file(file).has_value());

  auto results = romulus::engine::Matcher::match_all(db);
  ASSERT_TRUE(results.has_value()) << results.error().message;

  ASSERT_EQ(results->size(), 1u);
  EXPECT_EQ((*results)[0].match_type, romulus::core::MatchType::Exact);

  std::filesystem::remove(db_path);
  std::filesystem::remove(db_path.string() + "-wal");
  std::filesystem::remove(db_path.string() + "-shm");
}

// ── SHA256-led match cross-validation ─────────────────────────────────────────

/// When a DAT ROM has no SHA1 but has a SHA256 that matches a GlobalRom, and all other
/// available hashes (MD5, CRC32) also agree, the match must be classified as Exact.
TEST_F(MatcherTest, Sha256LeadMatchIsExactWhenAllHashesAgree) {
  std::filesystem::path db_path =
      std::filesystem::temp_directory_path() / "romulus_matcher_sha256_lead_exact_test.db";
  std::filesystem::remove(db_path);
  romulus::database::Database db(db_path);

  romulus::core::DatVersion dat{
      .name = "Sha256LeadExact", .version = "1.0", .source_url = {}, .dat_sha256 = "z1",
      .imported_at = {}};
  auto dat_id = db.insert_dat_version(dat);
  ASSERT_TRUE(dat_id.has_value());

  auto game_id = db.find_or_insert_game(*dat_id, "G");
  ASSERT_TRUE(game_id.has_value());

  // DAT ROM: no SHA1, but has SHA256 + MD5 + CRC32 (enriched DAT entry)
  const std::string sha256 = "1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef";
  const std::string sha1 = "1234567890abcdef1234567890abcdef12345678";

  romulus::core::RomInfo rom{.game_id = *game_id,
                             .name = "sha256_lead.bin",
                             .size = 256,
                             .crc32 = "12345678",
                             .md5 = "1234567890abcdef1234567890abcdef",
                             .sha1 = {},     // DAT has no SHA1
                             .sha256 = sha256,
                             .region = {}};
  ASSERT_TRUE(db.insert_rom(rom).has_value());

  // File has all hashes, including SHA256 matching the DAT
  romulus::core::FileInfo file{
      .path = "/roms/sha256_lead.bin",
      .archive_path = std::nullopt,
      .entry_name = std::nullopt,
      .size = 256,
      .crc32 = "12345678",
      .md5 = "1234567890abcdef1234567890abcdef",
      .sha1 = sha1,
      .sha256 = sha256,
  };
  ASSERT_TRUE(db.upsert_file(file).has_value());

  auto results = romulus::engine::Matcher::match_all(db);
  ASSERT_TRUE(results.has_value()) << results.error().message;

  ASSERT_EQ(results->size(), 1u);
  // SHA256 led the match, and CRC32+MD5 also agree → Exact
  EXPECT_EQ((*results)[0].match_type, romulus::core::MatchType::Exact);

  std::filesystem::remove(db_path);
  std::filesystem::remove(db_path.string() + "-wal");
  std::filesystem::remove(db_path.string() + "-shm");
}

/// When a DAT ROM has no SHA1 but has a SHA256 that matches, and a lower hash (CRC32)
/// disagrees, the match must remain Sha256Only — not Exact.
TEST_F(MatcherTest, Sha256LeadMatchIsSha256OnlyWhenLowerHashDisagrees) {
  std::filesystem::path db_path =
      std::filesystem::temp_directory_path() / "romulus_matcher_sha256_lead_partial_test.db";
  std::filesystem::remove(db_path);
  romulus::database::Database db(db_path);

  romulus::core::DatVersion dat{
      .name = "Sha256LeadPartial", .version = "1.0", .source_url = {}, .dat_sha256 = "z2",
      .imported_at = {}};
  auto dat_id = db.insert_dat_version(dat);
  ASSERT_TRUE(dat_id.has_value());

  auto game_id = db.find_or_insert_game(*dat_id, "G");
  ASSERT_TRUE(game_id.has_value());

  const std::string sha256 = "abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890";
  const std::string sha1 = "abcdef1234567890abcdef1234567890abcdef12";

  // DAT ROM: no SHA1, SHA256 matches file, but CRC32 in DAT disagrees with file CRC32
  romulus::core::RomInfo rom{.game_id = *game_id,
                             .name = "sha256_partial.bin",
                             .size = 128,
                             .crc32 = "aabbccdd", // DAT CRC32 differs from file CRC32
                             .md5 = {},
                             .sha1 = {},
                             .sha256 = sha256,
                             .region = {}};
  ASSERT_TRUE(db.insert_rom(rom).has_value());

  romulus::core::FileInfo file{
      .path = "/roms/sha256_partial.bin",
      .archive_path = std::nullopt,
      .entry_name = std::nullopt,
      .size = 128,
      .crc32 = "11223344", // different CRC32 from what the DAT declares
      .md5 = {},
      .sha1 = sha1,
      .sha256 = sha256, // SHA256 matches DAT
  };
  ASSERT_TRUE(db.upsert_file(file).has_value());

  auto results = romulus::engine::Matcher::match_all(db);
  ASSERT_TRUE(results.has_value()) << results.error().message;

  ASSERT_EQ(results->size(), 1u);
  // SHA256 matched but CRC32 disagrees → Sha256Only, not Exact
  EXPECT_EQ((*results)[0].match_type, romulus::core::MatchType::Sha256Only);

  std::filesystem::remove(db_path);
  std::filesystem::remove(db_path.string() + "-wal");
  std::filesystem::remove(db_path.string() + "-shm");
}

/// When two CRC32 candidates have no SHA256 cross-match, prefer the one backed by a
/// bare (non-archive) file on disk over one backed only by an archive entry.
TEST_F(MatcherTest, Crc32TiebreakerPrefersNonArchiveFile) {
  std::filesystem::path db_path =
      std::filesystem::temp_directory_path() / "romulus_matcher_crc32_bare_test.db";
  std::filesystem::remove(db_path);
  romulus::database::Database db(db_path);

  romulus::core::DatVersion dat{
      .name = "CRC32BareTBTest", .version = "1.0", .source_url = {}, .dat_sha256 = "y2",
      .imported_at = {}};
  auto dat_id = db.insert_dat_version(dat);
  ASSERT_TRUE(dat_id.has_value());

  auto game_id = db.find_or_insert_game(*dat_id, "G");
  ASSERT_TRUE(game_id.has_value());

  const std::string shared_crc32 = "cafebabe";
  const std::string sha1_archive = "a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1a1";
  const std::string sha1_bare = "b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2b2";

  // DAT ROM: only CRC32, no SHA256.
  romulus::core::RomInfo rom{.game_id = *game_id,
                             .name = "bare_wins.bin",
                             .size = 64,
                             .crc32 = shared_crc32,
                             .md5 = {},
                             .sha1 = {},
                             .sha256 = {},
                             .region = {}};
  ASSERT_TRUE(db.insert_rom(rom).has_value());

  // global_rom_archive: backed only by an archive entry
  romulus::core::GlobalRom global_rom_archive{
      .sha1 = sha1_archive, .sha256 = {}, .md5 = {}, .crc32 = shared_crc32, .size = 64};
  // global_rom_bare: backed by a bare file
  romulus::core::GlobalRom global_rom_bare{
      .sha1 = sha1_bare, .sha256 = {}, .md5 = {}, .crc32 = shared_crc32, .size = 64};
  ASSERT_TRUE(db.upsert_global_rom(global_rom_archive).has_value());
  ASSERT_TRUE(db.upsert_global_rom(global_rom_bare).has_value());

  // File for archive candidate: entry_name is set → archive entry
  romulus::core::FileInfo archive_file{
      .path = "/roms/collection.zip::bare_wins.bin",
      .archive_path = "/roms/collection.zip",
      .entry_name = "bare_wins.bin",
      .size = 64,
      .crc32 = shared_crc32,
      .md5 = {},
      .sha1 = sha1_archive,
      .sha256 = {},
  };
  ASSERT_TRUE(db.upsert_file(archive_file).has_value());

  // File for bare candidate: no entry_name → bare file
  romulus::core::FileInfo bare_file{
      .path = "/roms/bare_wins.bin",
      .archive_path = std::nullopt,
      .entry_name = std::nullopt,
      .size = 64,
      .crc32 = shared_crc32,
      .md5 = {},
      .sha1 = sha1_bare,
      .sha256 = {},
  };
  ASSERT_TRUE(db.upsert_file(bare_file).has_value());

  auto results = romulus::engine::Matcher::match_all(db);
  ASSERT_TRUE(results.has_value()) << results.error().message;

  ASSERT_EQ(results->size(), 1u);
  EXPECT_EQ((*results)[0].match_type, romulus::core::MatchType::Crc32Only);
  // Bare file should win over the archive entry.
  EXPECT_EQ((*results)[0].global_rom_sha1, sha1_bare);

  std::filesystem::remove(db_path);
  std::filesystem::remove(db_path.string() + "-wal");
  std::filesystem::remove(db_path.string() + "-shm");
}

} // namespace
