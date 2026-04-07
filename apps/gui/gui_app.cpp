#include "gui_app.hpp"

#include "file_dialog.hpp"
#include "romulus/core/logging.hpp"

// ImGui and backends — included as system headers to avoid third-party warnings.
// The SYSTEM include paths are set in CMakeLists.txt.
#if defined(__GNUC__) || defined(__clang__)
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wold-style-cast"
#endif
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#if defined(__GNUC__) || defined(__clang__)
  #pragma GCC diagnostic pop
#endif

// GLFW must come after imgui backend headers
// NOLINTNEXTLINE(misc-include-cleaner)
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <future>
#include <GLFW/glfw3.h>
#include <stdexcept>
#include <string>

namespace romulus::gui {

namespace {

constexpr int k_WindowWidth = 1280;
constexpr int k_WindowHeight = 720;
constexpr auto* k_WindowTitle = "ROMULUS — ROM Collection Verifier";
constexpr auto* k_GlslVersion = "#version 130";
constexpr float k_ToastDuration = 2.5F;
constexpr float k_ToastWidth = 310.0F;
constexpr float k_ToastHeight = 36.0F;
constexpr float k_ToastMarginRight = 10.0F;
constexpr float k_ToastMarginBottom = 50.0F;

// ROM checklist column indices
constexpr int k_ColStatus = 0;
constexpr int k_ColRomName = 1;
constexpr int k_ColSize = 2;
constexpr int k_ColCrc32 = 3;

// Status colours
constexpr ImVec4 k_ColorVerified{0.2F, 0.9F, 0.3F, 1.0F};   // green
constexpr ImVec4 k_ColorMissing{1.0F, 0.3F, 0.3F, 1.0F};    // red
constexpr ImVec4 k_ColorUnverified{1.0F, 0.9F, 0.2F, 1.0F};  // yellow
constexpr ImVec4 k_ColorMismatch{1.0F, 0.5F, 0.0F, 1.0F};    // orange

// Log panel colour scheme (RGBA)
constexpr ImVec4 k_ColorLogWarn{1.0F, 0.75F, 0.1F, 1.0F};   // amber  — warnings
constexpr ImVec4 k_ColorLogError{1.0F, 0.3F, 0.3F, 1.0F};   // red    — errors / critical
constexpr ImVec4 k_ColorLogDebug{0.6F, 0.6F, 0.6F, 1.0F};   // grey   — debug / trace
constexpr ImVec4 k_ColorLogDefault{1.0F, 1.0F, 1.0F, 1.0F}; // white  — info

void glfw_error_callback(int error, const char* description) {
  std::fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

/// Formats a byte count as a human-readable string (B, KB, MB, GB).
auto format_size(std::int64_t bytes) -> std::string {
  constexpr std::int64_t k_Kilo = 1024;
  constexpr std::int64_t k_Mega = 1024 * 1024;
  constexpr std::int64_t k_Giga = 1024 * 1024 * 1024;

  char buf[32];
  if (bytes >= k_Giga) {
    std::snprintf(
        buf, sizeof(buf), "%.1f GB", static_cast<double>(bytes) / static_cast<double>(k_Giga));
  } else if (bytes >= k_Mega) {
    std::snprintf(
        buf, sizeof(buf), "%.1f MB", static_cast<double>(bytes) / static_cast<double>(k_Mega));
  } else if (bytes >= k_Kilo) {
    std::snprintf(
        buf, sizeof(buf), "%.1f KB", static_cast<double>(bytes) / static_cast<double>(k_Kilo));
  } else {
    std::snprintf(buf, sizeof(buf), "%lld B", static_cast<long long>(bytes));
  }
  return buf;
}

auto status_label(core::RomStatusType status) -> const char* {
  switch (status) {
    case core::RomStatusType::Verified:
      return "  Available";
    case core::RomStatusType::Missing:
      return "  Missing";
    case core::RomStatusType::Unverified:
      return "? Unverified";
    case core::RomStatusType::Mismatch:
      return "! Mismatch";
  }
  return "? Unknown";
}

auto status_color(core::RomStatusType status) -> ImVec4 {
  switch (status) {
    case core::RomStatusType::Verified:
      return k_ColorVerified;
    case core::RomStatusType::Missing:
      return k_ColorMissing;
    case core::RomStatusType::Unverified:
      return k_ColorUnverified;
    case core::RomStatusType::Mismatch:
      return k_ColorMismatch;
  }
  return k_ColorMissing;
}

auto status_sort_order(core::RomStatusType status) -> int {
  switch (status) {
    case core::RomStatusType::Missing:
      return 0;
    case core::RomStatusType::Mismatch:
      return 1;
    case core::RomStatusType::Unverified:
      return 2;
    case core::RomStatusType::Verified:
      return 3;
  }
  return 4;
}

} // namespace

// ═════════════════════════════════════════════════════════════════
// Lifecycle
// ═════════════════════════════════════════════════════════════════

GuiApp::GuiApp(service::RomulusService& svc, std::shared_ptr<GuiLogSink> log_sink)
    : svc_(svc), log_sink_(std::move(log_sink)) {
  init_glfw();
  init_imgui();

  status_message_ = "Ready.";
  refresh_dat_versions();
}

GuiApp::~GuiApp() {
  // Wait for any pending background task to finish before shutting down
  if (pending_task_ && pending_task_->result.valid()) {
    pending_task_->result.wait();
  }
  shutdown();
}

// ═════════════════════════════════════════════════════════════════
// Initialization
// ═════════════════════════════════════════════════════════════════

void GuiApp::init_glfw() {
  glfwSetErrorCallback(glfw_error_callback);
  if (glfwInit() == 0) {
    throw std::runtime_error("Failed to initialize GLFW");
  }

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);

