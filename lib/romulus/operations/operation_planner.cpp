#include "romulus/operations/operation_planner.hpp"

#include "romulus/operations/operation_types.hpp"
#include "romulus/scanner/hash_service.hpp"

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace romulus::operations {
namespace {

namespace fs = std::filesystem;

[[nodiscard]] bool is_safe_leaf_name(const std::string& name) {
  const fs::path candidate{name};
  // DAT names are untrusted policy input. Phase 1 only renames loose files in place, so rejecting
  // rooted and multi-component names prevents a DAT from moving content outside the source folder.
  return !name.empty() && !candidate.has_root_path() && !candidate.has_parent_path() &&
         candidate.filename() == candidate && candidate.filename() != "." &&
         candidate.filename() != "..";
}

[[nodiscard]] SafetyAssessment hash_matches(const fs::path& path,
                                            const std::string& expected_sha1,
                                            OperationSafety mismatch_state,
                                            std::string_view subject) {
  auto hashes = scanner::HashService::compute_hashes(path);
  if (!hashes) {
    return {.state = OperationSafety::InspectionFailed,
            .detail = "Cannot hash " + std::string{subject} + " '" + path.string() +
                      "': " + hashes.error().message};
  }
  if (hashes->to_hex_sha1() != expected_sha1) {
    return {.state = mismatch_state,
            .detail =
                std::string{subject} + " content does not match planned SHA-1 " + expected_sha1};
  }
  return {.state = OperationSafety::Ready, .detail = "Content matches planned SHA-1"};
}

} // namespace

Operation OperationPlanner::plan_rename(const RenameRequest& request, std::size_t ordinal) {
  Operation operation;
  operation.id = "rename-" + std::to_string(request.selected_dat_id) + "-" +
                 std::to_string(request.expected_rom_id) + "-" + std::to_string(ordinal + 1);
  operation.type = OperationType::Rename;
  operation.source_path = request.source_path;
  operation.source_entry_name = request.source_entry_name;
  operation.content_sha1 = request.content_sha1;
  operation.selected_dat_id = request.selected_dat_id;
  operation.expected_rom_id = request.expected_rom_id;
  operation.expected_rom_name = request.expected_rom_name;
  operation.reason = request.reason;

  if (request.source_entry_name) {
    // Archive replacement needs container-level transactional semantics. Preserve the desired
    // entry name in the plan, but make the unsupported state visible instead of mutating a ZIP.
    operation.destination_path = request.source_path;
    operation.destination_entry_name = request.expected_rom_name;
  } else if (is_safe_leaf_name(request.expected_rom_name)) {
    operation.destination_path = request.source_path.parent_path() / request.expected_rom_name;
  }

  operation.expected_postcondition = {
      .canonical_path = operation.destination_path,
      .content_sha1 = request.content_sha1,
      .source_absent = !request.source_entry_name.has_value(),
  };
  const auto assessment = inspect(operation);
  operation.safety = assessment.state;
  operation.safety_detail = assessment.detail;
  return operation;
}

OperationPlan OperationPlanner::plan_renames(std::string plan_id,
                                             std::span<const RenameRequest> requests) {
  OperationPlan plan;
  plan.id = std::move(plan_id);
  plan.operations.reserve(requests.size());
  for (std::size_t index = 0; index < requests.size(); ++index) {
    plan.operations.push_back(plan_rename(requests[index], index));
  }
  return plan;
}

SafetyAssessment OperationPlanner::inspect(const Operation& operation) {
  if (operation.type != OperationType::Rename) {
    return {.state = OperationSafety::InspectionFailed,
            .detail = "Operation type is not supported by the rename planner"};
  }
  if (operation.source_entry_name) {
    return {.state = OperationSafety::UnsupportedArchiveEntry,
            .detail = "Archive-entry rename is deferred until safe container replacement exists"};
  }
  if (!is_safe_leaf_name(operation.expected_rom_name) || operation.destination_path.empty()) {
    return {.state = OperationSafety::InvalidDestination,
            .detail = "Canonical DAT name must be one safe filename for loose-file rename"};
  }
  const auto required_destination =
      (operation.source_path.parent_path() / operation.expected_rom_name).lexically_normal();
  if (operation.destination_path.lexically_normal() != required_destination) {
    return {.state = OperationSafety::InvalidDestination,
            .detail = "Destination does not match the canonical filename beside the source"};
  }
  if (operation.source_path.lexically_normal() == operation.destination_path.lexically_normal()) {
    std::error_code exists_error;
    const bool source_exists = fs::exists(operation.source_path, exists_error);
    if (exists_error) {
      return {.state = OperationSafety::InspectionFailed,
              .detail = "Cannot inspect canonical source '" + operation.source_path.string() +
                        "': " + exists_error.message()};
    }
    if (!source_exists) {
      return {.state = OperationSafety::SourceMissing,
              .detail = "Canonical source file no longer exists"};
    }
    std::error_code type_error;
    if (!fs::is_regular_file(operation.source_path, type_error) || type_error) {
      return {.state = OperationSafety::InspectionFailed,
              .detail = "Canonical source is not a readable regular file"};
    }
    auto source_hash = hash_matches(operation.source_path,
                                    operation.content_sha1,
                                    OperationSafety::SourceContentMismatch,
                                    "Source");
    if (source_hash.state != OperationSafety::Ready) {
      return source_hash;
    }
    return {.state = OperationSafety::NoChangeNeeded,
            .detail = "Source already has the canonical DAT filename"};
  }

  std::error_code source_error;
  const bool source_exists = fs::exists(operation.source_path, source_error);
  if (source_error) {
    return {.state = OperationSafety::InspectionFailed,
            .detail = "Cannot inspect source '" + operation.source_path.string() +
                      "': " + source_error.message()};
  }
  if (!source_exists) {
    return {.state = OperationSafety::SourceMissing,
            .detail = "Source file does not exist: " + operation.source_path.string()};
  }
  std::error_code source_type_error;
  if (!fs::is_regular_file(operation.source_path, source_type_error) || source_type_error) {
    return {.state = OperationSafety::InspectionFailed,
            .detail = "Source exists but is not a readable regular file: " +
                      operation.source_path.string()};
  }
  auto source_hash = hash_matches(operation.source_path,
                                  operation.content_sha1,
                                  OperationSafety::SourceContentMismatch,
                                  "Source");
  if (source_hash.state != OperationSafety::Ready) {
    return source_hash;
  }

  std::error_code destination_error;
  const bool destination_exists = fs::exists(operation.destination_path, destination_error);
  if (destination_error) {
    return {.state = OperationSafety::InspectionFailed,
            .detail = "Cannot inspect destination '" + operation.destination_path.string() +
                      "': " + destination_error.message()};
  }
  if (!destination_exists) {
    return {.state = OperationSafety::Ready,
            .detail = "Source content is verified and destination is absent"};
  }
  if (!fs::is_regular_file(operation.destination_path, destination_error)) {
    return {.state = OperationSafety::DestinationConflict,
            .detail = "Destination exists and is not a regular file"};
  }
  auto destination_hash = hash_matches(operation.destination_path,
                                       operation.content_sha1,
                                       OperationSafety::DestinationConflict,
                                       "Destination");
  if (destination_hash.state == OperationSafety::Ready) {
    return {.state = OperationSafety::AlreadySatisfied,
            .detail = "Destination already contains identical content; source is left untouched"};
  }
  return destination_hash;
}

} // namespace romulus::operations
