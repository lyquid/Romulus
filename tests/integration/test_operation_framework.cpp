#include "romulus/core/types.hpp"
#include "romulus/operations/operation_executor.hpp"
#include "romulus/operations/operation_planner.hpp"
#include "romulus/operations/operation_types.hpp"
#include "romulus/scanner/hash_service.hpp"
#include "romulus/service/romulus_service.hpp"
#include "synthetic_fixture_lab.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace {

namespace fs = std::filesystem;
using romulus::core::DatAuditStatus;
using romulus::operations::OperationBatchResult;
using romulus::operations::OperationExecutor;
using romulus::operations::OperationPlanner;
using romulus::operations::OperationResult;
using romulus::operations::OperationResultStatus;
using romulus::operations::OperationSafety;
using romulus::operations::RenameRequest;
using romulus::operations::VerificationStatus;
using romulus::service::RomulusService;
using romulus::test::SyntheticFixtureTree;

std::string sha1_of(const fs::path& path) {
  auto hashes = romulus::scanner::HashService::compute_hashes(path);
  EXPECT_TRUE(hashes.has_value()) << (hashes ? "" : hashes.error().message);
  return hashes ? hashes->to_hex_sha1() : std::string{};
}

RenameRequest rename_request(const fs::path& source,
                             std::string canonical_name,
                             std::string expected_sha1) {
  return {
      .source_path = source,
      .content_sha1 = std::move(expected_sha1),
      .selected_dat_id = 1,
      .expected_rom_id = 1,
      .expected_rom_name = std::move(canonical_name),
      .reason = "Synthetic exact content has a non-canonical filename",
  };
}

const OperationResult* find_result(const OperationBatchResult& batch, std::string_view rom_name) {
  const auto found = std::ranges::find_if(batch.results, [&](const OperationResult& result) {
    return result.operation.expected_rom_name == rom_name;
  });
  return found == batch.results.end() ? nullptr : &*found;
}

class OperationFrameworkTest : public ::testing::Test {
protected:
  void SetUp() override {
    const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
    const auto unique = std::string{"romulus_operations_"} + info->name();
    root_ = fs::temp_directory_path() / unique;
    database_path_ = fs::temp_directory_path() / (unique + ".db");
    fs::remove_all(root_);
    remove_database_files();

    auto generated = romulus::test::generate_synthetic_fixture_lab(root_);
    ASSERT_TRUE(generated.has_value()) << generated.error().message;
    tree_ = *generated;
    service_ = std::make_unique<RomulusService>(database_path_);
  }

  void TearDown() override {
    service_.reset();
    remove_database_files();
    fs::remove_all(root_);
  }

  void remove_database_files() const {
    fs::remove(database_path_);
    fs::remove(database_path_.string() + "-wal");
    fs::remove(database_path_.string() + "-shm");
  }

  std::int64_t scan_import_and_verify(const fs::path& scenario) {
    auto scan = service_->scan_directory(scenario);
    EXPECT_TRUE(scan.has_value()) << (scan ? "" : scan.error().message);
    auto dat = service_->import_dat(tree_.dats / "Synthetic Console v1.dat");
    EXPECT_TRUE(dat.has_value()) << (dat ? "" : dat.error().message);
    auto verified = service_->verify();
    EXPECT_TRUE(verified.has_value()) << (verified ? "" : verified.error().message);
    return dat ? dat->id : 0;
  }

  // GoogleTest TEST_F bodies derive from the fixture and require protected fixture state.
  // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes)
  fs::path root_;
  fs::path database_path_;
  SyntheticFixtureTree tree_;
  std::unique_ptr<RomulusService> service_;
  // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)
};

