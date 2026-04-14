#include "gui_app.hpp"

#include "gui_app_shared.hpp"
#include "romulus/core/logging.hpp"

// ImGui and backends — included as system headers to avoid third-party warnings.
// The SYSTEM include paths are set in CMakeLists.txt.
#if defined(__GNUC__) || defined(__clang__)
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wold-style-cast"
#endif
#include <imgui.h>
#if defined(__GNUC__) || defined(__clang__)
  #pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <charconv>
#include <cmath>
#include <ranges>
#include <string>

namespace romulus::gui {

void GuiApp::render_db_tab() {
  const bool busy = is_busy();

  // ── Database path (disabled text field + Browse button) ───────
  // Shows the full path to the active SQLite database.
  // Both fields are disabled for now — future versions may allow switching DBs.
  {
    std::array<char, 512> path_buf{};
    const std::string db_path_str = svc_.get_db_path().string();
    const auto copy_len = std::min(db_path_str.size(), path_buf.size() - 1U);
    db_path_str.copy(path_buf.data(), copy_len);
    path_buf[copy_len] = '\0';

    ImGui::BeginDisabled(true);
    ImGui::PushItemWidth(-220.0F);
    ImGui::InputText("##db_path", path_buf.data(), path_buf.size());
    ImGui::PopItemWidth();
    ImGui::SameLine();
    ImGui::Button("Browse...");
    ImGui::EndDisabled();
  }

  ImGui::SameLine();

  // ── Read DB button ────────────────────────────────────────────
  ImGui::BeginDisabled(busy);
  if (ImGui::Button("Read DB")) {
    auto result = svc_.get_db_table_names();
    if (result) {
      db_table_names_ = std::move(*result);
      selected_db_table_index_ = -1;
      db_table_data_ = {};
      db_table_lower_rows_.clear();
      db_tab_loaded_ = true;
      db_filter_buf_.fill('\0');
      db_filter_lower_.clear();
      db_sort_col_ = -1;
      db_sort_ascending_ = true;
      db_view_dirty_ = true;
    } else {
      ROMULUS_WARN("DB Explorer: failed to read table names: {}", result.error().message);
      show_toast("Failed to read DB: " + result.error().message);
    }
  }
  ImGui::EndDisabled();

  ImGui::Spacing();

  if (!db_tab_loaded_) {
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 20.0F);
    ImGui::TextDisabled("Click 'Read DB' to explore the database tables.");
    return;
  }

