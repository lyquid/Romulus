#include "gui_app.hpp"
#include "gui_app_shared.hpp"

#include <algorithm>
#include <string>

namespace romulus::gui {

void GuiApp::render_dats_tab() {
  const bool busy = is_busy();
  bool selection_changed = false;

  // ── DAT context controls ─────────────────────────────────────
  ImGui::BeginDisabled(busy);
  if (ImGui::Button("Import DAT")) {
    action_import_dat();
  }
  ImGui::SameLine();

  std::string preview = "(No DAT selected)";
  if (selected_dat_index_ >= 0 && selected_dat_index_ < static_cast<int>(dat_versions_.size())) {
    const auto& dv = dat_versions_[static_cast<std::size_t>(selected_dat_index_)];
    preview = dv.name + " v" + dv.version;
  }

  const float frame_px = ImGui::GetStyle().FramePadding.x;
  const float spacing = ImGui::GetStyle().ItemSpacing.x;
  const float refresh_w = ImGui::CalcTextSize("Refresh Audit").x + frame_px * 2.0F;
  const float delete_w = ImGui::CalcTextSize("Delete DAT").x + frame_px * 2.0F;
  ImGui::SetNextItemWidth(-(refresh_w + delete_w + spacing * 2.0F));
  if (ImGui::BeginCombo("##dat_combo", preview.c_str())) {
    for (int i = 0; i < static_cast<int>(dat_versions_.size()); ++i) {
      const auto& dv = dat_versions_[static_cast<std::size_t>(i)];
      std::string label = dv.name + " v" + dv.version;
      if (!dv.imported_at.empty()) {
        label += "  (" + dv.imported_at + ")";
      }
      const bool selected = selected_dat_index_ == i;
      if (ImGui::Selectable(label.c_str(), selected) && !selected) {
        selected_dat_index_ = i;
        dat_audit_ = {};
        dat_audit_loaded_ = false;
        dat_audit_filter_ = DatAuditFilter::All;
        selection_changed = true;
      }
      if (selected) {
        ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndCombo();
  }

  ImGui::SameLine();
  ImGui::BeginDisabled(selected_dat_index_ < 0);
  if (ImGui::Button("Refresh Audit")) {
    action_check_dat();
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Re-match existing hashes and refresh the audit; no files are scanned or "
                      "re-hashed");
  }

  ImGui::SameLine();
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55F, 0.12F, 0.12F, 1.0F));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75F, 0.18F, 0.18F, 1.0F));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.85F, 0.22F, 0.22F, 1.0F));
  if (ImGui::Button("Delete DAT")) {
    show_delete_dat_confirm_ = true;
  }
  ImGui::PopStyleColor(3);
  ImGui::EndDisabled();
  ImGui::EndDisabled();

  if (selection_changed) {
    action_check_dat();
  }

  // ── Active DAT banner ────────────────────────────────────────
  ImGui::Spacing();
  {
    const float line_h = ImGui::GetTextLineHeightWithSpacing();
    const float banner_h = line_h + ImGui::GetStyle().FramePadding.y * 2.0F + k_BannerExtraPadding;
    const float v_pad = (banner_h - line_h) * 0.5F - ImGui::GetStyle().WindowPadding.y;
    const bool has_dat =
        selected_dat_index_ >= 0 && selected_dat_index_ < static_cast<int>(dat_versions_.size());
    const ImVec4 bg =
        has_dat ? ImVec4(0.08F, 0.16F, 0.32F, 1.0F) : ImVec4(0.10F, 0.10F, 0.12F, 1.0F);

    ImGui::PushStyleColor(ImGuiCol_ChildBg, bg);
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
        ImGui::TextColored(ImVec4(0.45F, 0.75F, 1.0F, 1.0F), "Audit context");
        ImGui::SameLine();
        ImGui::Text("%s v%s", dv.name.c_str(), dv.version.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("| hashes are reused when this DAT changes");
      } else {
        ImGui::TextDisabled("No DAT selected");
      }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
  }

  ImGui::Spacing();
  if (selected_dat_index_ < 0) {
    ImGui::TextDisabled("Import or select a DAT to audit the scanned collection.");
    return;
  }
  if (!dat_audit_loaded_) {
    ImGui::TextDisabled("%s",
                        busy ? "Building DAT audit from existing hashes..."
                             : "Audit data is not available. Click Refresh Audit.");
    return;
  }

  // ── Clickable summary filters ────────────────────────────────
  const auto& summary = dat_audit_.summary;
  const auto weak_count = summary.crc_match + summary.md5_match;
  auto summary_button =
      [this](const std::string& text, DatAuditFilter filter, const ImVec4& color) {
        const bool active = dat_audit_filter_ == filter;
        const ImVec4 button_color =
            active ? color : ImVec4(color.x * 0.45F, color.y * 0.45F, color.z * 0.45F, 1.0F);
        ImGui::PushStyleColor(ImGuiCol_Button, button_color);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              ImVec4(color.x * 0.8F, color.y * 0.8F, color.z * 0.8F, 1.0F));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, color);
        if (ImGui::Button(text.c_str())) {
          dat_audit_filter_ = filter;
          scroll_dat_audit_top_ = true;
        }
        ImGui::PopStyleColor(3);
      };

  summary_button("All " + std::to_string(dat_audit_.rows.size()) + "##audit_all",
                 DatAuditFilter::All,
                 ImVec4(0.38F, 0.48F, 0.68F, 1.0F));
  ImGui::SameLine();
  summary_button("Correct " + std::to_string(summary.correct) + "##audit_correct",
                 DatAuditFilter::Correct,
                 k_ColorVerified);
  ImGui::SameLine();
  summary_button("Missing " + std::to_string(summary.missing) + "##audit_missing",
                 DatAuditFilter::Missing,
                 k_ColorMissing);
  ImGui::SameLine();
  summary_button("Wrong name " + std::to_string(summary.wrong_name) + "##audit_name",
                 DatAuditFilter::WrongName,
                 k_ColorWrongName);
  ImGui::SameLine();
  summary_button("Extra " + std::to_string(summary.extra) + "##audit_extra",
                 DatAuditFilter::Extra,
                 k_ColorExtra);
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("%lld match other imported DATs; %lld are globally unknown",
                      static_cast<long long>(summary.extra_known_other_dat),
                      static_cast<long long>(summary.globally_unknown));
  }
  ImGui::SameLine();
  summary_button("Duplicates " + std::to_string(summary.duplicate_files) + "##audit_dupes",
                 DatAuditFilter::Duplicate,
                 k_ColorDuplicate);
  ImGui::SameLine();
  summary_button("Weak " + std::to_string(weak_count) + "##audit_weak",
                 DatAuditFilter::WeakMatch,
                 k_ColorCrcMatch);
  ImGui::SameLine();
  summary_button("Conflicts " + std::to_string(summary.hash_conflict) + "##audit_conflict",
                 DatAuditFilter::HashConflict,
                 k_ColorHashConflict);
  ImGui::SameLine();
  summary_button("Mismatch " + std::to_string(summary.mismatch) + "##audit_mismatch",
                 DatAuditFilter::Mismatch,
                 k_ColorMismatch);

  ImGui::Spacing();

  // ── Explainable audit table ──────────────────────────────────
  constexpr float k_StatusW = 190.0F;
  constexpr float k_GameW = 190.0F;
  constexpr float k_CanonicalW = 230.0F;
  constexpr float k_ActualW = 210.0F;
  constexpr float k_SizeW = 80.0F;
  constexpr float k_LocationW = 300.0F;
  constexpr float k_ReasonW = 480.0F;
  constexpr float k_ActionW = 360.0F;
  constexpr float k_InnerW = k_StatusW + k_GameW + k_CanonicalW + k_ActualW + k_SizeW +
                             k_LocationW + k_ReasonW + k_ActionW + 20.0F;
  constexpr int k_ColumnCount = 8;

  if (ImGui::BeginTable("dat_audit_table",
                        k_ColumnCount,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                            ImGuiTableFlags_ScrollX | ImGuiTableFlags_Resizable |
                            ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Sortable,
                        ImVec2(0.0F, -ImGui::GetFrameHeightWithSpacing()),
                        k_InnerW)) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_DefaultSort, k_StatusW);
    ImGui::TableSetupColumn("Game", ImGuiTableColumnFlags_None, k_GameW);
    ImGui::TableSetupColumn("Canonical name", ImGuiTableColumnFlags_None, k_CanonicalW);
    ImGui::TableSetupColumn("Actual name", ImGuiTableColumnFlags_None, k_ActualW);
    ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_None, k_SizeW);
    ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_None, k_LocationW);
    ImGui::TableSetupColumn("Why", ImGuiTableColumnFlags_None, k_ReasonW);
    ImGui::TableSetupColumn("Suggested next step", ImGuiTableColumnFlags_None, k_ActionW);
    ImGui::TableHeadersRow();

    if (auto* specs = ImGui::TableGetSortSpecs(); specs != nullptr && specs->SpecsDirty) {
      if (specs->SpecsCount > 0) {
        dat_audit_sort_col_ = specs->Specs[0].ColumnIndex;
        dat_audit_sort_ascending_ = specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending;
      }
      apply_audit_sort();
      specs->SpecsDirty = false;
    }

    if (scroll_dat_audit_top_) {
      ImGui::SetScrollY(0.0F);
      scroll_dat_audit_top_ = false;
    }

    for (std::size_t i = 0; i < dat_audit_.rows.size(); ++i) {
      const auto& row = dat_audit_.rows[i];
      if (!audit_filter_matches(dat_audit_filter_, row.status)) {
        continue;
      }

      ImGui::TableNextRow();
      ImGui::PushID(static_cast<int>(i));
      const ImVec4 color = audit_status_color(row.status);

      ImGui::TableSetColumnIndex(k_AuditColStatus);
      ImGui::TextColored(color, "%s", audit_status_label(row.status));

      ImGui::TableSetColumnIndex(k_AuditColGame);
      ImGui::TextUnformatted(row.game_name.empty() ? "--" : row.game_name.c_str());

      ImGui::TableSetColumnIndex(k_AuditColCanonicalName);
      ImGui::TextUnformatted(row.canonical_name.empty() ? "--" : row.canonical_name.c_str());

      ImGui::TableSetColumnIndex(k_AuditColActualName);
      ImGui::TextUnformatted(row.actual_name.empty() ? "--" : row.actual_name.c_str());

      ImGui::TableSetColumnIndex(k_AuditColSize);
      if (row.size > 0) {
        ImGui::TextUnformatted(format_size(row.size).c_str());
      } else {
        ImGui::TextDisabled("--");
      }

      ImGui::TableSetColumnIndex(k_AuditColLocation);
      if (row.file_path.empty()) {
        ImGui::TextDisabled("--");
      } else {
        ImGui::TextUnformatted(row.file_path.c_str());
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
          ImGui::SetClipboardText(row.file_path.c_str());
          show_toast("File location copied to clipboard");
        }
      }

      ImGui::TableSetColumnIndex(k_AuditColReason);
      ImGui::TextUnformatted(row.reason.c_str());

      ImGui::TableSetColumnIndex(k_AuditColAction);
      ImGui::TextUnformatted(row.suggested_action.c_str());
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
}