  window_ = glfwCreateWindow(k_WindowWidth, k_WindowHeight, k_WindowTitle, nullptr, nullptr);
  if (window_ == nullptr) {
    glfwTerminate();
    throw std::runtime_error("Failed to create GLFW window");
  }

  glfwMakeContextCurrent(window_);
  glfwSwapInterval(1);
}

void GuiApp::init_imgui() {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();

  auto& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

  ImGui::StyleColorsDark();

  ImGui_ImplGlfw_InitForOpenGL(window_, true);
  ImGui_ImplOpenGL3_Init(k_GlslVersion);
}

void GuiApp::shutdown() {
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  if (window_ != nullptr) {
    glfwDestroyWindow(window_);
    window_ = nullptr;
  }
  glfwTerminate();
}

// ═════════════════════════════════════════════════════════════════
// Main loop
// ═════════════════════════════════════════════════════════════════

void GuiApp::run() {
  while (glfwWindowShouldClose(window_) == 0) {
    glfwPollEvents();

    // Check if a background task has finished
    check_pending_task();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Full-window docking area
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    int fb_width = 0;
    int fb_height = 0;
    glfwGetFramebufferSize(window_, &fb_width, &fb_height);
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(fb_width), static_cast<float>(fb_height)));
    ImGui::Begin("ROMULUS",
                 nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_MenuBar);

    render_main_menu_bar();
    render_actions_panel();