TEST_F(OperationFrameworkTest, WrongNameFindingRenamesAndVerifiesThroughRealPipeline) {
  const auto scenario = tree_.operations / "wrong-name-free";
  const auto source = scenario / "Beta Goblin.rom";
  const auto destination = scenario / "Beta (World).rom";
  const auto dat_id = scan_import_and_verify(scenario);
  ASSERT_GT(dat_id, 0);

  auto plan = service_->plan_wrong_name_renames(dat_id);
  ASSERT_TRUE(plan.has_value()) << plan.error().message;
  ASSERT_EQ(plan->operations.size(), 1);
  EXPECT_EQ(plan->operations.front().safety, OperationSafety::Ready);
  EXPECT_EQ(plan->operations.front().source_path, source);
  EXPECT_EQ(plan->operations.front().destination_path, destination);
  EXPECT_FALSE(plan->operations.front().content_sha1.empty());
  EXPECT_FALSE(plan->operations.front().reason.empty());

  auto repeated_plan = service_->plan_wrong_name_renames(dat_id);
  ASSERT_TRUE(repeated_plan.has_value()) << repeated_plan.error().message;
  ASSERT_EQ(repeated_plan->operations.size(), 1);
  EXPECT_EQ(repeated_plan->operations.front().id, plan->operations.front().id);
  EXPECT_EQ(repeated_plan->operations.front().source_path, plan->operations.front().source_path);
  EXPECT_EQ(repeated_plan->operations.front().destination_path,
            plan->operations.front().destination_path);

  auto executed = service_->execute_operation_plan(*plan);
  ASSERT_TRUE(executed.has_value()) << executed.error().message;
  ASSERT_EQ(executed->results.size(), 1);
  const auto& result = executed->results.front();
  EXPECT_EQ(result.status, OperationResultStatus::Completed);
  EXPECT_TRUE(result.destination_created);
  EXPECT_TRUE(result.source_removed);
  EXPECT_EQ(result.verification, VerificationStatus::Passed);
  EXPECT_TRUE(executed->completed_without_failures());
  EXPECT_FALSE(fs::exists(source));
  EXPECT_TRUE(fs::exists(destination));

  auto files = service_->get_all_files();
  ASSERT_TRUE(files.has_value()) << files.error().message;
  EXPECT_EQ(
      std::ranges::count_if(*files, [&](const auto& file) { return file.path == source.string(); }),
      0);
  EXPECT_EQ(std::ranges::count_if(
                *files, [&](const auto& file) { return file.path == destination.string(); }),
            1);

  auto audit = service_->get_dat_audit(dat_id);
  ASSERT_TRUE(audit.has_value()) << audit.error().message;
  EXPECT_EQ(std::ranges::count_if(audit->rows,
                                  [&](const auto& row) {
                                    return row.canonical_name == "Beta (World).rom" &&
                                           row.status == DatAuditStatus::Correct &&
                                           row.file_path == destination.string();
                                  }),
            1);
}

TEST_F(OperationFrameworkTest, IdenticalDestinationIsAlreadySatisfiedWithoutDeletingSource) {
  const auto scenario = tree_.operations / "destination-identical";
  const auto source = scenario / "Beta Goblin.rom";
  const auto destination = scenario / "Beta (World).rom";
  const auto operation = OperationPlanner::plan_rename(
      rename_request(source, destination.filename().string(), sha1_of(source)));
  EXPECT_EQ(operation.safety, OperationSafety::AlreadySatisfied);

  const auto batch = OperationExecutor::execute({.id = "identical", .operations = {operation}});
  ASSERT_EQ(batch.results.size(), 1);
  EXPECT_EQ(batch.results.front().status, OperationResultStatus::AlreadySatisfied);
  EXPECT_FALSE(batch.results.front().filesystem_changed);
  EXPECT_TRUE(fs::exists(source));
  EXPECT_TRUE(fs::exists(destination));
}

TEST_F(OperationFrameworkTest, DifferentDestinationIsHardConflictAndNeverOverwritten) {
  const auto scenario = tree_.operations / "destination-conflict";
  const auto source = scenario / "Beta Goblin.rom";
  const auto destination = scenario / "Beta (World).rom";
  const auto conflicting_sha1 = sha1_of(destination);
  const auto operation = OperationPlanner::plan_rename(
      rename_request(source, destination.filename().string(), sha1_of(source)));
  EXPECT_EQ(operation.safety, OperationSafety::DestinationConflict);

  const auto batch = OperationExecutor::execute({.id = "conflict", .operations = {operation}});
  ASSERT_EQ(batch.results.size(), 1);
  EXPECT_EQ(batch.results.front().status, OperationResultStatus::Blocked);
  EXPECT_EQ(sha1_of(destination), conflicting_sha1);
  EXPECT_TRUE(fs::exists(source));
}

