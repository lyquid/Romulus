#include "romulus/core/types.hpp"
#include "romulus/scanner/hash_service.hpp"
#include "romulus/service/romulus_service.hpp"
#include "synthetic_fixture_lab.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {

namespace fs = std::filesystem;
using nlohmann::json; // NOLINT(misc-include-cleaner): nlohmann's macro API hides the provider.
using romulus::core::DatAudit;
using romulus::core::DatAuditRow;
using romulus::core::DatAuditStatus;
using romulus::core::DatAuditSummary;
using romulus::core::DatVersion;
using romulus::service::RomulusService;
using romulus::test::SyntheticFixtureTree;

std::string read_file(const fs::path& path) {
  const std::ifstream stream(path, std::ios::binary);
  if (!stream.is_open()) {
    // A missing fixture must fail the test loudly. Treating a failed read as empty bytes could make
    // two independently broken generated trees look equal in the determinism assertion.
    throw std::runtime_error("Cannot read generated synthetic fixture: " + path.string());
  }
  std::ostringstream contents;
  contents << stream.rdbuf();
  if (stream.bad()) {
    throw std::runtime_error("Failed while reading generated synthetic fixture: " + path.string());
  }
  return std::move(contents).str();
}

std::map<std::string, std::string> snapshot_tree(const fs::path& root) {
  std::map<std::string, std::string> snapshot;
  for (const auto& entry : fs::recursive_directory_iterator(root)) {
    if (entry.is_regular_file()) {
      snapshot.emplace(fs::relative(entry.path(), root).generic_string(), read_file(entry.path()));
    }
  }
  return snapshot;
}

const DatAuditRow* find_expectation(const DatAudit& audit,
                                    std::string_view scenario_id,
                                    DatAuditStatus status) {
  const auto found = std::ranges::find_if(audit.rows, [&](const DatAuditRow& row) {
    return row.game_name == scenario_id && row.status == status;
  });
  return found == audit.rows.end() ? nullptr : &*found;
}

const DatAuditRow* find_physical_row(const DatAudit& audit,
                                     std::string_view actual_name,
                                     DatAuditStatus status) {
  const auto found = std::ranges::find_if(audit.rows, [&](const DatAuditRow& row) {
    return row.actual_name == actual_name && row.status == status;
  });
  return found == audit.rows.end() ? nullptr : &*found;
}

std::map<std::string, std::string> file_identity_snapshot(RomulusService& service) {
  auto files = service.get_all_files();
  EXPECT_TRUE(files.has_value()) << (files ? "" : files.error().message);
  std::map<std::string, std::string> identities;
  if (!files) {
    return identities;
  }
  for (const auto& file : *files) {
    identities.emplace(file.path, file.sha256);
  }
  return identities;
}

void expect_summary_matches_manifest(const DatAuditSummary& summary,
                                     const fs::path& manifest_path,
                                     std::string_view expectation_id) {
  const auto manifest = json::parse(read_file(manifest_path));
  const auto& expected =
      manifest.at("expected_audits").at(std::string{expectation_id}).at("summary");
  EXPECT_EQ(summary.expected_roms, expected.at("expected_roms").get<std::int64_t>());
  EXPECT_EQ(summary.correct, expected.at("correct").get<std::int64_t>());
  EXPECT_EQ(summary.missing, expected.at("missing").get<std::int64_t>());
  EXPECT_EQ(summary.wrong_name, expected.at("wrong_name").get<std::int64_t>());
  EXPECT_EQ(summary.extra, expected.at("extra").get<std::int64_t>());
  EXPECT_EQ(summary.extra_known_other_dat,
            expected.at("extra_known_other_dat").get<std::int64_t>());
  EXPECT_EQ(summary.globally_unknown, expected.at("globally_unknown").get<std::int64_t>());
  EXPECT_EQ(summary.duplicate_files, expected.at("duplicate_files").get<std::int64_t>());
}

struct ImportedDats {
  DatVersion v1;
  DatVersion v2;
  DatVersion other;
};

class SyntheticScenarioLabTest : public ::testing::Test {
protected:
  void SetUp() override {
    const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
    const std::string unique =
        std::string("romulus_synthetic_") + info->test_suite_name() + "_" + info->name();
    root_ = fs::temp_directory_path() / unique;
    db_path_ = fs::temp_directory_path() / (unique + ".db");

    // Every test gets a freshly generated tree. This is intentionally not a copied golden folder:
    // generator, real hashes, DAT parser, scanner, archive reader, matcher, and auditor remain in
    // the same executable path exercised by CI.
    fs::remove_all(root_);
    remove_database_files();
    auto generated = romulus::test::generate_synthetic_fixture_lab(root_);
    ASSERT_TRUE(generated.has_value()) << generated.error().message;
    tree_ = *generated;
    service_ = std::make_unique<RomulusService>(db_path_);
  }

