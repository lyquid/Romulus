#include "romulus/database/database.hpp"
#include "romulus/engine/dat_auditor.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <utility>

namespace {

using romulus::core::DatAudit;
using romulus::core::DatAuditStatus;

class DatAuditorTest : public ::testing::Test {
protected:
  void SetUp() override {
    db_path_ = std::filesystem::temp_directory_path() / "romulus_dat_auditor_test.db";
    std::filesystem::remove(db_path_);
    db_ = std::make_unique<romulus::database::Database>(db_path_);
  }

  void TearDown() override {
    db_.reset();
    std::filesystem::remove(db_path_);
    std::filesystem::remove(db_path_.string() + "-wal");
    std::filesystem::remove(db_path_.string() + "-shm");
  }

  std::int64_t add_dat(std::string name, std::string checksum) {
    auto id = db_->insert_dat_version({.name = std::move(name),
                                       .version = "1.0",
                                       .source_url = {},
                                       .dat_sha256 = std::move(checksum),
                                       .imported_at = {}});
    EXPECT_TRUE(id.has_value());
    return id.value_or(0);
  }

  std::int64_t add_rom(std::int64_t dat_id,
                       std::string name,
                       std::string sha1,
                       std::string game_name = "Game") {
    auto game_id = db_->find_or_insert_game(dat_id, game_name);
    EXPECT_TRUE(game_id.has_value());
    auto rom_id = db_->insert_rom({.game_id = game_id.value_or(0),
                                   .name = std::move(name),
                                   .size = 100,
                                   .crc32 = {},
                                   .md5 = {},
                                   .sha1 = std::move(sha1),
                                   .sha256 = {},
                                   .region = {}});
    EXPECT_TRUE(rom_id.has_value());
    return rom_id.value_or(0);
  }

  void add_file(std::string path,
                const std::string& sha1,
                std::optional<std::string> archive_path = {},
                std::optional<std::string> entry_name = {}) {
    ASSERT_TRUE(db_->upsert_file({.path = std::move(path),
                                  .archive_path = std::move(archive_path),
                                  .entry_name = std::move(entry_name),
                                  .size = 100,
                                  .crc32 = {},
                                  .md5 = {},
                                  .sha1 = sha1,
                                  .sha256 = {}})
                    .has_value());
  }

  void add_match(std::int64_t rom_id,
                 const std::string& sha1,
                 romulus::core::MatchType type = romulus::core::MatchType::Exact) {
    ASSERT_TRUE(
        db_->insert_rom_match({.rom_id = rom_id, .global_rom_sha1 = sha1, .match_type = type})
            .has_value());
  }

  DatAudit audit(std::int64_t dat_id) {
    auto result = romulus::engine::DatAuditor::audit(*db_, dat_id);
    if (!result) {
      ADD_FAILURE() << result.error().message;
      return {};
    }
    return result.value_or(DatAudit{});
  }

  static const romulus::core::DatAuditRow* find_row(const DatAudit& audit, DatAuditStatus status) {
    const auto it = std::ranges::find_if(
        audit.rows, [status](const auto& row) { return row.status == status; });
    return it == audit.rows.end() ? nullptr : &*it;
  }

