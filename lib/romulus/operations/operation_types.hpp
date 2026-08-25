#pragma once

/// @file operation_types.hpp
/// @brief Serializable data contracts for previewable filesystem operations.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace romulus::operations {

/// Mutation kinds supported by the shared operation pipeline.
enum class OperationType : std::uint8_t {
  Rename,
};

/// Planner/executor assessment of whether an operation may run without data loss.
enum class OperationSafety : std::uint8_t {
  Ready,
  AlreadySatisfied,
  NoChangeNeeded,
  SourceMissing,
  SourceContentMismatch,
  DestinationConflict,
  InvalidDestination,
  UnsupportedArchiveEntry,
  InspectionFailed,
};

/// Observable filesystem state expected after a successful operation.
struct ExpectedPostcondition {
  std::filesystem::path canonical_path{};
  std::string content_sha1;
  bool source_absent = true;
};

/// One fully described mutation. UI and CLI consumers preview this data; neither invents actions.
struct Operation {
  std::string id;
  OperationType type = OperationType::Rename;
  std::filesystem::path source_path{};      ///< Physical loose file or archive container.
  std::filesystem::path destination_path{}; ///< Physical destination or archive container.
  std::optional<std::string> source_entry_name{};
  std::optional<std::string> destination_entry_name{};
  std::string content_sha1;
  std::int64_t selected_dat_id = 0;
  std::int64_t expected_rom_id = 0;
  std::string expected_rom_name;
  std::string reason;
  OperationSafety safety = OperationSafety::InspectionFailed;
  std::string safety_detail;
  ExpectedPostcondition expected_postcondition{};
};

/// Immutable preview unit handed to the executor as one batch.
struct OperationPlan {
  std::string id;
  std::vector<Operation> operations;
};

/// Truthful per-operation outcome. These states deliberately avoid a vague success boolean.
enum class OperationResultStatus : std::uint8_t {
  Completed,
  AlreadySatisfied,
  NoChangeNeeded,
  Blocked,
  Failed,
};

/// Independent post-scan verification state for a filesystem result.
enum class VerificationStatus : std::uint8_t {
  NotRequired,
  Pending,
  Passed,
  Failed,
};

/// Execution record retained even when another operation in the same batch fails.
struct OperationResult {
  Operation operation;
  OperationSafety execution_safety = OperationSafety::InspectionFailed;
  OperationResultStatus status = OperationResultStatus::Failed;
  std::string detail;
  bool destination_created = false;
  bool source_removed = false;
  bool filesystem_changed = false;
  VerificationStatus verification = VerificationStatus::NotRequired;
  std::string verification_detail;
};

/// Complete batch record. Consumers derive aggregate state from the preserved individual results.
struct OperationBatchResult {
  std::string plan_id;
  std::vector<OperationResult> results;

  [[nodiscard]] std::size_t count(OperationResultStatus status) const noexcept;
  [[nodiscard]] bool completed_without_failures() const noexcept;
};

} // namespace romulus::operations
