#include "romulus/operations/operation_types.hpp"

#include <algorithm>
#include <cstddef>

namespace romulus::operations {

std::size_t OperationBatchResult::count(OperationResultStatus status) const noexcept {
  return static_cast<std::size_t>(std::ranges::count_if(
      results, [status](const OperationResult& result) { return result.status == status; }));
}

bool OperationBatchResult::completed_without_failures() const noexcept {
  return std::ranges::none_of(results, [](const OperationResult& result) {
    return result.status == OperationResultStatus::Blocked ||
           result.status == OperationResultStatus::Failed ||
           result.verification == VerificationStatus::Failed;
  });
}

} // namespace romulus::operations