  void TearDown() override {
    service_.reset();
    remove_database_files();
    fs::remove_all(root_);
  }

  void remove_database_files() const {
    fs::remove(db_path_);
    fs::remove(db_path_.string() + "-wal");
    fs::remove(db_path_.string() + "-shm");
  }

  void scan_once() {
    auto scan = service_->scan_directory(tree_.sources);
    ASSERT_TRUE(scan.has_value()) << scan.error().message;
    EXPECT_EQ(scan->files_scanned, 14);
    EXPECT_EQ(scan->files_hashed, 14);
    EXPECT_EQ(scan->files_skipped, 0);
    EXPECT_EQ(scan->archives_processed, 2);
  }

  ImportedDats import_all_dats_and_verify() {
    auto v1 = service_->import_dat(tree_.dats / "Synthetic Console v1.dat");
    EXPECT_TRUE(v1.has_value()) << (v1 ? "" : v1.error().message);
    auto v2 = service_->import_dat(tree_.dats / "Synthetic Console v2.dat");
    EXPECT_TRUE(v2.has_value()) << (v2 ? "" : v2.error().message);
    auto other = service_->import_dat(tree_.dats / "Other Console.dat");
    EXPECT_TRUE(other.has_value()) << (other ? "" : other.error().message);
    if (!v1 || !v2 || !other) {
      return {};
    }
    auto verified = service_->verify();
    EXPECT_TRUE(verified.has_value()) << (verified ? "" : verified.error().message);
    return {.v1 = *v1, .v2 = *v2, .other = *other};
  }

  DatAudit audit(std::int64_t dat_id) {
    auto result = service_->get_dat_audit(dat_id);
    EXPECT_TRUE(result.has_value()) << (result ? "" : result.error().message);
    return result.value_or(DatAudit{});
  }

  // GoogleTest implements TEST_F bodies as derived classes, so fixture state must remain
  // protected even though ordinary classes should keep data members private.
  // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes)
  fs::path root_;
  fs::path db_path_;
  SyntheticFixtureTree tree_;
  std::unique_ptr<RomulusService> service_;
  // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)
};

TEST_F(SyntheticScenarioLabTest, GeneratorIsDeterministicAndPublishesRealHashes) {
  const auto second_root = root_.string() + "_second";
  fs::remove_all(second_root);
  auto second = romulus::test::generate_synthetic_fixture_lab(second_root);
  ASSERT_TRUE(second.has_value()) << second.error().message;

  // Fixed archive metadata makes even the ZIP bytes reproducible, not merely their extracted
  // payloads. Relative paths and bytes must match across independently generated trees.
  EXPECT_EQ(snapshot_tree(root_), snapshot_tree(second_root));

  const auto parsed_manifest = json::parse(read_file(tree_.manifest));
  EXPECT_EQ(parsed_manifest.at("lab_id"), "romulus.synthetic.hostile.v1");
  EXPECT_EQ(parsed_manifest.at("payloads").size(), 12);

  // Cross-check one manifest record with the production regular-file hashing path. The complete
  // payload table is produced by that same hashing implementation inside the generator.
  auto alpha_hash =
      romulus::scanner::HashService::compute_hashes(tree_.sources / "clean" / "Alpha (World).rom");
  ASSERT_TRUE(alpha_hash.has_value()) << alpha_hash.error().message;
  const auto& alpha_manifest = parsed_manifest.at("payloads").at("alpha_v1");
  EXPECT_EQ(alpha_manifest.at("crc32"), alpha_hash->to_hex_crc32());
  EXPECT_EQ(alpha_manifest.at("md5"), alpha_hash->to_hex_md5());
  EXPECT_EQ(alpha_manifest.at("sha1"), alpha_hash->to_hex_sha1());
  EXPECT_EQ(alpha_manifest.at("sha256"), alpha_hash->to_hex_sha256());

  fs::remove_all(second_root);
}

