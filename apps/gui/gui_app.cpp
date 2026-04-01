#include "gui_app.hpp"

#include "romulus/core/logging.hpp"

// ImGui and backends — included as system headers to avoid third-party warnings.
// The SYSTEM include paths are set in CMakeLists.txt.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#pragma GCC diagnostic pop

// GLFW must come after imgui backend headers
// NOLINTNEXTLINE(misc-include-cleaner)
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdio>
#include <stdexcept>

namespace romulus::gui {

namespace {

constexpr int k_WindowWidth = 1024;
constexpr int k_WindowHeight = 720;
constexpr auto* k_WindowTitle = "ROMULUS — ROM Collection Verifier";
constexpr auto* k_GlslVersion = "#version 130";
constexpr std::size_t k_PathBufferSize = 512;

void glfw_error_callback(int error, const char* description) {
  std::fprintf(stderr, "GLFW error %d: %s\n", error, description);
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

  status_message_ = "Ready.";
  refresh_files();
  refresh_summary();
}

GuiApp::~GuiApp() {
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

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Full-window docking area
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    int fb_width = 0;
    int fb_height = 0;
    glfwGetFramebufferSize(window_, &fb_width, &fb_height);
    ImGui::SetNextWindowSize(
        ImVec2(static_cast<float>(fb_width), static_cast<float>(fb_height)));
    ImGui::Begin("ROMULUS",
                 nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_MenuBar);

    render_main_menu_bar();
    render_actions_panel();

    ImGui::Separator();
    render_summary_panel();

    ImGui::Separator();
    render_files_panel();

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
      if (ImGui::MenuItem("Purge Database")) {
        show_purge_confirm_ = true;
      }
      ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
  }
}

void GuiApp::render_actions_panel() {
  ImGui::Text("DAT Import");
  ImGui::PushItemWidth(-120);
  ImGui::InputText("##dat_path", dat_path_buf_.data(), dat_path_buf_.size());
  ImGui::PopItemWidth();
  ImGui::SameLine();
  if (ImGui::Button("Import DAT")) {
    action_import_dat();
  }

  ImGui::Spacing();
  ImGui::Text("ROM Directory");
  ImGui::PushItemWidth(-120);
  ImGui::InputText("##scan_dir", scan_dir_buf_.data(), scan_dir_buf_.size());
  ImGui::PopItemWidth();
  ImGui::SameLine();
  if (ImGui::Button("Scan Folder")) {
    action_scan_folder();
  }

  ImGui::Spacing();
  if (ImGui::Button("Verify All")) {
    action_verify();
  }
  ImGui::SameLine();
  if (ImGui::Button("Refresh")) {
    refresh_files();
    refresh_summary();
    status_message_ = "Data refreshed.";
  }
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
    ImGui::TextColored(ImVec4(1.0F, 0.5F, 0.0F, 1.0F),
                       "%lld",
                       static_cast<long long>(summary_.missing));
    ImGui::NextColumn();

    ImGui::Text("Unverified:");
    ImGui::NextColumn();
    ImGui::TextColored(ImVec4(1.0F, 1.0F, 0.0F, 1.0F),
                       "%lld",
                       static_cast<long long>(summary_.unverified));
    ImGui::NextColumn();

    ImGui::Text("Mismatch:");
    ImGui::NextColumn();
    ImGui::TextColored(ImVec4(1.0F, 0.0F, 0.0F, 1.0F),
                       "%lld",
                       static_cast<long long>(summary_.mismatch));
    ImGui::NextColumn();

    ImGui::Columns(1);
  } else {
    ImGui::TextDisabled("No data. Import a DAT and scan ROMs to see a summary.");
  }
}

void GuiApp::render_files_panel() {
  ImGui::Text("Scanned Files (%zu)", files_.size());

  if (ImGui::BeginTable(
          "files_table",
          4,
          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
              ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp,
          ImVec2(0, -30))) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Filename", ImGuiTableColumnFlags_None, 3.0F);
    ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_None, 5.0F);
    ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_None, 1.0F);
    ImGui::TableSetupColumn("SHA1", ImGuiTableColumnFlags_None, 3.0F);
    ImGui::TableHeadersRow();

    for (const auto& file : files_) {
      ImGui::TableNextRow();

      ImGui::TableSetColumnIndex(0);
      ImGui::TextUnformatted(file.filename.c_str());

      ImGui::TableSetColumnIndex(1);
      ImGui::TextUnformatted(file.path.c_str());

      ImGui::TableSetColumnIndex(2);
      ImGui::Text("%lld", static_cast<long long>(file.size));

      ImGui::TableSetColumnIndex(3);
      ImGui::TextUnformatted(file.sha1.c_str());
    }
    ImGui::EndTable();
  }
}

void GuiApp::render_status_bar() {
  ImGui::TextDisabled("%s", status_message_.c_str());
}

// ═════════════════════════════════════════════════════════════════
// Actions
// ═════════════════════════════════════════════════════════════════

void GuiApp::action_import_dat() {
  std::string path(dat_path_buf_.c_str());
  if (path.empty()) {
    status_message_ = "Error: DAT path is empty.";
    return;
  }

  auto result = svc_.import_dat(std::filesystem::path(path));
  if (!result) {
    status_message_ = "Import failed: " + result.error().message;
    return;
  }
  status_message_ = "Imported DAT: " + result->name + " v" + result->version;
  refresh_summary();
}

void GuiApp::action_scan_folder() {
  std::string dir(scan_dir_buf_.c_str());
  if (dir.empty()) {
    status_message_ = "Error: Scan directory is empty.";
    return;
  }

  auto result = svc_.scan_directory(std::filesystem::path(dir));
  if (!result) {
    status_message_ = "Scan failed: " + result.error().message;
    return;
  }
  status_message_ = "Scan complete: " + std::to_string(result->files_scanned) + " files, " +
                     std::to_string(result->files_hashed) + " hashed.";
  refresh_files();
  refresh_summary();
}

void GuiApp::action_verify() {
  auto result = svc_.verify();
  if (!result) {
    status_message_ = "Verification failed: " + result.error().message;
    return;
  }
  status_message_ = "Verification complete.";
  refresh_summary();
}

void GuiApp::action_purge_database() {
  // Drop all data by executing DELETE on each table
  static constexpr std::array k_Tables = {
      "rom_status", "file_matches", "files", "roms", "games", "dat_versions", "systems",
  };
  for (const auto* table : k_Tables) {
    auto result = svc_.execute_raw("DELETE FROM " + std::string(table));
    if (!result) {
      status_message_ = "Purge failed on table '" + std::string(table) + "': " +
                         result.error().message;
      return;
    }
  }
  status_message_ = "Database purged.";
  refresh_files();
  refresh_summary();
}

// ═════════════════════════════════════════════════════════════════
// Data Refresh
// ═════════════════════════════════════════════════════════════════

void GuiApp::refresh_files() {
  auto result = svc_.get_all_files();
  if (result) {
    files_ = std::move(*result);
  } else {
    files_.clear();
    ROMULUS_WARN("Failed to refresh files: {}", result.error().message);
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

} // namespace romulus::gui
