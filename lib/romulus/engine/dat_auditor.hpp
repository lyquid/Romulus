#pragma once

/// @file dat_auditor.hpp
/// @brief Composes existing matching/status/file queries into an explainable per-DAT audit.

#include "romulus/core/error.hpp"
#include "romulus/core/types.hpp"

#include <cstdint>

namespace romulus::database {
class Database;
}

namespace romulus::engine {

/// Builds the audit model consumed by the desktop workspace. This class is deliberately free
/// of ImGui and filesystem mutation so all audit semantics can be tested independently.
class DatAuditor final {
public:
  [[nodiscard]] static core::Result<core::DatAudit> audit(database::Database& db,
                                                          std::int64_t dat_version_id);
};

} // namespace romulus::engine
