#pragma once

/// @file operation_planner.hpp
/// @brief Builds previewable rename plans and assesses current filesystem safety.

#include "romulus/operations/operation_types.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>

namespace romulus::operations {

/// Trusted content-match input used to construct one rename operation.
struct RenameRequest {
  std::filesystem::path source_path{};
  std::optional<std::string> source_entry_name{};
  std::string content_sha1;
  std::int64_t selected_dat_id = 0;
  std::int64_t expected_rom_id = 0;
  std::string expected_rom_name;
  std::string reason;
};

struct SafetyAssessment {
  OperationSafety state = OperationSafety::InspectionFailed;
  std::string detail;
};

/// Filesystem-aware planner shared by service, CLI, and GUI layers.
class OperationPlanner final {
public:
  [[nodiscard]] static Operation plan_rename(const RenameRequest& request, std::size_t ordinal = 0);

  [[nodiscard]] static OperationPlan plan_renames(std::string plan_id,
                                                  std::span<const RenameRequest> requests);

  /// Re-evaluates live filesystem state. The executor calls this again immediately before writing.
  [[nodiscard]] static SafetyAssessment inspect(const Operation& operation);
};

} // namespace romulus::operations
