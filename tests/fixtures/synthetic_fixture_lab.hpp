#pragma once

/// @file synthetic_fixture_lab.hpp
/// @brief Public test-support API for Romulus's permanent hostile collection laboratory.

#include "romulus/core/error.hpp"

#include <filesystem>

namespace romulus::test {

/// Stable paths produced by generate_synthetic_fixture_lab().  Callers use these instead of
/// reconstructing directory names, which keeps both automated and manual consumers aligned with
/// the generator's vocabulary.
struct SyntheticFixtureTree {
  std::filesystem::path root{};
  std::filesystem::path dats{};
  std::filesystem::path sources{};
  std::filesystem::path manifest{};
};

/// Materializes the complete synthetic ROM/DAT scenario lab below output_root.
///
/// The generator intentionally owns the bytes, hashes, DAT XML, archive contents, and expected
/// manifest as one unit.  This prevents a fixture payload from drifting away from an opaque hash
/// copied into a DAT. Existing known files are overwritten, but unrelated files are never removed.
[[nodiscard]] core::Result<SyntheticFixtureTree> generate_synthetic_fixture_lab(
    const std::filesystem::path& output_root);

} // namespace romulus::test
