#include "romulus/operations/operation_executor.hpp"

#include "romulus/operations/operation_planner.hpp"
#include "romulus/operations/operation_types.hpp"
#include "romulus/scanner/hash_service.hpp"

#include <filesystem>
#include <string>
#include <system_error>

namespace romulus::operations {
namespace {

namespace fs = std::filesystem;

[[nodiscard]] OperationResult terminal_result(const Operation& operation,
                                              const SafetyAssessment& assessment) {
  OperationResult result;
  result.operation = operation;
  result.execution_safety = assessment.state;
  result.detail = assessment.detail;
  switch (assessment.state) {
    case OperationSafety::AlreadySatisfied:
      result.status = OperationResultStatus::AlreadySatisfied;
      break;
    case OperationSafety::NoChangeNeeded:
      result.status = OperationResultStatus::NoChangeNeeded;
      break;
    case OperationSafety::DestinationConflict:
    case OperationSafety::InvalidDestination:
    case OperationSafety::UnsupportedArchiveEntry:
      result.status = OperationResultStatus::Blocked;
      break;
    case OperationSafety::Ready:
      result.status = OperationResultStatus::Failed;
      result.detail = "Internal error: ready operation was treated as terminal";
      break;
    case OperationSafety::SourceMissing:
    case OperationSafety::SourceContentMismatch:
    case OperationSafety::InspectionFailed:
      result.status = OperationResultStatus::Failed;
      break;
  }
  return result;
}

} // namespace

OperationBatchResult OperationExecutor::execute(const OperationPlan& plan) {
  OperationBatchResult batch;
  batch.plan_id = plan.id;
  batch.results.reserve(plan.operations.size());
  // Operations are independent by design. Never abort the loop: a later failure must not erase
  // the truthful record of an earlier completed mutation (or prevent another safe one from
  // running).
  for (const auto& operation : plan.operations) {
    batch.results.push_back(execute_one(operation));
  }
  return batch;
}

OperationResult OperationExecutor::execute_one(const Operation& operation) {
  const auto assessment = OperationPlanner::inspect(operation);
  if (assessment.state != OperationSafety::Ready) {
    return terminal_result(operation, assessment);
  }

  OperationResult result;
  result.operation = operation;
  result.execution_safety = OperationSafety::Ready;
  result.status = OperationResultStatus::Failed;
  std::error_code copy_error;
  // copy_options::none is the central no-overwrite guarantee. If a destination appears after
  // planning, copy_file fails rather than replacing it; we then classify the new live state.
  const bool copied = fs::copy_file(
      operation.source_path, operation.destination_path, fs::copy_options::none, copy_error);
  if (!copied || copy_error) {
    const auto raced_assessment = OperationPlanner::inspect(operation);
    auto raced_result = terminal_result(operation, raced_assessment);
    if (raced_assessment.state == OperationSafety::Ready) {
      raced_result.status = OperationResultStatus::Failed;
      raced_result.detail = "Destination creation failed: " + copy_error.message();
    } else {
      raced_result.detail = "Filesystem changed after planning: " + raced_assessment.detail;
    }
    return raced_result;
  }
  result.destination_created = true;
  result.filesystem_changed = true;

  auto destination_hash = scanner::HashService::compute_hashes(operation.destination_path);
  if (!destination_hash || destination_hash->to_hex_sha1() != operation.content_sha1) {
    std::error_code cleanup_error;
    fs::remove(operation.destination_path, cleanup_error);
    result.detail = !destination_hash ? "Cannot verify newly created destination: " +
                                            destination_hash.error().message
                                      : "Newly created destination does not match planned SHA-1";
    if (cleanup_error) {
      result.detail += "; cleanup failed: " + cleanup_error.message();
    } else {
      result.destination_created = false;
      result.filesystem_changed = false;
    }
    return result;
  }

  // This is intentionally copy-verify-delete rather than rename(). The source is removed only
  // after the destination bytes are independently hashed and proven to preserve content identity.
  std::error_code remove_error;
  const bool removed = fs::remove(operation.source_path, remove_error);
  if (!removed || remove_error) {
    result.detail =
        remove_error ? "Destination verified, but source removal failed: " + remove_error.message()
                     : "Destination verified, but source disappeared before removal";
    return result;
  }

  result.source_removed = true;
  result.status = OperationResultStatus::Completed;
  result.detail = "Destination content verified before source removal";
  result.verification = VerificationStatus::Pending;
  return result;
}

} // namespace romulus::operations
