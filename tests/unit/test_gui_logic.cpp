#include "gui_logic.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <ranges>
#include <string>
#include <string_view>

namespace {

// ── ascii_lower ───────────────────────────────────────────────

TEST(GuiLogic, AsciiLowerConvertsUppercaseToLowercase) {
  EXPECT_EQ(romulus::gui::ascii_lower('A'), 'a');
  EXPECT_EQ(romulus::gui::ascii_lower('Z'), 'z');
  EXPECT_EQ(romulus::gui::ascii_lower('M'), 'm');
}

TEST(GuiLogic, AsciiLowerPreservesAlreadyLowercaseLetters) {
  EXPECT_EQ(romulus::gui::ascii_lower('a'), 'a');
  EXPECT_EQ(romulus::gui::ascii_lower('z'), 'z');
}

TEST(GuiLogic, AsciiLowerPreservesDigitsAndSymbols) {
  EXPECT_EQ(romulus::gui::ascii_lower('0'), '0');
  EXPECT_EQ(romulus::gui::ascii_lower('9'), '9');
  EXPECT_EQ(romulus::gui::ascii_lower(' '), ' ');
  EXPECT_EQ(romulus::gui::ascii_lower('_'), '_');
  EXPECT_EQ(romulus::gui::ascii_lower('-'), '-');
}

TEST(GuiLogic, AsciiLowerWorksWithRangesTransform) {
  std::string s = "Hello World 123";
  std::ranges::transform(s, s.begin(), romulus::gui::ascii_lower);
  EXPECT_EQ(s, "hello world 123");
}

// ── format_size ───────────────────────────────────────────────

TEST(GuiLogic, FormatSizeDisplaysBytes) {
  EXPECT_EQ(romulus::gui::format_size(0), "0 B");
  EXPECT_EQ(romulus::gui::format_size(1), "1 B");
  EXPECT_EQ(romulus::gui::format_size(512), "512 B");
  EXPECT_EQ(romulus::gui::format_size(1023), "1023 B");
}

TEST(GuiLogic, FormatSizeDisplaysKilobytes) {
  // 1024 bytes = 1.0 KB
  EXPECT_EQ(romulus::gui::format_size(1024), "1.0 KB");
  EXPECT_EQ(romulus::gui::format_size(2048), "2.0 KB");
}

TEST(GuiLogic, FormatSizeDisplaysMegabytes) {
  // 1 MB = 1024 * 1024 bytes
  EXPECT_EQ(romulus::gui::format_size(1024LL * 1024), "1.0 MB");
  EXPECT_EQ(romulus::gui::format_size(4LL * 1024 * 1024), "4.0 MB");
}

TEST(GuiLogic, FormatSizeDisplaysGigabytes) {
  // 1 GB = 1024^3 bytes
  EXPECT_EQ(romulus::gui::format_size(1024LL * 1024 * 1024), "1.0 GB");
  EXPECT_EQ(romulus::gui::format_size(2LL * 1024 * 1024 * 1024), "2.0 GB");
}

// ── status_label ─────────────────────────────────────────────

TEST(GuiLogic, StatusLabelReturnsDistinctStringsForEachStatus) {
  using romulus::core::RomStatusType;
  const auto* verified = romulus::gui::status_label(RomStatusType::Verified);
  const auto* missing = romulus::gui::status_label(RomStatusType::Missing);
  const auto* unverified = romulus::gui::status_label(RomStatusType::Unverified);
  const auto* mismatch = romulus::gui::status_label(RomStatusType::Mismatch);

  EXPECT_NE(std::string_view{verified}, std::string_view{missing});
  EXPECT_NE(std::string_view{verified}, std::string_view{unverified});
  EXPECT_NE(std::string_view{verified}, std::string_view{mismatch});
  EXPECT_NE(std::string_view{missing}, std::string_view{unverified});
  EXPECT_NE(std::string_view{missing}, std::string_view{mismatch});
  EXPECT_NE(std::string_view{unverified}, std::string_view{mismatch});
}

TEST(GuiLogic, StatusLabelReturnsNonEmptyStrings) {
  using romulus::core::RomStatusType;
  EXPECT_NE(romulus::gui::status_label(RomStatusType::Verified), nullptr);
  EXPECT_NE(romulus::gui::status_label(RomStatusType::Missing), nullptr);
  EXPECT_NE(romulus::gui::status_label(RomStatusType::Unverified), nullptr);
  EXPECT_NE(romulus::gui::status_label(RomStatusType::Mismatch), nullptr);
}

// ── status_icon ──────────────────────────────────────────────

TEST(GuiLogic, StatusIconReturnsDistinctStringsForEachStatus) {
  using romulus::core::RomStatusType;
  const auto* verified = romulus::gui::status_icon(RomStatusType::Verified);
  const auto* missing = romulus::gui::status_icon(RomStatusType::Missing);
  const auto* unverified = romulus::gui::status_icon(RomStatusType::Unverified);
  const auto* mismatch = romulus::gui::status_icon(RomStatusType::Mismatch);

  EXPECT_NE(std::string_view{verified}, std::string_view{missing});
  EXPECT_NE(std::string_view{verified}, std::string_view{unverified});
  EXPECT_NE(std::string_view{verified}, std::string_view{mismatch});
  EXPECT_NE(std::string_view{missing}, std::string_view{unverified});
}

TEST(GuiLogic, StatusIconReturnsNonEmptyStrings) {
  using romulus::core::RomStatusType;
  EXPECT_NE(romulus::gui::status_icon(RomStatusType::Verified), nullptr);
  EXPECT_NE(romulus::gui::status_icon(RomStatusType::Missing), nullptr);
  EXPECT_NE(romulus::gui::status_icon(RomStatusType::Unverified), nullptr);
  EXPECT_NE(romulus::gui::status_icon(RomStatusType::Mismatch), nullptr);
}

// ── status_sort_order ─────────────────────────────────────────

TEST(GuiLogic, StatusSortOrderMissingBeforeMismatch) {
  using romulus::core::RomStatusType;
  EXPECT_LT(romulus::gui::status_sort_order(RomStatusType::Missing),
            romulus::gui::status_sort_order(RomStatusType::Mismatch));
}

TEST(GuiLogic, StatusSortOrderMismatchBeforeUnverified) {
  using romulus::core::RomStatusType;
  EXPECT_LT(romulus::gui::status_sort_order(RomStatusType::Mismatch),
            romulus::gui::status_sort_order(RomStatusType::Unverified));
}

TEST(GuiLogic, StatusSortOrderUnverifiedBeforeVerified) {
  using romulus::core::RomStatusType;
  EXPECT_LT(romulus::gui::status_sort_order(RomStatusType::Unverified),
            romulus::gui::status_sort_order(RomStatusType::Verified));
}

TEST(GuiLogic, StatusSortOrderAllStatusesAreNonNegative) {
  using romulus::core::RomStatusType;
  EXPECT_GE(romulus::gui::status_sort_order(RomStatusType::Verified), 0);
  EXPECT_GE(romulus::gui::status_sort_order(RomStatusType::Missing), 0);
  EXPECT_GE(romulus::gui::status_sort_order(RomStatusType::Unverified), 0);
  EXPECT_GE(romulus::gui::status_sort_order(RomStatusType::Mismatch), 0);
}

} // namespace