TEST_F(SyntheticScenarioLabTest, RealPipelineAuditsV1BaselineSemantics) {
  scan_once();
  const auto dats = import_all_dats_and_verify();
  ASSERT_GT(dats.v1.id, 0);
  const auto result = audit(dats.v1.id);

  // The checked audit and the manual GUI oracle must agree. Hard assertions below retain focused
  // failure messages for classifier-specific categories not shared by every DAT summary.
  expect_summary_matches_manifest(result.summary, tree_.manifest, "synthetic_v1_all_dats_imported");
  EXPECT_EQ(result.summary.expected_roms, 8);
  EXPECT_EQ(result.summary.correct, 2);
  EXPECT_EQ(result.summary.wrong_name, 2);
  EXPECT_EQ(result.summary.missing, 1);
  EXPECT_EQ(result.summary.crc_match, 1);
  EXPECT_EQ(result.summary.md5_match, 2);
  EXPECT_EQ(result.summary.extra, 4);
  EXPECT_EQ(result.summary.extra_known_other_dat, 3);
  EXPECT_EQ(result.summary.globally_unknown, 1);
  EXPECT_EQ(result.summary.duplicate_files, 4);

  const auto* alpha = find_expectation(result, "alpha", DatAuditStatus::Correct);
  ASSERT_NE(alpha, nullptr);
  EXPECT_EQ(alpha->actual_name, "Alpha (World).rom");
  EXPECT_FALSE(alpha->reason.empty());

  const auto* beta = find_expectation(result, "beta", DatAuditStatus::WrongCanonicalName);
  ASSERT_NE(beta, nullptr);
  EXPECT_EQ(beta->actual_name, "Beta Goblin.rom");
  EXPECT_NE(beta->reason.find("Beta (World).rom"), std::string::npos);

  const auto* missing = find_expectation(result, "gamma_missing", DatAuditStatus::Missing);
  ASSERT_NE(missing, nullptr);
  EXPECT_TRUE(missing->file_path.empty());
  EXPECT_FALSE(missing->reason.empty());

  const auto* known_elsewhere =
      find_physical_row(result, "Delta (World).rom", DatAuditStatus::ExtraKnownOtherDat);
  ASSERT_NE(known_elsewhere, nullptr);
  EXPECT_NE(known_elsewhere->reason.find("Synthetic Console v2"), std::string::npos);

  const auto* globally_unknown =
      find_physical_row(result, "Mystery Goblin.rom", DatAuditStatus::ExtraUnknown);
  ASSERT_NE(globally_unknown, nullptr);
  EXPECT_NE(globally_unknown->reason.find("globally unknown"), std::string::npos);
}

TEST_F(SyntheticScenarioLabTest, DatImportsReclassifyExistingScanWithoutRehashing) {
  scan_once();
  const auto original_files = file_identity_snapshot(*service_);

  auto v1 = service_->import_dat(tree_.dats / "Synthetic Console v1.dat");
  ASSERT_TRUE(v1.has_value()) << v1.error().message;
  ASSERT_TRUE(service_->verify().has_value());

  // Before v2 exists in the database, Delta really is globally unknown. The same physical file
  // becomes "known elsewhere" after v2 is imported; its bytes and global identity do not change.
  const auto before_v2 = audit(v1->id);
  ASSERT_NE(find_physical_row(before_v2, "Delta (World).rom", DatAuditStatus::ExtraUnknown),
            nullptr);

  auto v2 = service_->import_dat(tree_.dats / "Synthetic Console v2.dat");
  ASSERT_TRUE(v2.has_value()) << v2.error().message;
  auto other = service_->import_dat(tree_.dats / "Other Console.dat");
  ASSERT_TRUE(other.has_value()) << other.error().message;
  ASSERT_TRUE(service_->verify().has_value());

  const auto after_v2 = audit(v1->id);
  ASSERT_NE(find_physical_row(after_v2, "Delta (World).rom", DatAuditStatus::ExtraKnownOtherDat),
            nullptr);
  ASSERT_NE(find_physical_row(
                after_v2, "Other Console Exclusive.rom", DatAuditStatus::ExtraKnownOtherDat),
            nullptr);

  const auto v2_audit = audit(v2->id);
  expect_summary_matches_manifest(
      v2_audit.summary, tree_.manifest, "synthetic_v2_all_dats_imported");
  EXPECT_EQ(v2_audit.summary.expected_roms, 5);
  EXPECT_EQ(v2_audit.summary.correct, 3);
  EXPECT_EQ(v2_audit.summary.wrong_name, 2);
  EXPECT_EQ(v2_audit.summary.missing, 0);
  EXPECT_EQ(v2_audit.summary.extra_known_other_dat, 5);
  EXPECT_EQ(v2_audit.summary.globally_unknown, 1);

  // v2 renamed alpha without changing bytes, changed beta bytes without changing its policy name,
  // added delta, and removed gamma. These assertions encode those contradictions explicitly.
  const auto* alpha_v1 = find_expectation(after_v2, "alpha", DatAuditStatus::Correct);
  const auto* alpha_v2 = find_expectation(v2_audit, "alpha", DatAuditStatus::WrongCanonicalName);
  ASSERT_NE(alpha_v1, nullptr);
  ASSERT_NE(alpha_v2, nullptr);
  EXPECT_EQ(alpha_v1->file_path, alpha_v2->file_path);

  const auto* beta_v1 = find_expectation(after_v2, "beta", DatAuditStatus::WrongCanonicalName);
  const auto* beta_v2 = find_expectation(v2_audit, "beta", DatAuditStatus::Correct);
  ASSERT_NE(beta_v1, nullptr);
  ASSERT_NE(beta_v2, nullptr);
  EXPECT_NE(beta_v1->file_path, beta_v2->file_path);
  ASSERT_NE(find_expectation(v2_audit, "delta_added", DatAuditStatus::Correct), nullptr);
  EXPECT_EQ(find_expectation(v2_audit, "gamma_missing", DatAuditStatus::Missing), nullptr);

  EXPECT_EQ(file_identity_snapshot(*service_), original_files);
  auto rescan = service_->scan_directory(tree_.sources);
  ASSERT_TRUE(rescan.has_value()) << rescan.error().message;
  EXPECT_EQ(rescan->files_hashed, 0);
  EXPECT_EQ(rescan->files_skipped, 14);
}

