#include "gui_app.hpp"

#include "gui_app_shared.hpp"

namespace romulus::gui {

void GuiApp::render_folders_tab() {
  const bool busy = is_busy();

  ImGui::BeginDisabled(busy);
  if (ImGui::Button("Add Folder")) {
    action_add_rom_folder();
  }
  ImGui::EndDisabled();

  if (!scanned_dirs_.empty()) {
    ImGui::SameLine();
    ImGui::BeginDisabled(busy);
    if (ImGui::Button("Rescan All")) {
      action_rescan_folders();
    }
    ImGui::EndDisabled();
  }

  ImGui::Spacing();

  if (scanned_dirs_.empty()) {
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 20.0F);
    ImGui::TextDisabled("No folders added yet. Click 'Add Folder' to register a ROM directory.");
    return;
  }

  // Folder list table
  constexpr int k_FolderCols = 3;
  if (ImGui::BeginTable("folders_table",
                        k_FolderCols,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                            ImGuiTableFlags_SizingStretchProp,
                        ImVec2(0, -30))) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Path",    ImGuiTableColumnFlags_None, 6.0F);
    ImGui::TableSetupColumn("Files",   ImGuiTableColumnFlags_None, 1.0F);
    ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_None, 0.6F);
    ImGui::TableHeadersRow();

    std::int64_t to_remove = -1;
    for (std::size_t i = 0; i < scanned_dirs_.size(); ++i) {
      const auto& dir = scanned_dirs_[i];
      ImGui::TableNextRow();
      ImGui::PushID(static_cast<int>(i));

      ImGui::TableSetColumnIndex(0);
      ImGui::TextUnformatted(dir.path.c_str());
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Right-click to copy path");
      }
      if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        ImGui::SetClipboardText(dir.path.c_str());
        show_toast("Path copied to clipboard");
      }

      ImGui::TableSetColumnIndex(1);
      ImGui::TextDisabled("%lld", static_cast<long long>(dir.file_count));

      ImGui::TableSetColumnIndex(2);
      ImGui::BeginDisabled(busy);
      if (ImGui::SmallButton("[X]")) {
        to_remove = dir.id;
      }
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Remove this folder");
      }
      ImGui::EndDisabled();

      ImGui::PopID();
    }
    ImGui::EndTable();

    if (to_remove >= 0) {
      action_remove_folder(to_remove);
    }
  }
}

} // namespace romulus::gui
