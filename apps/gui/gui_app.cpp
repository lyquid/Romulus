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
constexpr std::size_t k_PathBufferSize = 512;
constexpr float k_ToastDuration = 2.5F;
constexpr float k_ToastWidth = 310.0F;
constexpr float k_ToastHeight = 36.0F;
constexpr float k_ToastMarginRight = 10.0F;
constexpr float k_ToastMarginBottom = 50.0F;

// Files table column indices
constexpr int k_ColFilename = 0;
constexpr int k_ColSize = 1;
constexpr int k_ColCrc32 = 2;
constexpr int k_ColMd5 = 3;
constexpr int k_ColSha1 = 4;
constexpr int k_ColSha256 = 5;

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

} // namespace

// ═════════════════════════════════════════════════════════════════
// Lifecycle
// ═════════════════════════════════════════════════════════════════

GuiApp::GuiApp(service::RomulusService& svc) : svc_(svc) {
  dat_path_buf_.resize(k_PathBufferSize, '\0');
  scan_dir_buf_.resize(k_PathBufferSize, '\0');

  init_glfw();
  init_imgui();

  // Register the in-memory log sink so the "Log" tab captures all log messages.
  log_sink_ = std::make_shared<GuiLogSink>();
  core::get_logger()->sinks().push_back(log_sink_);

  status_message_ = "Ready.";
  refresh_files();
  refresh_systems();
  refresh_summary();
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
    render_summary_panel();

    ImGui::Separator();
    if (ImGui::BeginTabBar("##main_tabs")) {
      if (ImGui::BeginTabItem("Local ROMs")) {
        if (ImGui::IsItemActivated() && !is_busy()) {
          refresh_files();
        }
        render_files_panel();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Systems")) {
        if (ImGui::IsItemActivated() && !is_busy()) {
          refresh_systems();
        }
        render_systems_panel();
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

  // ── DAT Import ──
  ImGui::Text("DAT Import");
  ImGui::PushItemWidth(-230);
  ImGui::InputText("##dat_path", dat_path_buf_.data(), dat_path_buf_.size());
  ImGui::PopItemWidth();
  ImGui::SameLine();
  if (ImGui::Button("Browse##dat")) {
    auto path = open_file_dialog();
    if (!path.empty()) {
      dat_path_buf_.assign(k_PathBufferSize, '\0');
      std::copy_n(path.begin(), std::min(path.size(), k_PathBufferSize - 1), dat_path_buf_.begin());
    }
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(busy);
  if (ImGui::Button("Import DAT")) {
    action_import_dat();
  }
  ImGui::EndDisabled();

  // ── ROM Directory ──
  ImGui::Spacing();
  ImGui::Text("ROM Directory");
  ImGui::PushItemWidth(-230);
  ImGui::InputText("##scan_dir", scan_dir_buf_.data(), scan_dir_buf_.size());
  ImGui::PopItemWidth();
  ImGui::SameLine();
  if (ImGui::Button("Browse##dir")) {
    auto path = open_folder_dialog();
    if (!path.empty()) {
      scan_dir_buf_.assign(k_PathBufferSize, '\0');
      std::copy_n(path.begin(), std::min(path.size(), k_PathBufferSize - 1), scan_dir_buf_.begin());
    }
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(busy);
  if (ImGui::Button("Scan Folder")) {
    action_scan_folder();
  }
  ImGui::EndDisabled();

  // ── Actions ──
  ImGui::Spacing();
  ImGui::BeginDisabled(busy);
  if (ImGui::Button("Verify All")) {
    action_verify();
  }
  ImGui::SameLine();
  if (ImGui::Button("Refresh")) {
    refresh_files();
    refresh_systems();
    refresh_summary();
    status_message_ = "Data refreshed.";
  }
  ImGui::EndDisabled();
}

void GuiApp::render_summary_panel() {
  ImGui::Text("Collection Summary");
  if (has_summary_) {
    ImGui::Columns(2, "summary_cols", false);
    ImGui::SetColumnWidth(0, 150);

    ImGui::Text("System:");
    ImGui::NextColumn();
    ImGui::Text("%s", summary_.system_name.c_str());
    ImGui::NextColumn();

    ImGui::Text("Total ROMs:");
    ImGui::NextColumn();
    ImGui::Text("%lld", static_cast<long long>(summary_.total_roms));
    ImGui::NextColumn();

    ImGui::Text("Verified:");
    ImGui::NextColumn();
    ImGui::TextColored(ImVec4(0.0F, 1.0F, 0.0F, 1.0F),
                       "%lld (%.0f%%)",
                       static_cast<long long>(summary_.verified),
                       summary_.verified_percent());
    ImGui::NextColumn();

    ImGui::Text("Missing:");
    ImGui::NextColumn();
    ImGui::TextColored(
        ImVec4(1.0F, 0.5F, 0.0F, 1.0F), "%lld", static_cast<long long>(summary_.missing));
    ImGui::NextColumn();

    ImGui::Text("Unverified:");
    ImGui::NextColumn();
    ImGui::TextColored(
        ImVec4(1.0F, 1.0F, 0.0F, 1.0F), "%lld", static_cast<long long>(summary_.unverified));
    ImGui::NextColumn();

    ImGui::Text("Mismatch:");
    ImGui::NextColumn();
    ImGui::TextColored(
        ImVec4(1.0F, 0.0F, 0.0F, 1.0F), "%lld", static_cast<long long>(summary_.mismatch));
    ImGui::NextColumn();

    ImGui::Columns(1);
  } else {
    ImGui::TextDisabled("No data. Import a DAT and scan ROMs to see a summary.");
  }
}

void GuiApp::render_files_panel() {
  ImGui::Text("Local ROMs (%zu)", files_.size());

  constexpr int k_ColumnCount = 6;
  if (ImGui::BeginTable("files_table",
                        k_ColumnCount,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                            ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp |
                            ImGuiTableFlags_Sortable,
                        ImVec2(0, -30))) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Filename", ImGuiTableColumnFlags_DefaultSort, 3.0F);
    ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_None, 1.0F);
    ImGui::TableSetupColumn("CRC32", ImGuiTableColumnFlags_None, 1.5F);
    ImGui::TableSetupColumn("MD5", ImGuiTableColumnFlags_None, 3.0F);
    ImGui::TableSetupColumn("SHA1", ImGuiTableColumnFlags_None, 3.5F);
    ImGui::TableSetupColumn("SHA256", ImGuiTableColumnFlags_None, 4.0F);
    ImGui::TableHeadersRow();

    // Apply any pending sort requested by the user clicking a column header.
    if (auto* sort_specs = ImGui::TableGetSortSpecs()) {
      if (sort_specs->SpecsDirty) {
        if (sort_specs->SpecsCount > 0) {
          sort_col_ = sort_specs->Specs[0].ColumnIndex;
          sort_ascending_ = (sort_specs->Specs[0].SortDirection == ImGuiSortDirection_Ascending);
        } else {
          sort_col_ = -1;
        }
        apply_sort();
        sort_specs->SpecsDirty = false;
      }
    }

    for (std::size_t i = 0; i < files_.size(); ++i) {
      const auto& file = files_[i];
      ImGui::TableNextRow();
      ImGui::PushID(static_cast<int>(i));

      // Filename
      ImGui::TableSetColumnIndex(k_ColFilename);
      ImGui::TextUnformatted(file.filename.c_str());

      // Size (human-readable)
      ImGui::TableSetColumnIndex(k_ColSize);
      ImGui::TextUnformatted(format_size(file.size).c_str());

      // Hash columns — right-click to copy
      render_hash_cell(k_ColCrc32, file.crc32, "CRC32");
      render_hash_cell(k_ColMd5, file.md5, "MD5");
      render_hash_cell(k_ColSha1, file.sha1, "SHA1");
      render_hash_cell(k_ColSha256, file.sha256, "SHA256");

      ImGui::PopID();
    }
    ImGui::EndTable();
  }
}

void GuiApp::render_systems_panel() {
  ImGui::Text("Systems (%zu)", systems_.size());

  constexpr int k_SystemColumnCount = 3;
  if (ImGui::BeginTable("systems_table",
                        k_SystemColumnCount,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                            ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp,
                        ImVec2(0, -30))) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_None, 3.0F);
    ImGui::TableSetupColumn("Short Name", ImGuiTableColumnFlags_None, 1.0F);
    ImGui::TableSetupColumn("Extensions", ImGuiTableColumnFlags_None, 2.0F);
    ImGui::TableHeadersRow();

    for (std::size_t i = 0; i < systems_.size(); ++i) {
      const auto& sys = systems_[i];
      ImGui::TableNextRow();
      ImGui::PushID(static_cast<int>(i));

      ImGui::TableSetColumnIndex(0);
      ImGui::TextUnformatted(sys.name.c_str());

      ImGui::TableSetColumnIndex(1);
      ImGui::TextUnformatted(sys.short_name.c_str());

      ImGui::TableSetColumnIndex(2);
      ImGui::TextUnformatted(sys.extensions.c_str());

      ImGui::PopID();
    }
    ImGui::EndTable();
  }
}