    ImGui::Separator();
    if (ImGui::BeginTabBar("##main_tabs")) {
      if (ImGui::BeginTabItem("ROM Checklist")) {
        render_rom_checklist_panel();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Log")) {
        render_log_panel();
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }

    render_status_bar();

    ImGui::End();

    // Purge confirmation popup (must be outside the main window for proper modal)
    if (show_purge_confirm_) {
      ImGui::OpenPopup("Confirm Purge");
      show_purge_confirm_ = false;
    }
    if (ImGui::BeginPopupModal("Confirm Purge", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::Text("Are you sure you want to purge the entire database?");
      ImGui::Text("This action cannot be undone!");
      ImGui::Separator();
      if (ImGui::Button("Yes, Purge", ImVec2(120, 0))) {
        action_purge_database();
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel", ImVec2(120, 0))) {
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }

    // Toast overlay (rendered last so it appears on top)
    render_toast();

    // Render
    ImGui::Render();
    glClearColor(0.1F, 0.1F, 0.1F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    glfwSwapBuffers(window_);
  }
}

// ═════════════════════════════════════════════════════════════════
// UI Panels
// ═════════════════════════════════════════════════════════════════

void GuiApp::render_main_menu_bar() {
  if (ImGui::BeginMenuBar()) {
    if (ImGui::BeginMenu("File")) {
      if (ImGui::MenuItem("Quit", "Alt+F4")) {
        glfwSetWindowShouldClose(window_, GLFW_TRUE);
      }
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Database")) {
      if (ImGui::MenuItem("Purge Database", nullptr, false, !is_busy())) {
        show_purge_confirm_ = true;
      }
      ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
  }
}

void GuiApp::render_actions_panel() {
  const bool busy = is_busy();

  // ── DAT Management ──
  ImGui::Text("DAT File");

  // Import DAT button
  ImGui::BeginDisabled(busy);
  if (ImGui::Button("Import DAT")) {
    action_import_dat();
  }
  ImGui::EndDisabled();

  ImGui::SameLine();

  // DAT Selector Dropdown
  {
    // Build combo preview string
    std::string preview = "(No DAT selected)";
    if (selected_dat_index_ >= 0 &&
        selected_dat_index_ < static_cast<int>(dat_versions_.size())) {
      const auto& dv = dat_versions_[static_cast<std::size_t>(selected_dat_index_)];
      preview = dv.name + " v" + dv.version;
    }

    ImGui::PushItemWidth(-260);
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
          rom_checklist_.clear(); // Clear stale checklist when switching DAT
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

  // Check DAT button
  ImGui::BeginDisabled(busy || selected_dat_index_ < 0);
  if (ImGui::Button("Check DAT")) {
    action_check_dat();
  }
  ImGui::EndDisabled();

  // ── Active DAT indicator ──
  if (selected_dat_index_ >= 0 &&
      selected_dat_index_ < static_cast<int>(dat_versions_.size())) {
    const auto& dv = dat_versions_[static_cast<std::size_t>(selected_dat_index_)];
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.4F, 0.7F, 1.0F, 1.0F), "[Active: %s v%s]",
                       dv.name.c_str(), dv.version.c_str());
  }

  // ── ROM Directory ──
  ImGui::Spacing();
  ImGui::Text("ROM Directory");

  ImGui::BeginDisabled(busy);
  if (ImGui::Button("Add ROM Folder")) {
    action_add_rom_folder();
  }
  ImGui::EndDisabled();

  // Show tracked folders
  if (!scanned_dirs_.empty()) {
    ImGui::SameLine();
    ImGui::BeginDisabled(busy);
    if (ImGui::Button("Rescan All")) {
      action_rescan_folders();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::TextDisabled("(%zu folder%s tracked)",
                        scanned_dirs_.size(),
                        scanned_dirs_.size() == 1 ? "" : "s");
  }
}

void GuiApp::render_rom_checklist_panel() {
  if (rom_checklist_.empty()) {
    if (selected_dat_index_ < 0) {
      ImGui::TextDisabled("Select a DAT and click 'Check DAT' to see the ROM checklist.");
    } else {
      ImGui::TextDisabled("Click 'Check DAT' to verify and populate the ROM list.");
    }
    return;
  }

  // Summary header
  std::int64_t total = static_cast<std::int64_t>(rom_checklist_.size());
  std::int64_t available = 0;
  for (const auto& entry : rom_checklist_) {
    if (entry.status == core::RomStatusType::Verified) {
      ++available;
    }
  }
  double pct = total > 0 ? static_cast<double>(available) / static_cast<double>(total) * 100.0 : 0.0;

  ImGui::TextColored(k_ColorVerified, "%lld", static_cast<long long>(available));
  ImGui::SameLine();
  ImGui::Text("/ %lld ROMs available (%.1f%%)", static_cast<long long>(total), pct);

  // Progress bar
  float progress = total > 0 ? static_cast<float>(available) / static_cast<float>(total) : 0.0F;
  ImGui::ProgressBar(progress, ImVec2(-1, 4));
  ImGui::Spacing();

  // Table
  constexpr int k_ColumnCount = 4;
  if (ImGui::BeginTable("rom_checklist_table",
                        k_ColumnCount,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                            ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp |
                            ImGuiTableFlags_Sortable,
                        ImVec2(0, -30))) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_DefaultSort, 1.5F);
    ImGui::TableSetupColumn("ROM Name", ImGuiTableColumnFlags_None, 5.0F);
    ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_None, 1.0F);
    ImGui::TableSetupColumn("CRC32", ImGuiTableColumnFlags_None, 1.5F);
    ImGui::TableHeadersRow();

    // Apply any pending sort
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

    for (std::size_t i = 0; i < rom_checklist_.size(); ++i) {
      const auto& entry = rom_checklist_[i];
      ImVec4 color = status_color(entry.status);

      ImGui::TableNextRow();
      ImGui::PushID(static_cast<int>(i));

      // Status
      ImGui::TableSetColumnIndex(k_ColStatus);
      ImGui::TextColored(color, "%s", status_label(entry.status));

      // ROM Name
      ImGui::TableSetColumnIndex(k_ColRomName);
      ImGui::TextColored(color, "%s", entry.name.c_str());

      // Size
      ImGui::TableSetColumnIndex(k_ColSize);
      ImGui::TextUnformatted(format_size(entry.size).c_str());

      // CRC32
      ImGui::TableSetColumnIndex(k_ColCrc32);
      ImGui::TextUnformatted(entry.crc32.c_str());

      ImGui::PopID();
    }
    ImGui::EndTable();
  }
}

void GuiApp::render_log_panel() {
  // Only copy the sink's buffer when new entries have been added.
  bool had_new_entries = false;
  if (auto new_entries = log_sink_->get_entries_if_changed(log_generation_, log_generation_)) {
    log_entries_cache_ = std::move(*new_entries);
    had_new_entries = true;
  }

  ImGui::Text("Log (%zu entries)", log_entries_cache_.size());
  ImGui::SameLine();
  if (ImGui::SmallButton("Clear")) {
    log_sink_->clear();
    log_entries_cache_.clear();
    log_generation_ = 0;
  }

  // Reserve space at the bottom for the horizontal scrollbar.
  constexpr float k_ScrollbarReserve = 30.0F;
  if (ImGui::BeginChild("##log_scroll",
                        ImVec2(0, -k_ScrollbarReserve),
                        false,
                        ImGuiWindowFlags_HorizontalScrollbar)) {
    // Capture the at-bottom state *before* rendering new lines.
    const bool was_at_bottom = ImGui::GetScrollY() >= (ImGui::GetScrollMaxY() - 1.0F);

    for (const auto& entry : log_entries_cache_) {
      ImVec4 color = k_ColorLogDefault;
      if (entry.level == spdlog::level::warn) {
        color = k_ColorLogWarn;
      } else if (entry.level >= spdlog::level::err) {
        color = k_ColorLogError;
      } else if (entry.level <= spdlog::level::debug) {
        color = k_ColorLogDebug;
      }
      ImGui::TextColored(color, "%s", entry.text.c_str());
    }

    // Scroll to the bottom only when new entries arrived and the user hadn't scrolled up.
    if (had_new_entries && was_at_bottom) {
      ImGui::SetScrollHereY(1.0F);
    }
  }
  ImGui::EndChild();
}

void GuiApp::render_status_bar() {
  if (is_busy()) {
    // Animated indeterminate progress bar
    float time = static_cast<float>(ImGui::GetTime());
    float progress = 0.5F + 0.5F * std::sin(time * 3.0F);
    ImGui::ProgressBar(progress, ImVec2(-1, 0), status_message_.c_str());
  } else {
    ImGui::TextDisabled("%s", status_message_.c_str());
  }
}

void GuiApp::render_toast() {
  if (toast_timer_ <= 0.0F) {
    return;
  }

  toast_timer_ -= ImGui::GetIO().DeltaTime;
  float alpha = std::min(toast_timer_, 1.0F);

  int fb_width = 0;
  int fb_height = 0;
  glfwGetFramebufferSize(window_, &fb_width, &fb_height);

  ImVec2 pos(static_cast<float>(fb_width) - k_ToastWidth - k_ToastMarginRight,
             static_cast<float>(fb_height) - k_ToastMarginBottom);
  ImGui::SetNextWindowPos(pos);
  ImGui::SetNextWindowSize(ImVec2(k_ToastWidth, k_ToastHeight));
  ImGui::SetNextWindowBgAlpha(0.85F * alpha);

  ImGui::Begin("##toast",
               nullptr,
               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoFocusOnAppearing |
                   ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs);
  ImGui::TextColored(ImVec4(0.3F, 1.0F, 0.5F, alpha), "%s", toast_message_.c_str());
  ImGui::End();
}

// ═════════════════════════════════════════════════════════════════
// Checklist sorting
// ═════════════════════════════════════════════════════════════════

void GuiApp::apply_checklist_sort() {
  if (checklist_sort_col_ < 0 || rom_checklist_.empty()) {
    return;
  }
  const int col = checklist_sort_col_;
  const bool asc = checklist_sort_ascending_;

  std::stable_sort(rom_checklist_.begin(), rom_checklist_.end(),
                   [col, asc](const RomChecklistEntry& a, const RomChecklistEntry& b) {
                     switch (col) {
                       case k_ColStatus: {
                         int sa = status_sort_order(a.status);
                         int sb = status_sort_order(b.status);
                         return asc ? sa < sb : sb < sa;
                       }
                       case k_ColRomName:
                         return asc ? a.name < b.name : b.name < a.name;
                       case k_ColSize:
                         return asc ? a.size < b.size : b.size < a.size;
                       case k_ColCrc32:
                         return asc ? a.crc32 < b.crc32 : b.crc32 < a.crc32;
                       default:
                         return false;
                     }
                   });
}

// ═════════════════════════════════════════════════════════════════
// Actions (launch background tasks)
// ═════════════════════════════════════════════════════════════════

void GuiApp::action_import_dat() {
  if (is_busy()) {
    return;
  }

  auto path = open_file_dialog();
  if (path.empty()) {
    return; // User cancelled
  }

  status_message_ = "Importing DAT... Please wait.";
  pending_task_ = PendingTask{
      .result = std::async(std::launch::async,
                           [this, p = std::filesystem::path(path)]() -> std::string {
                             auto result = svc_.import_dat(p);
                             if (!result) {
                               return "Import failed: " + result.error().message;
                             }
                             return "Imported DAT: " + result->name + " v" + result->version;
                           }),
      .refresh_dat_versions = true,
      .refresh_checklist = false,
  };
}

void GuiApp::action_add_rom_folder() {
  if (is_busy()) {
    return;
  }

  auto path = open_folder_dialog();
  if (path.empty()) {
    return; // User cancelled
  }

  auto dir = std::filesystem::path(path);

  // Track the folder for future rescans (avoid duplicates)
  bool already_tracked = false;
  for (const auto& d : scanned_dirs_) {
    if (std::filesystem::equivalent(d, dir)) {
      already_tracked = true;
      break;
    }
  }
  if (!already_tracked) {
    scanned_dirs_.push_back(dir);
  }

  // Auto-scan immediately
  status_message_ = "Scanning folder... Please wait.";
  pending_task_ = PendingTask{
      .result = std::async(std::launch::async,
                           [this, d = std::move(dir)]() -> std::string {
                             auto result = svc_.scan_directory(d);
                             if (!result) {
                               return "Scan failed: " + result.error().message;
                             }
                             return "Scan complete: " + std::to_string(result->files_scanned) +
                                    " files, " + std::to_string(result->files_hashed) + " hashed.";
                           }),
      .refresh_dat_versions = false,
      .refresh_checklist = false,
  };
}

void GuiApp::action_rescan_folders() {
  if (is_busy() || scanned_dirs_.empty()) {
    return;
  }

  status_message_ = "Rescanning all folders... Please wait.";
  // Copy the dirs vector for the async lambda
  auto dirs = scanned_dirs_;

  pending_task_ = PendingTask{
      .result = std::async(std::launch::async,
                           [this, dirs = std::move(dirs)]() -> std::string {
                             std::int64_t total_scanned = 0;
                             std::int64_t total_hashed = 0;
                             for (const auto& d : dirs) {
                               auto result = svc_.scan_directory(d);
                               if (result) {
                                 total_scanned += result->files_scanned;
                                 total_hashed += result->files_hashed;
                               } else {
                                 ROMULUS_WARN("Rescan failed for '{}': {}",
                                              d.string(), result.error().message);
                               }
                             }
                             return "Rescan complete: " + std::to_string(total_scanned) +
                                    " files, " + std::to_string(total_hashed) + " hashed.";
                           }),
      .refresh_dat_versions = false,
      .refresh_checklist = false,
  };
}

void GuiApp::action_check_dat() {
  if (is_busy() || selected_dat_index_ < 0) {
    return;
  }

  const auto& dv = dat_versions_[static_cast<std::size_t>(selected_dat_index_)];
  auto dat_id = dv.id;
  auto system_name = dv.name;

  status_message_ = "Verifying and checking DAT... Please wait.";
  pending_task_ = PendingTask{
      .result = std::async(std::launch::async,
                           [this, dat_id, system_name]() -> std::string {
                             // Run verify first to update statuses
                             auto verify_result = svc_.verify(system_name);
                             if (!verify_result) {
                               ROMULUS_WARN("Verification step failed: {}",
                                            verify_result.error().message);
                             }

                             // Get ROMs with status
                             auto roms = svc_.get_roms_with_status(dat_id);
                             if (!roms) {
                               return "Check failed: " + roms.error().message;
                             }

                             return "OK:" + std::to_string(roms->size());
                           }),
      .refresh_dat_versions = false,
      .refresh_checklist = true,
  };
}

void GuiApp::action_verify() {
  if (is_busy()) {
    return;
  }

  status_message_ = "Verifying... Please wait.";
  pending_task_ = PendingTask{
      .result = std::async(std::launch::async,
                           [this]() -> std::string {
                             auto result = svc_.verify();
                             if (!result) {
                               return "Verification failed: " + result.error().message;
                             }
                             return "Verification complete.";
                           }),
      .refresh_dat_versions = false,
      .refresh_checklist = false,
  };
}

void GuiApp::action_purge_database() {
  if (is_busy()) {
    return;
  }

  status_message_ = "Purging database... Please wait.";
  pending_task_ = PendingTask{
      .result = std::async(std::launch::async,
                           [this]() -> std::string {
                             auto result = svc_.purge_database();
                             if (!result) {
                               return "Purge failed: " + result.error().message;
                             }
                             return "Database purged.";
                           }),
      .refresh_dat_versions = true,
      .refresh_checklist = false,
  };
}

// ═════════════════════════════════════════════════════════════════
// Background Task Management
// ═════════════════════════════════════════════════════════════════

void GuiApp::check_pending_task() {
  if (!pending_task_ || !pending_task_->result.valid()) {
    return;
  }

  if (pending_task_->result.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
    return;
  }

  // Task finished — collect the result on the main thread
  std::string task_result;
  try {
    task_result = pending_task_->result.get();
  } catch (const std::exception& e) {
    task_result = std::string("Task error: ") + e.what();
  }

  bool should_refresh_dat = pending_task_->refresh_dat_versions;
  bool should_refresh_checklist = pending_task_->refresh_checklist;
  pending_task_.reset();

  // Handle check_dat result: populates checklist from service on main thread
  if (should_refresh_checklist && task_result.starts_with("OK:")) {
    // Re-fetch on main thread to populate the checklist
    if (selected_dat_index_ >= 0) {
      const auto& dv = dat_versions_[static_cast<std::size_t>(selected_dat_index_)];
      auto roms = svc_.get_roms_with_status(dv.id);
      if (roms) {
        rom_checklist_.clear();
        rom_checklist_.reserve(roms->size());
        std::int64_t available = 0;
        for (const auto& [rom, st] : *roms) {
          rom_checklist_.push_back({
              .name = rom.name,
              .size = rom.size,
              .crc32 = rom.crc32,
              .status = st,
          });
          if (st == core::RomStatusType::Verified) {
            ++available;
          }
        }
        apply_checklist_sort();
        status_message_ = "Check complete: " + std::to_string(available) + " / " +
                           std::to_string(rom_checklist_.size()) + " ROMs available.";
      } else {
        status_message_ = "Failed to load checklist: " + roms.error().message;
      }
    }
  } else {
    status_message_ = task_result;
  }

  if (should_refresh_dat) {
    refresh_dat_versions();
  }

  show_toast(status_message_);
}

auto GuiApp::is_busy() const -> bool {
  return pending_task_.has_value() && pending_task_->result.valid();
}

// ═════════════════════════════════════════════════════════════════
// Data Refresh
// ═════════════════════════════════════════════════════════════════

void GuiApp::refresh_dat_versions() {
  auto result = svc_.list_dat_versions();
  if (result) {
    dat_versions_ = std::move(*result);

    // Reset selection if the current index is out of bounds
    if (selected_dat_index_ >= static_cast<int>(dat_versions_.size())) {
      selected_dat_index_ = dat_versions_.empty() ? -1 : 0;
    }

    // Auto-select first if nothing selected and we have data
    if (selected_dat_index_ < 0 && !dat_versions_.empty()) {
      selected_dat_index_ = 0;
    }
  } else {
    dat_versions_.clear();
    selected_dat_index_ = -1;
    ROMULUS_WARN("Failed to refresh DAT versions: {}", result.error().message);
  }
}

// ═════════════════════════════════════════════════════════════════
// Toast Notification
// ═════════════════════════════════════════════════════════════════

void GuiApp::show_toast(const std::string& message) {
  toast_message_ = message;
  toast_timer_ = k_ToastDuration;
}

} // namespace romulus::gui
