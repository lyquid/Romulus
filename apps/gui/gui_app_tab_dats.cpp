#include "gui_app.hpp"

#include "gui_app_shared.hpp"

#include <algorithm>
#include <cmath>
#include <ranges>
#include <string>

namespace romulus::gui {

void GuiApp::render_dats_tab() {
  const bool busy = is_busy();

  // ── DAT controls ──────────────────────────────────────────────
  ImGui::BeginDisabled(busy);
  if (ImGui::Button("Import DAT")) {
    action_import_dat();
  }
  ImGui::EndDisabled();

  ImGui::SameLine();

  // DAT selector dropdown — expands to fill available width, leaving room for "Check DAT" and
  // "Delete DAT" buttons on the right. Widths are computed from the current font/style so the
  // combo never crowds or clips the buttons regardless of DPI or font size.
  {
    std::string preview = "(No DAT selected)";
    if (selected_dat_index_ >= 0 && selected_dat_index_ < static_cast<int>(dat_versions_.size())) {
      const auto& dv = dat_versions_[static_cast<std::size_t>(selected_dat_index_)];
      preview = dv.name + " v" + dv.version;
    }

    const float frame_px = ImGui::GetStyle().FramePadding.x;
    const float spacing  = ImGui::GetStyle().ItemSpacing.x;
    const float w_check  = ImGui::CalcTextSize("Check DAT").x  + frame_px * 2.0F;
    const float w_delete = ImGui::CalcTextSize("Delete DAT").x + frame_px * 2.0F;
    ImGui::PushItemWidth(-(w_check + w_delete + spacing * 2.0F));
    if (ImGui::BeginCombo("##dat_combo", preview.c_str())) {
      for (int i = 0; i < static_cast<int>(dat_versions_.size()); ++i) {
        const auto& dv = dat_versions_[static_cast<std::size_t>(i)];
        std::string label = dv.name + " v" + dv.version;
        if (!dv.imported_at.empty()) {
          label += "  (" + dv.imported_at + ")";
        }
        bool is_selected = (selected_dat_index_ == i);
        if (ImGui::Selectable(label.c_str(), is_selected)) {
          selected_dat_index_ = i;
          rom_checklist_.clear();
          game_checklist_.clear();
          selected_game_id_ = -1;
          checklist_stats_ = {};
        }
        if (is_selected) {
          ImGui::SetItemDefaultFocus();
        }
      }
      ImGui::EndCombo();
    }
    ImGui::PopItemWidth();
  }

  ImGui::SameLine();

  ImGui::BeginDisabled(busy || selected_dat_index_ < 0);
  if (ImGui::Button("Check DAT")) {
    action_check_dat();
  }
  ImGui::EndDisabled();

  ImGui::SameLine();

  ImGui::BeginDisabled(busy || selected_dat_index_ < 0);
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55F, 0.12F, 0.12F, 1.0F));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75F, 0.18F, 0.18F, 1.0F));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.85F, 0.22F, 0.22F, 1.0F));
  if (ImGui::Button("Delete DAT")) {
    show_delete_dat_confirm_ = true;
  }
  ImGui::PopStyleColor(3);
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && selected_dat_index_ < 0) {
    ImGui::SetTooltip("Select a DAT first");
  }
  ImGui::EndDisabled();

  // ── Active DAT banner ─────────────────────────────────────────
  ImGui::Spacing();
  {
    const float line_h = ImGui::GetTextLineHeightWithSpacing();
    const float banner_h = line_h + ImGui::GetStyle().FramePadding.y * 2.0F + k_BannerExtraPadding;
    const float v_pad = (banner_h - line_h) * 0.5F - ImGui::GetStyle().WindowPadding.y;

    const bool has_dat =
        selected_dat_index_ >= 0 && selected_dat_index_ < static_cast<int>(dat_versions_.size());
    const ImVec4 bg_col =
        has_dat ? ImVec4(0.08F, 0.16F, 0.32F, 1.0F) : ImVec4(0.10F, 0.10F, 0.12F, 1.0F);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, bg_col);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0F);
    if (ImGui::BeginChild("##active_dat_banner",
                          ImVec2(-1.0F, banner_h),
                          true,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
      if (v_pad > 0.0F) {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + v_pad);
      }
      if (has_dat) {
        const auto& dv = dat_versions_[static_cast<std::size_t>(selected_dat_index_)];
        ImGui::TextColored(ImVec4(0.45F, 0.75F, 1.0F, 1.0F), "Active DAT");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.55F, 0.55F, 0.60F, 1.0F), "|");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0F, 1.0F, 1.0F, 1.0F), "%s", dv.name.c_str());
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.6F, 0.75F, 0.9F, 1.0F), "v%s", dv.version.c_str());
        if (!dv.imported_at.empty()) {
          ImGui::SameLine();
          ImGui::TextDisabled("  imported %s", dv.imported_at.c_str());
        }
      } else {
        ImGui::TextDisabled("No DAT selected");
      }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
  }

  ImGui::Spacing();

  // ── Empty state ────────────────────────────────────────────────
  if (rom_checklist_.empty()) {
    ImGui::Spacing();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 20.0F);
    if (selected_dat_index_ < 0) {
      ImGui::TextDisabled("Select a DAT from the dropdown above, then click 'Check DAT'.");
    } else {
      ImGui::TextDisabled("Click 'Check DAT' to verify and populate the ROM list.");
    }
    return;
  }

  // ── Summary bar ───────────────────────────────────────────────
  const std::int64_t total = checklist_stats_.total;
  const std::int64_t cnt_verified = checklist_stats_.verified;
  const std::int64_t cnt_missing = checklist_stats_.missing;
  const std::int64_t cnt_crc_match = checklist_stats_.crc_match;
  const std::int64_t cnt_md5_match = checklist_stats_.md5_match;
  const std::int64_t cnt_hash_conflict = checklist_stats_.hash_conflict;
  const std::int64_t cnt_mismatch = checklist_stats_.mismatch;
  const std::int64_t games_total = checklist_stats_.games_total;
  const double pct =
      total > 0 ? static_cast<double>(cnt_verified) / static_cast<double>(total) * 100.0 : 0.0;

  ImGui::TextDisabled("%lld games", static_cast<long long>(games_total));
  ImGui::SameLine(0.0F, 14.0F);
  ImGui::TextColored(ImVec4(0.35F, 0.35F, 0.42F, 1.0F), "|");
  ImGui::SameLine(0.0F, 14.0F);
  ImGui::TextColored(k_ColorVerified, "%lld", static_cast<long long>(cnt_verified));
  ImGui::SameLine();
  ImGui::Text("/ %lld ROMs verified (%.1f%%)", static_cast<long long>(total), pct);
  if (cnt_missing > 0) {
    ImGui::SameLine(0.0F, 14.0F);
    ImGui::TextColored(
        k_ColorMissing, "%s %lld missing", k_SymbolMissing, static_cast<long long>(cnt_missing));
  }
  if (cnt_crc_match > 0) {
    ImGui::SameLine(0.0F, 14.0F);
    ImGui::TextColored(
        k_ColorCrcMatch, "[~] %lld CRC match", static_cast<long long>(cnt_crc_match));
  }
  if (cnt_md5_match > 0) {
    ImGui::SameLine(0.0F, 14.0F);
    ImGui::TextColored(
        k_ColorMd5Match, "[~~] %lld MD5 match", static_cast<long long>(cnt_md5_match));
  }
  if (cnt_hash_conflict > 0) {
    ImGui::SameLine(0.0F, 14.0F);
    ImGui::TextColored(k_ColorHashConflict,
                       "%s %lld hash conflict",
                       k_StatusIconHashConflict,
                       static_cast<long long>(cnt_hash_conflict));
  }
  if (cnt_mismatch > 0) {
    ImGui::SameLine(0.0F, 14.0F);
    ImGui::TextColored(k_ColorMismatch, "[!!] %lld mismatch", static_cast<long long>(cnt_mismatch));
  }

  ImGui::Spacing();

  // ── Master-detail split layout ────────────────────────────────
  // Left panel (38 %): sortable, filterable game list.
  // Right panel (62 %): ROM detail for the selected game.
  // Reserve enough vertical space for the status bar rendered after EndTabBar().
  constexpr float k_LeftFraction = 0.38F;
  constexpr float k_PanelGap = 6.0F; // px between the two panels
  const float avail_w = ImGui::GetContentRegionAvail().x;
  const float left_w = std::floor(avail_w * k_LeftFraction);
  const float right_w = avail_w - left_w - k_PanelGap;
  const float panel_h = -ImGui::GetFrameHeightWithSpacing(); // leave room for status bar

  // ── Left panel: Games ─────────────────────────────────────────
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0F, 6.0F));
  ImGui::BeginChild("##games_panel", ImVec2(left_w, panel_h), true);
  ImGui::PopStyleVar();

  {
    // Filter bar
    constexpr float k_StatusComboW = 110.0F;
    ImGui::SetNextItemWidth(
        ImGui::GetContentRegionAvail().x - k_StatusComboW - ImGui::GetStyle().ItemSpacing.x);
    ImGui::InputText("##game_filter", game_filter_buf_.data(), k_MaxFilterLen);
    if (ImGui::IsItemEdited()) {
      game_filter_lower_.assign(game_filter_buf_.data());
      std::ranges::transform(game_filter_lower_, game_filter_lower_.begin(), ascii_lower);
    }
    ImGui::SameLine();
    constexpr const char* k_StatusFilterItems[] = {
        "All", "Verified", "Missing", "CRC Match", "MD5 Match", "Hash Conflict", "Mismatch"};
    ImGui::SetNextItemWidth(k_StatusComboW);
    ImGui::Combo("##game_status_filter",
                 &game_status_filter_,
                 k_StatusFilterItems,
                 IM_ARRAYSIZE(k_StatusFilterItems));

    ImGui::Spacing();

    // Games table + right-side nav strip
    constexpr float k_NavStripW = 24.0F;
    const float nav_gap = ImGui::GetStyle().ItemSpacing.x;
    constexpr int k_GameColumnCount = 2;

    ImGui::BeginGroup();
    if (ImGui::BeginTable("game_checklist_table",
                          k_GameColumnCount,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable |
                              ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Sortable,
                          ImVec2(-k_NavStripW - nav_gap, 0.0F))) {
      ImGui::TableSetupScrollFreeze(0, 1);
      ImGui::TableSetupColumn("St", ImGuiTableColumnFlags_None, 0.9F);
      ImGui::TableSetupColumn("Game Name", ImGuiTableColumnFlags_DefaultSort, 6.0F);
      ImGui::TableHeadersRow();

      if (auto* sort_specs = ImGui::TableGetSortSpecs()) {
        if (sort_specs->SpecsDirty) {
          if (sort_specs->SpecsCount > 0) {
            game_sort_col_ = sort_specs->Specs[0].ColumnIndex;
            game_sort_ascending_ =
                (sort_specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending);
          } else {
            game_sort_col_ = -1;
          }
          apply_game_sort();
          sort_specs->SpecsDirty = false;
        }
      }

      if (scroll_game_top_) {
        ImGui::SetScrollY(0.0F);
        scroll_game_top_ = false;
      } else if (scroll_game_bottom_) {
        ImGui::SetScrollY(ImGui::GetScrollMaxY());
        scroll_game_bottom_ = false;
      }

      const std::string& filter_str = game_filter_lower_;

      for (std::size_t i = 0; i < game_checklist_.size(); ++i) {
        const auto& entry = game_checklist_[i];

        // Status filter
        if (game_status_filter_ != 0 &&
            (game_status_filter_ - 1) != static_cast<int>(entry.status)) {
          continue;
        }
        // Name filter
        if (!filter_str.empty() &&
            entry.name_lower.find(filter_str) == std::string::npos) {
          continue;
        }

        const bool is_selected = (entry.game_id == selected_game_id_);
        const ImVec4 color = status_color(entry.status);

        ImGui::TableNextRow();
        ImGui::PushID(static_cast<int>(i));

        // Highlight the selected row with a distinct background.
        if (is_selected) {
          ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(38, 82, 160, 200));
          ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, IM_COL32(38, 82, 160, 200));
        }

        // Col 0: compact status badge
        ImGui::TableSetColumnIndex(k_GameColStatus);
        ImGui::TextColored(color, "%s", status_icon(entry.status));

        // Col 1: game name — Selectable spanning remaining columns for full-row click.
        ImGui::TableSetColumnIndex(k_GameColName);
        if (ImGui::Selectable(entry.name.c_str(),
                              is_selected,
                              ImGuiSelectableFlags_SpanAllColumns,
                              ImVec2(0.0F, 0.0F))) {
          selected_game_id_ = entry.game_id;
          // Reset ROM detail scroll so it lands at the top for the new game.
          scroll_checklist_top_ = true;
        }
        if (ImGui::IsItemHovered()) {
          ImGui::SetTooltip("Right-click to copy name");
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
          ImGui::SetClipboardText(entry.name.c_str());
          show_toast("Game name copied to clipboard");
        }

        ImGui::PopID();
      }
      ImGui::EndTable();
    }
    ImGui::EndGroup();

    // Vertically centred ^ / v scroll buttons to the right of the table.
    ImGui::SameLine(0.0F, nav_gap);
    {
      const float strip_h = ImGui::GetItemRectSize().y;
      const float btn_h = ImGui::GetFrameHeight();
      const float total_btn_h = btn_h * 2.0F + ImGui::GetStyle().ItemSpacing.y;
      ImGui::BeginChild("##game_nav_strip",
                        ImVec2(k_NavStripW, strip_h),
                        false,
                        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
      const float v_pad = (strip_h - total_btn_h) * 0.5F;
      if (v_pad > 0.0F) {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + v_pad);
      }
      if (ImGui::Button("^")) {
        scroll_game_top_ = true;
      }
      if (ImGui::Button("v")) {
        scroll_game_bottom_ = true;
      }
      ImGui::EndChild();
    }
  }

  ImGui::EndChild(); // ##games_panel

  ImGui::SameLine(0.0F, k_PanelGap);

  // ── Right panel: ROM detail ───────────────────────────────────
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0F, 6.0F));
  ImGui::BeginChild("##roms_panel", ImVec2(right_w, panel_h), true);
  ImGui::PopStyleVar();

  {
    if (selected_game_id_ < 0) {
      // Nothing selected yet — show a gentle prompt.
      const float content_h = ImGui::GetContentRegionAvail().y;
      ImGui::SetCursorPosY(ImGui::GetCursorPosY() + content_h * 0.42F);
      const char* hint = "Select a game on the left to view its ROMs.";
      const float hint_w = ImGui::CalcTextSize(hint).x;
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                           (ImGui::GetContentRegionAvail().x - hint_w) * 0.5F);
      ImGui::TextDisabled("%s", hint);
    } else {
      // Find the selected game entry for the header.
      const GameChecklistEntry* sel_game = nullptr;
      for (const auto& g : game_checklist_) {
        if (g.game_id == selected_game_id_) {
          sel_game = &g;
          break;
        }
      }

      // ── Game header banner ──────────────────────────────────
      if (sel_game != nullptr) {
        const ImVec4 badge_col = status_color(sel_game->status);
        ImGui::TextColored(badge_col, "%s", status_icon(sel_game->status));
        ImGui::SameLine(0.0F, 8.0F);
        ImGui::TextColored(ImVec4(0.88F, 0.93F, 1.0F, 1.0F), "%s", sel_game->name.c_str());
        ImGui::SameLine(0.0F, 10.0F);
        ImGui::TextDisabled("(%d ROM%s)",
                            sel_game->rom_count,
                            sel_game->rom_count != 1 ? "s" : "");
        ImGui::Separator();
      }

      // ── ROM detail table ─────────────────────────────────────
      // Use SizingFixedFit with explicit pixel widths + an inner_width slightly
      // larger than their sum so the last column's right border is never clipped.
      constexpr float k_ColWStatus = 80.0F;
      constexpr float k_ColWRomName = 280.0F;
      constexpr float k_ColWSize = 70.0F;
      constexpr float k_ColWSha1 = 200.0F;
      constexpr float k_ColWMd5 = 180.0F;
      constexpr float k_ColWCrc32 = 90.0F;
      constexpr float k_RomTableInnerW =
          k_ColWStatus + k_ColWRomName + k_ColWSize + k_ColWSha1 + k_ColWMd5 + k_ColWCrc32 +
          20.0F; // +20 px padding so the last column border is never clipped
      constexpr int k_RomColumnCount = 6;
      if (ImGui::BeginTable("rom_detail_table",
                            k_RomColumnCount,
                            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX |
                                ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit |
                                ImGuiTableFlags_Sortable,
                            ImVec2(0.0F, 0.0F),
                            k_RomTableInnerW)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_None, k_ColWStatus);
        ImGui::TableSetupColumn("ROM Name", ImGuiTableColumnFlags_DefaultSort, k_ColWRomName);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_None, k_ColWSize);
        ImGui::TableSetupColumn("SHA1", ImGuiTableColumnFlags_None, k_ColWSha1);
        ImGui::TableSetupColumn("MD5", ImGuiTableColumnFlags_None, k_ColWMd5);
        ImGui::TableSetupColumn("CRC32", ImGuiTableColumnFlags_None, k_ColWCrc32);
        ImGui::TableHeadersRow();

        if (auto* sort_specs = ImGui::TableGetSortSpecs()) {
          if (sort_specs->SpecsDirty) {
            if (sort_specs->SpecsCount > 0) {
              checklist_sort_col_ = sort_specs->Specs[0].ColumnIndex;
              checklist_sort_ascending_ =
                  (sort_specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending);
            } else {
              checklist_sort_col_ = -1;
            }
            apply_checklist_sort();
            sort_specs->SpecsDirty = false;
          }
        }

        // One-shot scroll requests (top-scroll is also triggered on game selection change).
        if (scroll_checklist_top_) {
          ImGui::SetScrollY(0.0F);
          scroll_checklist_top_ = false;
        } else if (scroll_checklist_bottom_) {
          ImGui::SetScrollY(ImGui::GetScrollMaxY());
          scroll_checklist_bottom_ = false;
        }

        // Rebuild per-game ROM index cache when the selection or checklist changes.
        if (cached_rom_game_id_ != selected_game_id_ ||
            cached_rom_generation_ != rom_checklist_generation_) {
          cached_rom_game_id_ = selected_game_id_;
          cached_rom_generation_ = rom_checklist_generation_;
          selected_rom_indices_.clear();
          for (std::size_t i = 0; i < rom_checklist_.size(); ++i) {
            if (rom_checklist_[i].game_id == selected_game_id_) {
              selected_rom_indices_.push_back(i);
            }
          }
        }

        for (const std::size_t i : selected_rom_indices_) {
          const auto& entry = rom_checklist_[i];

          const ImVec4 color = status_color(entry.status);
          ImGui::TableNextRow();
          ImGui::PushID(static_cast<int>(i));

          ImGui::TableSetColumnIndex(k_ColStatus);
          ImGui::TextColored(color, "%s", status_label(entry.status));

          ImGui::TableSetColumnIndex(k_ColRomName);
          ImGui::TextColored(color, "%s", entry.name.c_str());
          if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Right-click to copy");
          }
          if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            ImGui::SetClipboardText(entry.name.c_str());
            show_toast("Name copied to clipboard");
          }

          ImGui::TableSetColumnIndex(k_ColSize);
          ImGui::TextUnformatted(format_size(entry.size).c_str());
          if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Right-click to copy (bytes)");
          }
          if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            ImGui::SetClipboardText(std::to_string(entry.size).c_str());
            show_toast("Size copied to clipboard");
          }

          ImGui::TableSetColumnIndex(k_ColSha1);
          ImGui::TextUnformatted(entry.sha1.c_str());
          if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Right-click to copy SHA1");
          }
          if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            ImGui::SetClipboardText(entry.sha1.c_str());
            show_toast("SHA1 copied to clipboard");
          }

          ImGui::TableSetColumnIndex(k_ColMd5);
          ImGui::TextUnformatted(entry.md5.c_str());
          if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Right-click to copy MD5");
          }
          if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            ImGui::SetClipboardText(entry.md5.c_str());
            show_toast("MD5 copied to clipboard");
          }

          ImGui::TableSetColumnIndex(k_ColCrc32);
          ImGui::TextUnformatted(entry.crc32.c_str());
          if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Right-click to copy CRC32");
          }
          if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            ImGui::SetClipboardText(entry.crc32.c_str());
            show_toast("CRC32 copied to clipboard");
          }

          ImGui::PopID();
        }
        ImGui::EndTable();
      }
    }
  }

  ImGui::EndChild(); // ##roms_panel
}