void GuiApp::render_log_panel() {
  auto entries = log_sink_->get_entries();
  ImGui::Text("Log (%zu entries)", entries.size());
  ImGui::SameLine();
  if (ImGui::SmallButton("Clear")) {
    log_sink_->clear();
  }

  constexpr float k_ScrollbarReserve = 30.0F;
  if (ImGui::BeginChild("##log_scroll",
                        ImVec2(0, -k_ScrollbarReserve),
                        false,
                        ImGuiWindowFlags_HorizontalScrollbar)) {
    for (const auto& entry : entries) {
      // Colour-code by log level detected in the formatted "[level]" token.
      ImVec4 color{1.0F, 1.0F, 1.0F, 1.0F}; // default: white
      if (entry.find("[warning]") != std::string::npos ||
          entry.find("[warn]") != std::string::npos) {
        color = ImVec4{1.0F, 0.75F, 0.1F, 1.0F}; // amber
      } else if (entry.find("[error]") != std::string::npos ||
                 entry.find("[critical]") != std::string::npos) {
        color = ImVec4{1.0F, 0.3F, 0.3F, 1.0F}; // red
      } else if (entry.find("[debug]") != std::string::npos ||
                 entry.find("[trace]") != std::string::npos) {
        color = ImVec4{0.6F, 0.6F, 0.6F, 1.0F}; // grey
      }
      ImGui::TextColored(color, "%s", entry.c_str());
    }

    // Auto-scroll to the bottom whenever new entries arrive.
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
      ImGui::SetScrollHereY(1.0F);
    }
  }
  ImGui::EndChild();
}