TEST_F(OperationFrameworkTest, DestinationThatAppearsAfterPlanningIsRecheckedAtExecution) {
  const auto scenario = tree_.operations / "wrong-name-free";
  const auto source = scenario / "Beta Goblin.rom";
  const auto destination = scenario / "Beta (World).rom";
  const auto operation = OperationPlanner::plan_rename(
      rename_request(source, destination.filename().string(), sha1_of(source)));
  ASSERT_EQ(operation.safety, OperationSafety::Ready);

  // Simulate another process satisfying the plan between preview and execute.
  fs::copy_file(source, destination);
  const auto batch = OperationExecutor::execute({.id = "race", .operations = {operation}});
  ASSERT_EQ(batch.results.size(), 1);
  EXPECT_EQ(batch.results.front().status, OperationResultStatus::AlreadySatisfied);
  EXPECT_TRUE(fs::exists(source));
  EXPECT_TRUE(fs::exists(destination));
}

TEST_F(OperationFrameworkTest, ConflictingDestinationRaceNeverOverwritesNewContent) {
  const auto scenario = tree_.operations / "wrong-name-free";
  const auto source = scenario / "Beta Goblin.rom";
  const auto destination = scenario / "Beta (World).rom";
  const auto operation = OperationPlanner::plan_rename(
      rename_request(source, destination.filename().string(), sha1_of(source)));
  ASSERT_EQ(operation.safety, OperationSafety::Ready);

  // Simulate unrelated content claiming the free path after preview. The executor must classify
  // the new state as a hard conflict and preserve both the source and the new destination bytes.
  fs::copy_file(tree_.operations / "destination-conflict" / "Beta (World).rom", destination);
  const auto conflict_sha1 = sha1_of(destination);
  const auto batch =
      OperationExecutor::execute({.id = "conflicting-race", .operations = {operation}});
  ASSERT_EQ(batch.results.size(), 1);
  EXPECT_EQ(batch.results.front().execution_safety, OperationSafety::DestinationConflict);
  EXPECT_EQ(batch.results.front().status, OperationResultStatus::Blocked);
  EXPECT_EQ(sha1_of(destination), conflict_sha1);
  EXPECT_TRUE(fs::exists(source));
}

TEST_F(OperationFrameworkTest, MissingSourceAfterPlanningFailsTruthfully) {
  const auto scenario = tree_.operations / "source-disappears";
  const auto source = scenario / "Beta Goblin.rom";
  const auto dat_id = scan_import_and_verify(scenario);
  auto plan = service_->plan_wrong_name_renames(dat_id);
  ASSERT_TRUE(plan.has_value()) << plan.error().message;
  ASSERT_EQ(plan->operations.size(), 1);
  ASSERT_TRUE(fs::remove(source));

  auto batch = service_->execute_operation_plan(*plan);
  ASSERT_TRUE(batch.has_value()) << batch.error().message;
  ASSERT_EQ(batch->results.size(), 1);
  EXPECT_EQ(batch->results.front().execution_safety, OperationSafety::SourceMissing);
  EXPECT_EQ(batch->results.front().status, OperationResultStatus::Failed);
  EXPECT_FALSE(batch->results.front().filesystem_changed);
  EXPECT_FALSE(batch->completed_without_failures());
}

TEST_F(OperationFrameworkTest, ChangedSourceAfterPlanningIsNeverCopiedOrRemoved) {
  const auto scenario = tree_.operations / "wrong-name-free";
  const auto source = scenario / "Beta Goblin.rom";
  const auto destination = scenario / "Beta (World).rom";
  const auto operation = OperationPlanner::plan_rename(
      rename_request(source, destination.filename().string(), sha1_of(source)));
  ASSERT_EQ(operation.safety, OperationSafety::Ready);

  // Replace the bytes while preserving the planned path. Names and mtimes cannot authorize a
  // mutation: execution must hash the live source and reject the changed content identity.
  fs::copy_file(tree_.operations / "destination-conflict" / "Beta (World).rom",
                source,
                fs::copy_options::overwrite_existing);
  const auto changed_sha1 = sha1_of(source);
  ASSERT_NE(changed_sha1, operation.content_sha1);

  const auto batch =
      OperationExecutor::execute({.id = "changed-source", .operations = {operation}});
  ASSERT_EQ(batch.results.size(), 1);
  EXPECT_EQ(batch.results.front().execution_safety, OperationSafety::SourceContentMismatch);
  EXPECT_EQ(batch.results.front().status, OperationResultStatus::Failed);
  EXPECT_FALSE(batch.results.front().filesystem_changed);
  EXPECT_EQ(sha1_of(source), changed_sha1);
  EXPECT_FALSE(fs::exists(destination));
}