void GuiApp::apply_audit_sort() {
  if (dat_audit_.rows.empty()) {
    return;
  }
  const int column = dat_audit_sort_col_;
  const bool ascending = dat_audit_sort_ascending_;
  std::stable_sort(
      dat_audit_.rows.begin(),
      dat_audit_.rows.end(),
      [column, ascending](const core::DatAuditRow& left, const core::DatAuditRow& right) {
        auto compare = [ascending](const auto& lhs, const auto& rhs) {
          return ascending ? lhs < rhs : rhs < lhs;
        };
        switch (column) {
          case k_AuditColStatus:
            return compare(audit_status_sort_order(left.status),
                           audit_status_sort_order(right.status));
          case k_AuditColGame:
            return compare(left.game_name, right.game_name);
          case k_AuditColCanonicalName:
            return compare(left.canonical_name, right.canonical_name);
          case k_AuditColActualName:
            return compare(left.actual_name, right.actual_name);
          case k_AuditColSize:
            return compare(left.size, right.size);
          case k_AuditColLocation:
            return compare(left.file_path, right.file_path);
          case k_AuditColReason:
            return compare(left.reason, right.reason);
          case k_AuditColAction:
            return compare(left.suggested_action, right.suggested_action);
          default:
            return false;
        }
      });
}

} // namespace romulus::gui