  std::filesystem::path db_path_;
  std::unique_ptr<romulus::database::Database> db_;
};

TEST_F(DatAuditorTest, ExactContentAndCanonicalNameAreCorrect) {
  const auto dat_id = add_dat("Selected", "audit-correct");
  const std::string sha1(40, '1');
  const auto rom_id = add_rom(dat_id, "canonical.bin", sha1);
  add_file("/roms/canonical.bin", sha1);
  add_match(rom_id, sha1);

  const auto result = audit(dat_id);

  EXPECT_EQ(result.summary.correct, 1);
  EXPECT_EQ(result.summary.wrong_name, 0);
  const auto* row = find_row(result, DatAuditStatus::Correct);
  ASSERT_NE(row, nullptr);
  EXPECT_EQ(row->actual_name, "canonical.bin");
  EXPECT_FALSE(row->reason.empty());
}

TEST_F(DatAuditorTest, ExactContentWithWrongNameIsRenameCandidate) {
  const auto dat_id = add_dat("Selected", "audit-wrong-name");
  const std::string sha1(40, '2');
  const auto rom_id = add_rom(dat_id, "canonical.bin", sha1);
  add_file("/roms/messy-name.bin", sha1);
  add_match(rom_id, sha1);

  const auto result = audit(dat_id);

  EXPECT_EQ(result.summary.wrong_name, 1);
  EXPECT_EQ(result.summary.correct, 0);
  const auto* row = find_row(result, DatAuditStatus::WrongCanonicalName);
  ASSERT_NE(row, nullptr);
  EXPECT_EQ(row->actual_name, "messy-name.bin");
  EXPECT_NE(row->reason.find("Exact content match"), std::string::npos);
  EXPECT_NE(row->reason.find("canonical.bin"), std::string::npos);
  EXPECT_NE(row->suggested_action.find("operation planner"), std::string::npos);
}

TEST_F(DatAuditorTest, MissingRomExplainsAbsentContent) {
  const auto dat_id = add_dat("Selected", "audit-missing");
  add_rom(dat_id, "missing.bin", std::string(40, '3'));

  const auto result = audit(dat_id);

  EXPECT_EQ(result.summary.missing, 1);
  const auto* row = find_row(result, DatAuditStatus::Missing);
  ASSERT_NE(row, nullptr);
  EXPECT_TRUE(row->file_path.empty());
  EXPECT_NE(row->reason.find("No scanned content"), std::string::npos);
}

TEST_F(DatAuditorTest, GloballyUnknownFileIsExtraRelativeToSelectedDat) {
  const auto dat_id = add_dat("Selected", "audit-unknown-extra");
  add_file("/roms/mystery.bin", std::string(40, '4'));

  const auto result = audit(dat_id);

  EXPECT_EQ(result.summary.extra, 1);
  EXPECT_EQ(result.summary.globally_unknown, 1);
  EXPECT_EQ(result.summary.extra_known_other_dat, 0);
  const auto* row = find_row(result, DatAuditStatus::ExtraUnknown);
  ASSERT_NE(row, nullptr);
  EXPECT_NE(row->reason.find("globally unknown"), std::string::npos);
}

TEST_F(DatAuditorTest, FileMatchingAnotherDatIsNotCalledGloballyUnknown) {
  const auto selected_dat_id = add_dat("Selected", "audit-selected");
  const auto other_dat_id = add_dat("Other DAT", "audit-other");
  const std::string sha1(40, '5');
  const auto other_rom_id = add_rom(other_dat_id, "known.bin", sha1);
  add_file("/roms/known.bin", sha1);
  add_match(other_rom_id, sha1);

  const auto result = audit(selected_dat_id);

  EXPECT_EQ(result.summary.extra, 1);
  EXPECT_EQ(result.summary.extra_known_other_dat, 1);
  EXPECT_EQ(result.summary.globally_unknown, 0);
  const auto* row = find_row(result, DatAuditStatus::ExtraKnownOtherDat);
  ASSERT_NE(row, nullptr);
  EXPECT_NE(row->reason.find("Other DAT v1.0"), std::string::npos);
  EXPECT_EQ(find_row(result, DatAuditStatus::ExtraUnknown), nullptr);
}

TEST_F(DatAuditorTest, DuplicateMatchedContentReportsEveryPhysicalCopy) {
  const auto dat_id = add_dat("Selected", "audit-duplicates");
  const std::string sha1(40, '6');
  const auto rom_id = add_rom(dat_id, "canonical.bin", sha1);
  add_file("/a/canonical.bin", sha1);
  add_file("/b/canonical.bin", sha1);
  add_match(rom_id, sha1);

  const auto result = audit(dat_id);

  EXPECT_EQ(result.summary.correct, 1);
  EXPECT_EQ(result.summary.duplicate_files, 2);
  EXPECT_EQ(
      std::ranges::count_if(
          result.rows, [](const auto& row) { return row.status == DatAuditStatus::Duplicate; }),
      2);
  const auto* row = find_row(result, DatAuditStatus::Duplicate);
  ASSERT_NE(row, nullptr);
  EXPECT_NE(row->reason.find("2 physical locations"), std::string::npos);
}

TEST_F(DatAuditorTest, ArchiveEntryLeafNameIsComparedWithCanonicalName) {
  const auto dat_id = add_dat("Selected", "audit-archive-name");
  const std::string sha1(40, '7');
  const auto rom_id = add_rom(dat_id, "canonical.bin", sha1);
  add_file("/roms/set.zip::nested/canonical.bin", sha1, "/roms/set.zip", "nested/canonical.bin");
  add_match(rom_id, sha1);

  const auto result = audit(dat_id);

  EXPECT_EQ(result.summary.correct, 1);
  EXPECT_EQ(result.summary.wrong_name, 0);
  const auto* row = find_row(result, DatAuditStatus::Correct);
  ASSERT_NE(row, nullptr);
  EXPECT_EQ(row->actual_name, "canonical.bin");
  EXPECT_EQ(row->file_path, "/roms/set.zip::nested/canonical.bin");
}

TEST_F(DatAuditorTest, WeakMatchDoesNotTriggerBadNamePolicy) {
  const auto dat_id = add_dat("Selected", "audit-weak-name");
  const std::string sha1(40, '8');
  const auto rom_id = add_rom(dat_id, "canonical.bin", std::string(40, '9'));
  add_file("/roms/wrong.bin", sha1);
  add_match(rom_id, sha1, romulus::core::MatchType::Crc32Only);

  const auto result = audit(dat_id);

  EXPECT_EQ(result.summary.crc_match, 1);
  EXPECT_EQ(result.summary.wrong_name, 0);
  const auto* row = find_row(result, DatAuditStatus::CrcMatch);
  ASSERT_NE(row, nullptr);
  EXPECT_NE(row->reason.find("weak evidence"), std::string::npos);
}

TEST_F(DatAuditorTest, Md5OnlyExplanationDoesNotClaimHashesDisagree) {
  const auto dat_id = add_dat("Selected", "audit-md5-only-reason");
  const std::string sha1(40, '9');
  const auto rom_id = add_rom(dat_id, "canonical.bin", {});
  add_file("/roms/md5-only.bin", sha1);
  add_match(rom_id, sha1, romulus::core::MatchType::Md5Only);

  const auto result = audit(dat_id);

  EXPECT_EQ(result.summary.md5_match, 1);
  const auto* row = find_row(result, DatAuditStatus::Md5Match);
  ASSERT_NE(row, nullptr);
  EXPECT_NE(row->reason.find("insufficient"), std::string::npos);
  EXPECT_EQ(row->reason.find("do not all agree"), std::string::npos);
}

TEST_F(DatAuditorTest, BadNameComparisonUsesExactContentBeforeWeakerCandidate) {
  const auto dat_id = add_dat("Selected", "audit-exact-resolution");
  const std::string exact_sha1(40, 'a');
  const std::string weak_sha1(40, 'b');
  const auto rom_id = add_rom(dat_id, "canonical.bin", exact_sha1);
  add_file("/a/longer/path/canonical.bin", exact_sha1);
  add_file("/x.bin", weak_sha1);
  add_match(rom_id, exact_sha1, romulus::core::MatchType::Exact);
  add_match(rom_id, weak_sha1, romulus::core::MatchType::Crc32Only);

  const auto result = audit(dat_id);

  EXPECT_EQ(result.summary.correct, 1);
  EXPECT_EQ(result.summary.wrong_name, 0);
  const auto* row = find_row(result, DatAuditStatus::Correct);
  ASSERT_NE(row, nullptr);
  EXPECT_EQ(row->file_path, "/a/longer/path/canonical.bin");
}

TEST_F(DatAuditorTest, ConflictAndStaleMatchRemainExplainableClassifierStates) {
  const auto dat_id = add_dat("Selected", "audit-conflict-mismatch");
  const std::string conflict_sha1_a(40, 'c');
  const std::string conflict_sha1_b(40, 'd');
  const auto conflict_rom_id = add_rom(dat_id, "conflict.bin", std::string(40, 'e'));
  add_file("/roms/conflict-a.bin", conflict_sha1_a);
  add_file("/roms/conflict-b.bin", conflict_sha1_b);
  add_match(conflict_rom_id, conflict_sha1_a, romulus::core::MatchType::Md5Only);
  add_match(conflict_rom_id, conflict_sha1_b, romulus::core::MatchType::Crc32Only);

  const std::string stale_sha1(40, 'f');
  const auto stale_rom_id = add_rom(dat_id, "stale.bin", stale_sha1, "Stale Game");
  ASSERT_TRUE(db_->upsert_global_rom(
                     {.sha1 = stale_sha1, .sha256 = {}, .md5 = {}, .crc32 = {}, .size = 100})
                  .has_value());
  add_match(stale_rom_id, stale_sha1);

  const auto result = audit(dat_id);

  EXPECT_EQ(result.summary.hash_conflict, 1);
  EXPECT_EQ(result.summary.mismatch, 1);
  const auto* conflict = find_row(result, DatAuditStatus::HashConflict);
  ASSERT_NE(conflict, nullptr);
  EXPECT_NE(conflict->reason.find("Multiple distinct content identities"), std::string::npos);
  const auto* mismatch = find_row(result, DatAuditStatus::Mismatch);
  ASSERT_NE(mismatch, nullptr);
  EXPECT_NE(mismatch->reason.find("no scanned physical file"), std::string::npos);
}

} // namespace