void GuiApp::apply_checklist_sort() {
  if (checklist_sort_col_ < 0 || rom_checklist_.empty()) {
    return;
  }
  ++rom_checklist_generation_; // Invalidate the per-game ROM index cache.
  const int col = checklist_sort_col_;
  const bool asc = checklist_sort_ascending_;

  std::stable_sort(rom_checklist_.begin(),
                   rom_checklist_.end(),
                   [col, asc](const RomChecklistEntry& a, const RomChecklistEntry& b) {
                     switch (col) {
                       case k_ColStatus: {
                         const int sa = status_sort_order(a.status);
                         const int sb = status_sort_order(b.status);
                         return asc ? sa < sb : sb < sa;
                       }
                       case k_ColRomName:
                         return asc ? a.name < b.name : b.name < a.name;
                       case k_ColSize:
                         return asc ? a.size < b.size : b.size < a.size;
                       case k_ColSha1:
                         return asc ? a.sha1 < b.sha1 : b.sha1 < a.sha1;
                       case k_ColMd5:
                         return asc ? a.md5 < b.md5 : b.md5 < a.md5;
                       case k_ColCrc32:
                         return asc ? a.crc32 < b.crc32 : b.crc32 < a.crc32;
                       default:
                         return false;
                     }
                   });
}

void GuiApp::apply_game_sort() {
  if (game_sort_col_ < 0 || game_checklist_.empty()) {
    return;
  }
  const int col = game_sort_col_;
  const bool asc = game_sort_ascending_;

  std::stable_sort(game_checklist_.begin(),
                   game_checklist_.end(),
                   [col, asc](const GameChecklistEntry& a, const GameChecklistEntry& b) {
                     switch (col) {
                       case k_GameColStatus: {
                         const int sa = status_sort_order(a.status);
                         const int sb = status_sort_order(b.status);
                         return asc ? sa < sb : sb < sa;
                       }
                       case k_GameColName:
                         return asc ? a.name < b.name : b.name < a.name;
                       default:
                         return false;
                     }
                   });
}

} // namespace romulus::gui