TEST_F(SyntheticScenarioLabTest, WeakMatchesDoNotTriggerWrongNamePolicy) {
  scan_once();
  const auto dats = import_all_dats_and_verify();
  const auto result = audit(dats.v1.id);

  const auto* md5 = find_expectation(result, "md5_fallback", DatAuditStatus::Md5Match);
  ASSERT_NE(md5, nullptr);
  EXPECT_EQ(md5->actual_name, "MD5 Goblin.rom");

  const auto* crc = find_expectation(result, "crc32_fallback", DatAuditStatus::CrcMatch);
  ASSERT_NE(crc, nullptr);
  EXPECT_EQ(crc->actual_name, "CRC Goblin.rom");

  // SHA-1 points at Conflict Goblin while deliberately contradictory MD5 points at alpha. Matcher
  // priority must retain the stronger SHA-1 content path, and DatAuditor must retain the weak /
  // inconsistent-content state instead of suggesting a rename from the goblin filename.
  const auto* conflict =
      find_expectation(result, "hash_metadata_conflict", DatAuditStatus::Md5Match);
  ASSERT_NE(conflict, nullptr);
  EXPECT_EQ(conflict->actual_name, "Conflict Goblin.rom");
  EXPECT_EQ(find_expectation(result, "hash_metadata_conflict", DatAuditStatus::WrongCanonicalName),
            nullptr);
}

TEST_F(SyntheticScenarioLabTest, ArchiveEntryNamesAndPhysicalDuplicatesUseRealScannerData) {
  scan_once();
  const auto dats = import_all_dats_and_verify();
  const auto result = audit(dats.v1.id);

  const auto* exact = find_expectation(result, "archive_exact", DatAuditStatus::Correct);
  ASSERT_NE(exact, nullptr);
  EXPECT_EQ(exact->actual_name, "Archive Exact (World).rom");
  EXPECT_NE(exact->file_path.find("naming-cases.zip::"), std::string::npos);

  const auto* wrong =
      find_expectation(result, "archive_wrong_name", DatAuditStatus::WrongCanonicalName);
  ASSERT_NE(wrong, nullptr);
  EXPECT_EQ(wrong->actual_name, "Archive Goblin.rom");
  EXPECT_NE(wrong->file_path.find("naming-cases.zip::"), std::string::npos);

  const auto duplicate_count = std::ranges::count_if(result.rows, [](const DatAuditRow& row) {
    return row.game_name == "alpha" && row.status == DatAuditStatus::Duplicate;
  });
  EXPECT_EQ(duplicate_count, 4);
  EXPECT_NE(find_physical_row(result, "Alpha Archive Copy.rom", DatAuditStatus::Duplicate),
            nullptr);

  // The canonical loose alpha wins over two longer loose paths and the archive entry. This proves
  // duplicate reporting and matched-file resolution are observing the same scanned identities.
  const auto* alpha = find_expectation(result, "alpha", DatAuditStatus::Correct);
  ASSERT_NE(alpha, nullptr);
  EXPECT_EQ(alpha->actual_name, "Alpha (World).rom");
  EXPECT_EQ(alpha->file_path.find("::"), std::string::npos);
}

} // namespace