void GuiApp::render_hash_cell(int column, const std::string& hash, const char* label) {
  ImGui::TableSetColumnIndex(column);
  ImGui::TextUnformatted(hash.c_str());
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Right-click to copy %s", label);
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
      glfwSetClipboardString(window_, hash.c_str());
      show_toast(std::string(label) + " copied to clipboard");
    }
  }
}

void GuiApp::apply_sort() {
  if (sort_col_ < 0 || files_.empty()) {
    return;
  }
  const int col = sort_col_;
  const bool asc = sort_ascending_;
  std::stable_sort(
      files_.begin(), files_.end(), [col, asc](const core::FileInfo& a, const core::FileInfo& b) {
        switch (col) {
          case k_ColFilename:
            return asc ? a.filename < b.filename : b.filename < a.filename;
          case k_ColSize:
            return asc ? a.size < b.size : b.size < a.size;
          case k_ColCrc32:
            return asc ? a.crc32 < b.crc32 : b.crc32 < a.crc32;
          case k_ColMd5:
            return asc ? a.md5 < b.md5 : b.md5 < a.md5;
          case k_ColSha1:
            return asc ? a.sha1 < b.sha1 : b.sha1 < a.sha1;
          case k_ColSha256:
            return asc ? a.sha256 < b.sha256 : b.sha256 < a.sha256;
          default:
            // Unknown column: treat elements as equal (no reordering).
            return false;
        }
      });
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
// Actions (launch background tasks)
// ═════════════════════════════════════════════════════════════════

void GuiApp::action_import_dat() {
  if (is_busy()) {
    return;
  }

  std::string path(dat_path_buf_.c_str());
  if (path.empty()) {
    status_message_ = "Error: DAT path is empty.";
    return;
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
      .refresh_files = false,
      .refresh_summary = true,
      .refresh_systems = true,
  };
}

void GuiApp::action_scan_folder() {
  if (is_busy()) {
    return;
  }

  std::string dir(scan_dir_buf_.c_str());
  if (dir.empty()) {
    status_message_ = "Error: Scan directory is empty.";
    return;
  }

  status_message_ = "Scanning folder... Please wait.";
  pending_task_ = PendingTask{
      .result = std::async(std::launch::async,
                           [this, d = std::filesystem::path(dir)]() -> std::string {
                             auto result = svc_.scan_directory(d);
                             if (!result) {
                               return "Scan failed: " + result.error().message;
                             }
                             return "Scan complete: " + std::to_string(result->files_scanned) +
                                    " files, " + std::to_string(result->files_hashed) + " hashed.";
                           }),
      .refresh_files = true,
      .refresh_summary = true,
      .refresh_systems = false,
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
      .refresh_files = false,
      .refresh_summary = true,
      .refresh_systems = false,
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
      .refresh_files = true,
      .refresh_summary = true,
      .refresh_systems = true,
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
  try {
    status_message_ = pending_task_->result.get();
  } catch (const std::exception& e) {
    status_message_ = std::string("Task error: ") + e.what();
  }

  bool should_refresh_files = pending_task_->refresh_files;
  bool should_refresh_summary = pending_task_->refresh_summary;
  bool should_refresh_systems = pending_task_->refresh_systems;
  pending_task_.reset();

  if (should_refresh_files) {
    refresh_files();
  }
  if (should_refresh_summary) {
    refresh_summary();
  }
  if (should_refresh_systems) {
    refresh_systems();
  }
}

auto GuiApp::is_busy() const -> bool {
  return pending_task_.has_value() && pending_task_->result.valid();
}

// ═════════════════════════════════════════════════════════════════
// Data Refresh
// ═════════════════════════════════════════════════════════════════

void GuiApp::refresh_files() {
  auto result = svc_.get_all_files();
  if (result) {
    files_ = std::move(*result);
    apply_sort();
  } else {
    files_.clear();
    ROMULUS_WARN("Failed to refresh files: {}", result.error().message);
  }
}

void GuiApp::refresh_systems() {
  auto result = svc_.list_systems();
  if (result) {
    systems_ = std::move(*result);
  } else {
    systems_.clear();
    ROMULUS_WARN("Failed to refresh systems: {}", result.error().message);
  }
}

void GuiApp::refresh_summary() {
  auto result = svc_.get_summary();
  if (result) {
    summary_ = std::move(*result);
    has_summary_ = summary_.total_roms > 0;
  } else {
    summary_ = {};
    has_summary_ = false;
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
