#pragma once

/// @file operation_executor.hpp
/// @brief Executes previously previewed operations without overwriting unrelated content.

#include "romulus/operations/operation_types.hpp"

namespace romulus::operations {

class OperationExecutor final {
public:
  [[nodiscard]] static OperationBatchResult execute(const OperationPlan& plan);

private:
  [[nodiscard]] static OperationResult execute_one(const Operation& operation);
};

} // namespace romulus::operations