  if (db_table_names_.empty()) {
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 20.0F);
    ImGui::TextDisabled("No tables found in the database.");
    return;
  }

  // ── Tables dropdown ───────────────────────────────────────────
  // Disabled when a background task is running — both the combo interaction
  // and the resulting svc_.query_db_table() call touch the same SQLite
  // connection used by the background task, which is not thread-safe.
  {
    const std::string table_preview =
        (selected_db_table_index_ >= 0 &&
         selected_db_table_index_ < static_cast<int>(db_table_names_.size()))
            ? db_table_names_[static_cast<std::size_t>(selected_db_table_index_)]
            : "(Select a table)";

    ImGui::BeginDisabled(busy);
    ImGui::PushItemWidth(300.0F);
    if (ImGui::BeginCombo("##db_table_combo", table_preview.c_str())) {
      for (int i = 0; i < static_cast<int>(db_table_names_.size()); ++i) {
        bool is_selected = (selected_db_table_index_ == i);
        if (ImGui::Selectable(db_table_names_[static_cast<std::size_t>(i)].c_str(),
                              is_selected)) {
          selected_db_table_index_ = i;
          auto data = svc_.query_db_table(db_table_names_[static_cast<std::size_t>(i)]);
          if (data) {
            db_table_data_ = std::move(*data);
          } else {
            ROMULUS_WARN("DB Explorer: failed to query table '{}': {}",
                         db_table_names_[static_cast<std::size_t>(i)],
                         data.error().message);
            show_toast("Failed to read table: " + data.error().message);
            db_table_data_ = {};
          }
          rebuild_db_lower_cache();
          // Reset filter, sort, and nav state for the new table.
          db_filter_buf_.fill('\0');
          db_filter_lower_.clear();
          db_sort_col_ = -1;
          db_sort_ascending_ = true;
          db_view_dirty_ = true;
        }
        if (is_selected) {
          ImGui::SetItemDefaultFocus();
        }
      }
      ImGui::EndCombo();
    }
    ImGui::PopItemWidth();
    ImGui::EndDisabled();
  }

  if (selected_db_table_index_ < 0) {
    ImGui::Spacing();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 20.0F);
    ImGui::TextDisabled("Select a table from the dropdown above to view its contents.");
    return;
  }

  ImGui::SameLine();
  if (db_filter_lower_.empty()) {
    ImGui::TextDisabled("(%zu rows)", db_table_data_.rows.size());
  } else {
    ImGui::TextDisabled("(%zu / %zu rows)", db_display_rows_.size(), db_table_data_.rows.size());
  }

  ImGui::Spacing();

  if (db_table_data_.columns.empty()) {
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 20.0F);
    ImGui::TextDisabled("No data available for this table.");
    return;
  }

  // ── Schema panel ─────────────────────────────────────────────
  // Collapsible summary of column metadata (type, PK, NN, UQ, FK).
  constexpr ImVec4 k_ColorPk{1.0F, 0.80F, 0.10F, 1.0F}; // gold   — primary key
  constexpr ImVec4 k_ColorFk{0.40F, 0.75F, 1.0F, 1.0F}; // blue   — foreign key
  constexpr ImVec4 k_ColorUq{0.80F, 0.50F, 1.0F, 1.0F}; // purple — unique
  constexpr ImVec4 k_ColorNn{0.70F, 0.70F, 0.70F, 1.0F};// grey   — not null

  if (ImGui::CollapsingHeader("Schema", ImGuiTreeNodeFlags_DefaultOpen)) {
    constexpr int k_SchemaCols = 3;
    constexpr ImGuiTableFlags k_SchemaFlags =
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit;
    if (ImGui::BeginTable("##schema_info", k_SchemaCols, k_SchemaFlags)) {
      ImGui::TableSetupColumn("Column",  ImGuiTableColumnFlags_None, 140.0F);
      ImGui::TableSetupColumn("Type",    ImGuiTableColumnFlags_None, 90.0F);
      ImGui::TableSetupColumn("Flags",   ImGuiTableColumnFlags_None, 0.0F);
      ImGui::TableHeadersRow();

      for (const auto& col : db_table_data_.columns) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(col.name.c_str());

        ImGui::TableSetColumnIndex(1);
        ImGui::TextDisabled("%s", col.type.empty() ? "-" : col.type.c_str());

        ImGui::TableSetColumnIndex(2);
        bool any = false;
        // Primary key (always first, most important)
        if (col.is_primary_key) {
          ImGui::TextColored(k_ColorPk, "[PK]");
          any = true;
        }
        // Not-null (only show when not implied by PK, which is inherently NN)
        if (col.not_null && !col.is_primary_key) {
          if (any) { ImGui::SameLine(); }
          ImGui::TextColored(k_ColorNn, "[NN]");
          any = true;
        }
        // Unique index (skip if column is already a PK — that implies uniqueness)
        if (col.is_unique && !col.is_primary_key) {
          if (any) { ImGui::SameLine(); }
          ImGui::TextColored(k_ColorUq, "[UQ]");
          any = true;
        }
        // Foreign key with target table.column
        if (!col.fk_table.empty()) {
          if (any) { ImGui::SameLine(); }
          std::string fk_label = "[FK]->" + col.fk_table;
          if (!col.fk_column.empty()) {
            fk_label += '.' + col.fk_column;
          }
          ImGui::TextColored(k_ColorFk, "%s", fk_label.c_str());
        }
      }
      ImGui::EndTable();
    }
    ImGui::Spacing();
  }

  // ── Data table (read-only, sortable, filterable) ─────────────
  const int col_count = static_cast<int>(db_table_data_.columns.size());

  // Filter bar
  {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Filter:");
    ImGui::SameLine(0.0F, 4.0F);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (ImGui::InputText("##db_filter", db_filter_buf_.data(), k_DbMaxFilterLen)) {
      db_filter_lower_.assign(db_filter_buf_.data());
      std::ranges::transform(db_filter_lower_, db_filter_lower_.begin(), ascii_lower);
      db_view_dirty_ = true;
    }
  }

  ImGui::Spacing();

  // Table + nav-strip layout (mirrors DATs tab pattern)
  constexpr float k_NavStripW = 24.0F;
  const float nav_gap = ImGui::GetStyle().ItemSpacing.x;

  // Build a unique table ID from the selected table name so ImGui doesn't persist
  // sort specs from a previous table when the user switches to a different one.
  std::string db_table_id = "##db_view:";
  db_table_id += db_table_names_[static_cast<std::size_t>(selected_db_table_index_)];

  constexpr ImGuiTableFlags k_TableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                           ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY |
                                           ImGuiTableFlags_SizingFixedFit |
                                           ImGuiTableFlags_Resizable |
                                           ImGuiTableFlags_Sortable;

  ImGui::BeginGroup();
  if (ImGui::BeginTable(db_table_id.c_str(),
                        col_count,
                        k_TableFlags,
                        ImVec2(-k_NavStripW - nav_gap, -30))) {
    ImGui::TableSetupScrollFreeze(0, 1);
    for (int c = 0; c < col_count; ++c) {
      ImGui::TableSetupColumn(db_table_data_.columns[static_cast<std::size_t>(c)].name.c_str(),
                              ImGuiTableColumnFlags_None);
    }
    ImGui::TableHeadersRow();

    // Consume sort-spec changes from ImGui — must happen before apply_db_filter_sort()
    // so sort and filter are recomputed in a single pass per frame.
    if (auto* sort_specs = ImGui::TableGetSortSpecs()) {
      if (sort_specs->SpecsDirty) {
        if (sort_specs->SpecsCount > 0) {
          db_sort_col_ = sort_specs->Specs[0].ColumnIndex;
          db_sort_ascending_ =
              (sort_specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending);
        } else {
          db_sort_col_ = -1;
        }
        db_view_dirty_ = true;
        sort_specs->SpecsDirty = false;
      }
    }

    // Recompute filtered+sorted view at most once per frame, after sort specs are consumed.
    if (db_view_dirty_) {
      apply_db_filter_sort();
      db_view_dirty_ = false;
    }

    // One-shot scroll requests.
    if (scroll_db_top_) {
      ImGui::SetScrollY(0.0F);
      scroll_db_top_ = false;
    } else if (scroll_db_bottom_) {
      ImGui::SetScrollY(ImGui::GetScrollMaxY());
      scroll_db_bottom_ = false;
    }

    for (const std::size_t r : db_display_rows_) {
      ImGui::TableNextRow();
      ImGui::PushID(static_cast<int>(r));
      const auto& row = db_table_data_.rows[r];
      for (int c = 0; c < col_count; ++c) {
        ImGui::TableSetColumnIndex(c);
        const std::string& cell =
            (c < static_cast<int>(row.size())) ? row[static_cast<std::size_t>(c)] : "";
        ImGui::TextUnformatted(cell.c_str());
        if (ImGui::IsItemHovered()) {
          ImGui::SetTooltip("Right-click to copy");
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
          ImGui::SetClipboardText(cell.c_str());
          show_toast("Copied to clipboard");
        }
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
    ImGui::BeginChild("##db_nav_strip",
                      ImVec2(k_NavStripW, strip_h),
                      false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const float v_pad = (strip_h - total_btn_h) * 0.5F;
    if (v_pad > 0.0F) {
      ImGui::SetCursorPosY(ImGui::GetCursorPosY() + v_pad);
    }
    if (ImGui::Button("^")) {
      scroll_db_top_ = true;
    }
    if (ImGui::Button("v")) {
      scroll_db_bottom_ = true;
    }
    ImGui::EndChild();
  }
}


void GuiApp::rebuild_db_lower_cache() {
  db_table_lower_rows_.clear();
  db_table_lower_rows_.reserve(db_table_data_.rows.size());
  for (const auto& row : db_table_data_.rows) {
    auto& lr = db_table_lower_rows_.emplace_back();
    lr.reserve(row.size());
    for (const auto& cell : row) {
      std::string lower = cell;
      std::ranges::transform(lower, lower.begin(), ascii_lower);
      lr.push_back(std::move(lower));
    }
  }
}

void GuiApp::apply_db_filter_sort() {
  const std::size_t row_count = db_table_data_.rows.size();
  db_display_rows_.clear();
  db_display_rows_.reserve(row_count);

  // Build filtered index list using the pre-lowercased cell cache to avoid
  // per-cell string allocations while filtering.
  for (std::size_t i = 0; i < row_count; ++i) {
    if (db_filter_lower_.empty()) {
      db_display_rows_.push_back(i);
      continue;
    }
    bool matches = false;
    if (i < db_table_lower_rows_.size()) {
      for (const auto& cell_lower : db_table_lower_rows_[i]) {
        if (cell_lower.find(db_filter_lower_) != std::string::npos) {
          matches = true;
          break;
        }
      }
    }
    if (matches) {
      db_display_rows_.push_back(i);
    }
  }

  // Sort the filtered indices.
  if (db_sort_col_ < 0 ||
      db_sort_col_ >= static_cast<int>(db_table_data_.columns.size())) {
    return;
  }
  const std::size_t sort_col = static_cast<std::size_t>(db_sort_col_);
  const bool ascending = db_sort_ascending_;

  // Normalise declared type to lowercase to handle columns declared as e.g.
  // "integer" or "Integer" as well as the canonical "INTEGER".
  std::string col_type_lower = db_table_data_.columns[sort_col].type;
  std::ranges::transform(col_type_lower, col_type_lower.begin(), ascii_lower);

  // Follow SQLite type affinity rules.  A column has INTEGER affinity when its
  // declared type contains "int"; REAL affinity when it contains "real", "floa",
  // or "doub"; NUMERIC affinity when it contains "numeric".
  const bool is_numeric = col_type_lower.find("int") != std::string::npos ||
                          col_type_lower.find("real") != std::string::npos ||
                          col_type_lower.find("floa") != std::string::npos ||
                          col_type_lower.find("doub") != std::string::npos ||
                          col_type_lower.find("numeric") != std::string::npos;

  std::stable_sort(
      db_display_rows_.begin(),
      db_display_rows_.end(),
      [&](std::size_t a, std::size_t b) {
        const auto& ra = db_table_data_.rows[a];
        const auto& rb = db_table_data_.rows[b];
        const std::string& va = (sort_col < ra.size()) ? ra[sort_col] : "";
        const std::string& vb = (sort_col < rb.size()) ? rb[sort_col] : "";
        // Use separate a_less / b_less to maintain strict-weak-ordering when equal.
        bool a_less = false;
        bool b_less = false;
        if (is_numeric) {
          // std::from_chars is locale-independent unlike strtod.
          // Unparseable cells (NULL, empty, non-numeric text) default to 0.0.
          double na = 0.0;
          double nb = 0.0;
          std::from_chars(va.data(), va.data() + va.size(), na);
          std::from_chars(vb.data(), vb.data() + vb.size(), nb);
          a_less = na < nb;
          b_less = nb < na;
        } else {
          a_less = va < vb;
          b_less = vb < va;
        }
        return ascending ? a_less : b_less;
      });
}

} // namespace romulus::gui