TEST_F(OperationFrameworkTest, CanonicalSourceProducesExplicitNoChangeResult) {
  const auto scenario = tree_.operations / "already-correct";
  const auto source = scenario / "Beta (World).rom";
  const auto operation = OperationPlanner::plan_rename(
      rename_request(source, source.filename().string(), sha1_of(source)));
  EXPECT_EQ(operation.safety, OperationSafety::NoChangeNeeded);

  const auto batch = OperationExecutor::execute({.id = "no-op", .operations = {operation}});
  ASSERT_EQ(batch.results.size(), 1);
  EXPECT_EQ(batch.results.front().status, OperationResultStatus::NoChangeNeeded);
  EXPECT_FALSE(batch.results.front().filesystem_changed);
  EXPECT_TRUE(fs::exists(source));
}

TEST_F(OperationFrameworkTest, PartialBatchPreservesIndependentResultsAndVerification) {
  const auto scenario = tree_.operations / "partial-batch";
  const auto alpha_source = scenario / "Alpha Goblin.rom";
  const auto beta_source = scenario / "Beta Goblin.rom";
  const auto dat_id = scan_import_and_verify(scenario);
  auto plan = service_->plan_wrong_name_renames(dat_id);
  ASSERT_TRUE(plan.has_value()) << plan.error().message;
  ASSERT_EQ(plan->operations.size(), 2);

  ASSERT_TRUE(fs::remove(beta_source));
  auto batch = service_->execute_operation_plan(*plan);
  ASSERT_TRUE(batch.has_value()) << batch.error().message;
  ASSERT_EQ(batch->results.size(), 2);

  const auto* alpha = find_result(*batch, "Alpha (World).rom");
  const auto* beta = find_result(*batch, "Beta (World).rom");
  ASSERT_NE(alpha, nullptr);
  ASSERT_NE(beta, nullptr);
  EXPECT_EQ(alpha->status, OperationResultStatus::Completed);
  EXPECT_EQ(alpha->verification, VerificationStatus::Passed);
  EXPECT_EQ(beta->status, OperationResultStatus::Failed);
  EXPECT_EQ(beta->execution_safety, OperationSafety::SourceMissing);
  EXPECT_EQ(batch->count(OperationResultStatus::Completed), 1);
  EXPECT_EQ(batch->count(OperationResultStatus::Failed), 1);
  EXPECT_FALSE(batch->completed_without_failures());
  EXPECT_FALSE(fs::exists(alpha_source));
  EXPECT_TRUE(fs::exists(scenario / "Alpha (World).rom"));
  EXPECT_FALSE(fs::exists(scenario / "Beta (World).rom"));
}

TEST_F(OperationFrameworkTest, ArchiveEntryRenameIsPlannedButExplicitlyDeferred) {
  const auto scenario = tree_.operations / "archive-deferred";
  const auto archive = scenario / "archive-name.zip";
  const auto archive_sha1 = sha1_of(archive);
  const auto dat_id = scan_import_and_verify(scenario);

  auto plan = service_->plan_wrong_name_renames(dat_id);
  ASSERT_TRUE(plan.has_value()) << plan.error().message;
  ASSERT_EQ(plan->operations.size(), 1);
  EXPECT_EQ(plan->operations.front().source_path, archive);
  EXPECT_EQ(plan->operations.front().source_entry_name,
            std::optional<std::string>{"Archive Goblin.rom"});
  EXPECT_EQ(plan->operations.front().destination_entry_name,
            std::optional<std::string>{"Archive Wrong (World).rom"});
  EXPECT_EQ(plan->operations.front().safety, OperationSafety::UnsupportedArchiveEntry);

  auto batch = service_->execute_operation_plan(*plan);
  ASSERT_TRUE(batch.has_value()) << batch.error().message;
  ASSERT_EQ(batch->results.size(), 1);
  EXPECT_EQ(batch->results.front().status, OperationResultStatus::Blocked);
  EXPECT_EQ(sha1_of(archive), archive_sha1);
}

} // namespace
