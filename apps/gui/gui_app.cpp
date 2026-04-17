#include "gui_app.hpp"

#include "file_dialog.hpp"
#include "gui_app_shared.hpp"
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
#include <cstdlib>
#include <future>
#include <GLFW/glfw3.h>
#include <stdexcept>
#include <unordered_map>

namespace romulus::gui {

// ═════════════════════════════════════════════════════════════════
// Lifecycle
// ═════════════════════════════════════════════════════════════════

GuiApp::GuiApp(service::RomulusService& svc, std::shared_ptr<GuiLogSink> log_sink)
    : svc_(svc), log_sink_(std::move(log_sink)) {
  init_glfw();
  init_imgui();

  status_message_ = "Ready.";
  refresh_dat_versions();
  refresh_folders();
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

  apply_custom_theme();

  ImGui_ImplGlfw_InitForOpenGL(window_, true);
  ImGui_ImplOpenGL3_Init(k_GlslVersion);
}

// ─────────────────────────────────────────────────────────────────
// Custom theme
// ─────────────────────────────────────────────────────────────────

void GuiApp::apply_custom_theme() {
  ImGuiStyle& style = ImGui::GetStyle();
  ImVec4* c = style.Colors;

  // ── Background & panels ──────────────────────────────────────
  c[ImGuiCol_WindowBg] = ImVec4(0.10F, 0.10F, 0.13F, 1.00F);
  c[ImGuiCol_ChildBg] = ImVec4(0.08F, 0.08F, 0.10F, 1.00F);
  c[ImGuiCol_PopupBg] = ImVec4(0.10F, 0.10F, 0.13F, 0.96F);

  // ── Borders ──────────────────────────────────────────────────
  c[ImGuiCol_Border] = ImVec4(0.28F, 0.30F, 0.42F, 0.60F);
  c[ImGuiCol_BorderShadow] = ImVec4(0.00F, 0.00F, 0.00F, 0.00F);

  // ── Text ─────────────────────────────────────────────────────
  c[ImGuiCol_Text] = ImVec4(0.92F, 0.93F, 0.95F, 1.00F);
  c[ImGuiCol_TextDisabled] = ImVec4(0.45F, 0.48F, 0.56F, 1.00F);

  // ── Frame inputs ─────────────────────────────────────────────
  c[ImGuiCol_FrameBg] = ImVec4(0.15F, 0.16F, 0.21F, 1.00F);
  c[ImGuiCol_FrameBgHovered] = ImVec4(0.20F, 0.22F, 0.29F, 1.00F);
  c[ImGuiCol_FrameBgActive] = ImVec4(0.24F, 0.27F, 0.38F, 1.00F);

  // ── Title bars ───────────────────────────────────────────────
  c[ImGuiCol_TitleBg] = ImVec4(0.08F, 0.08F, 0.11F, 1.00F);
  c[ImGuiCol_TitleBgActive] = ImVec4(0.12F, 0.15F, 0.22F, 1.00F);
  c[ImGuiCol_TitleBgCollapsed] = ImVec4(0.08F, 0.08F, 0.11F, 0.75F);

  // ── Menu bar ─────────────────────────────────────────────────
  c[ImGuiCol_MenuBarBg] = ImVec4(0.08F, 0.09F, 0.12F, 1.00F);

  // ── Scrollbar ────────────────────────────────────────────────
  c[ImGuiCol_ScrollbarBg] = ImVec4(0.08F, 0.08F, 0.10F, 1.00F);
  c[ImGuiCol_ScrollbarGrab] = ImVec4(0.28F, 0.32F, 0.44F, 1.00F);
  c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.36F, 0.40F, 0.55F, 1.00F);
  c[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.42F, 0.48F, 0.65F, 1.00F);

  // ── Check marks & sliders ────────────────────────────────────
  c[ImGuiCol_CheckMark] = ImVec4(0.40F, 0.78F, 1.00F, 1.00F);
  c[ImGuiCol_SliderGrab] = ImVec4(0.40F, 0.62F, 0.92F, 1.00F);
  c[ImGuiCol_SliderGrabActive] = ImVec4(0.50F, 0.72F, 1.00F, 1.00F);

  // ── Buttons ──────────────────────────────────────────────────
  c[ImGuiCol_Button] = ImVec4(0.18F, 0.34F, 0.56F, 1.00F);
  c[ImGuiCol_ButtonHovered] = ImVec4(0.24F, 0.44F, 0.72F, 1.00F);
  c[ImGuiCol_ButtonActive] = ImVec4(0.30F, 0.52F, 0.84F, 1.00F);

  // ── Header (table headers, combo items, tree nodes) ──────────
  c[ImGuiCol_Header] = ImVec4(0.20F, 0.32F, 0.52F, 0.80F);
  c[ImGuiCol_HeaderHovered] = ImVec4(0.26F, 0.42F, 0.66F, 0.90F);
  c[ImGuiCol_HeaderActive] = ImVec4(0.32F, 0.52F, 0.80F, 1.00F);

  // ── Separators ───────────────────────────────────────────────
  c[ImGuiCol_Separator] = ImVec4(0.26F, 0.29F, 0.40F, 0.50F);
  c[ImGuiCol_SeparatorHovered] = ImVec4(0.38F, 0.50F, 0.72F, 0.80F);
  c[ImGuiCol_SeparatorActive] = ImVec4(0.48F, 0.62F, 0.86F, 1.00F);

  // ── Resize grips ─────────────────────────────────────────────
  c[ImGuiCol_ResizeGrip] = ImVec4(0.28F, 0.42F, 0.64F, 0.25F);
  c[ImGuiCol_ResizeGripHovered] = ImVec4(0.36F, 0.52F, 0.78F, 0.67F);
  c[ImGuiCol_ResizeGripActive] = ImVec4(0.44F, 0.62F, 0.92F, 0.95F);

  // ── Tabs ─────────────────────────────────────────────────────
  c[ImGuiCol_Tab] = ImVec4(0.13F, 0.16F, 0.22F, 1.00F);
  c[ImGuiCol_TabHovered] = ImVec4(0.26F, 0.42F, 0.66F, 0.90F);
  c[ImGuiCol_TabActive] = ImVec4(0.20F, 0.34F, 0.56F, 1.00F);
  c[ImGuiCol_TabUnfocused] = ImVec4(0.10F, 0.12F, 0.17F, 1.00F);
  c[ImGuiCol_TabUnfocusedActive] = ImVec4(0.16F, 0.24F, 0.38F, 1.00F);

  // ── Table ────────────────────────────────────────────────────
  c[ImGuiCol_TableHeaderBg] = ImVec4(0.14F, 0.18F, 0.28F, 1.00F);
  c[ImGuiCol_TableBorderStrong] = ImVec4(0.28F, 0.32F, 0.46F, 1.00F);
  c[ImGuiCol_TableBorderLight] = ImVec4(0.18F, 0.22F, 0.32F, 1.00F);
  c[ImGuiCol_TableRowBg] = ImVec4(0.00F, 0.00F, 0.00F, 0.00F);
  c[ImGuiCol_TableRowBgAlt] = ImVec4(1.00F, 1.00F, 1.00F, 0.03F);

  // ── Nav & selection ──────────────────────────────────────────
  c[ImGuiCol_NavHighlight] = ImVec4(0.40F, 0.70F, 1.00F, 1.00F);
  c[ImGuiCol_TextSelectedBg] = ImVec4(0.26F, 0.46F, 0.74F, 0.50F);
  c[ImGuiCol_ModalWindowDimBg] = ImVec4(0.00F, 0.00F, 0.00F, 0.60F);

  // ── Style metrics ────────────────────────────────────────────
  style.WindowRounding = 5.0F;
  style.ChildRounding = 4.0F;
  style.FrameRounding = 3.0F;
  style.GrabRounding = 3.0F;
  style.PopupRounding = 5.0F;
  style.TabRounding = 4.0F;
  style.ScrollbarRounding = 6.0F;
  style.WindowBorderSize = 1.0F;
  style.FrameBorderSize = 0.0F;
  style.TabBorderSize = 0.0F;
  style.WindowPadding = ImVec2(10.0F, 10.0F);
  style.FramePadding = ImVec2(8.0F, 4.0F);
  style.ItemSpacing = ImVec2(8.0F, 5.0F);
  style.ItemInnerSpacing = ImVec2(6.0F, 4.0F);
  style.ScrollbarSize = 12.0F;
  style.GrabMinSize = 10.0F;
  style.IndentSpacing = 18.0F;
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
  // Sleep until an event arrives (or the timeout expires) — avoids busy-looping
  // at thousands of FPS when idle.  glfwSwapInterval(1) caps rendering to the
  // display refresh rate; glfwWaitEventsTimeout provides the ~60 FPS floor used
  // when background tasks are running and no user input arrives.
  constexpr double k_TargetFrameTimeout = 1.0 / 60.0;

  while (glfwWindowShouldClose(window_) == 0) {
    glfwWaitEventsTimeout(k_TargetFrameTimeout);

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
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_MenuBar |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    render_main_menu_bar();

    ImGui::Spacing();
    if (ImGui::BeginTabBar("##main_tabs")) {
      if (ImGui::BeginTabItem("DATs")) {
        render_dats_tab();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Folders")) {
        render_folders_tab();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("DB")) {
        render_db_tab();
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

    // Delete DAT confirmation popup
    if (show_delete_dat_confirm_) {
      ImGui::OpenPopup("Confirm Delete DAT");
      show_delete_dat_confirm_ = false;
    }
    if (ImGui::BeginPopupModal("Confirm Delete DAT", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      if (selected_dat_index_ >= 0 &&
          selected_dat_index_ < static_cast<int>(dat_versions_.size())) {
        const auto& dv = dat_versions_[static_cast<std::size_t>(selected_dat_index_)];
        ImGui::Text("Delete DAT version:");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0F, 0.8F, 0.2F, 1.0F), "%s v%s", dv.name.c_str(),
                           dv.version.c_str());
        ImGui::Text("This will remove its games, ROMs, and match records.");
        ImGui::Text("Scanned files are preserved. This action cannot be undone!");
      }
      ImGui::Separator();
      if (ImGui::Button("Yes, Delete", ImVec2(120, 0))) {
        action_delete_dat();
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

// ─────────────────────────────────────────────────────────────────
// DB Tab
// ─────────────────────────────────────────────────────────────────

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
  if (toast_timer_ <= 0.0F) {
    return;
  }

  const float alpha = std::min(toast_timer_, 1.0F);
  const ImVec2 display_size = ImGui::GetIO().DisplaySize;

  // Compute toast dimensions from actual text size + padding so it always fits.
  const ImVec2 text_size = ImGui::CalcTextSize(toast_message_.c_str());
  const float toast_w = text_size.x + k_ToastPaddingH * 2.0F;
  const float toast_h = text_size.y + k_ToastPaddingV * 2.0F;

  // Position at the bottom-right of the viewport (in logical display coordinates).
  const ImVec2 toast_min(display_size.x - toast_w - k_ToastMarginRight,
                         display_size.y - toast_h - k_ToastMarginBottom);
  const ImVec2 toast_max(toast_min.x + toast_w, toast_min.y + toast_h);

  // GetForegroundDrawList() renders on top of ALL ImGui windows — no Z-order issues.
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  dl->AddRectFilled(toast_min,
                    toast_max,
                    IM_COL32(30, 40, 60, static_cast<int>(k_ToastBgAlpha * alpha)),
                    k_ToastRounding);
  dl->AddRect(toast_min,
              toast_max,
              IM_COL32(100, 160, 255, static_cast<int>(k_ToastBorderAlpha * alpha)),
              k_ToastRounding);
  // Center the text both horizontally and vertically within the toast box.
  const ImVec2 text_pos(toast_min.x + k_ToastPaddingH, toast_min.y + k_ToastPaddingV);
  dl->AddText(text_pos,
              IM_COL32(77, 255, 128, static_cast<int>(k_ToastTextAlpha * alpha)),
              toast_message_.c_str());
}

// ═════════════════════════════════════════════════════════════════
// Checklist sorting
// ═════════════════════════════════════════════════════════════════

// ═════════════════════════════════════════════════════════════════
// DB Explorer sort + filter
// ═════════════════════════════════════════════════════════════════

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

  // Persist the directory to the DB (upsert — handles duplicates gracefully).
  // The refresh_folders flag causes the in-memory list to reload on task completion.
  status_message_ = "Scanning folder... Please wait.";
  pending_task_ = PendingTask{
      .result = std::async(std::launch::async,
                           [this, d = std::move(dir)]() -> std::string {
                             // Register in DB first; surface any failure in the result string
                             // so the user knows the folder won't persist across restarts.
                             auto reg = svc_.add_scan_directory(d);
                             if (!reg) {
                               ROMULUS_WARN("Could not register folder: {}", reg.error().message);
                               return "Folder not saved to DB (" + reg.error().message +
                                      "). Scan aborted.";
                             }
                             // Then scan
                             auto result = svc_.scan_directory(d);
                             if (!result) {
                               return "Scan failed: " + result.error().message;
                             }
                             return "Scan complete: " + std::to_string(result->files_scanned) +
                                    " files, " + std::to_string(result->files_hashed) + " hashed.";
                           }),
      .refresh_dat_versions = false,
      .refresh_checklist = false,
      .refresh_folders = true,
  };
}

void GuiApp::action_remove_folder(std::int64_t id) {
  if (is_busy()) {
    return;
  }
  auto result = svc_.remove_scan_directory(id);
  if (!result) {
    ROMULUS_WARN("Could not remove folder: {}", result.error().message);
    status_message_ = "Failed to remove folder: " + result.error().message;
  } else {
    status_message_ = "Folder removed.";
  }
  refresh_folders();
  show_toast(status_message_);
}

void GuiApp::action_rescan_folders() {
  if (is_busy() || scanned_dirs_.empty()) {
    return;
  }

  status_message_ = "Rescanning all folders... Please wait.";
  // Collect paths from the DB snapshot for the async lambda
  std::vector<std::string> paths;
  paths.reserve(scanned_dirs_.size());
  for (const auto& d : scanned_dirs_) {
    paths.push_back(d.path);
  }

  pending_task_ = PendingTask{
      .result =
          std::async(std::launch::async,
                     [this, paths = std::move(paths)]() -> std::string {
                       std::int64_t total_scanned = 0;
                       std::int64_t total_hashed = 0;
                       for (const auto& p : paths) {
                         auto result = svc_.scan_directory(std::filesystem::path(p));
                         if (result) {
                           total_scanned += result->files_scanned;
                           total_hashed += result->files_hashed;
                         } else {
                           ROMULUS_WARN("Rescan failed for '{}': {}", p, result.error().message);
                         }
                       }
                       return "Rescan complete: " + std::to_string(total_scanned) + " files, " +
                              std::to_string(total_hashed) + " hashed.";
                     }),
      .refresh_dat_versions = false,
      .refresh_checklist = false,
      .refresh_folders = false,
  };
}

void GuiApp::action_check_dat() {
  if (is_busy() || selected_dat_index_ < 0) {
    return;
  }

  const auto& dv = dat_versions_[static_cast<std::size_t>(selected_dat_index_)];
  auto dat_id = dv.id;
  auto dat_name = dv.name;

  status_message_ = "Verifying and checking DAT... Please wait.";
  pending_task_ = PendingTask{
      .result = std::async(std::launch::async,
                           [this, dat_id, dat_name]() -> std::string {
                             // Run verify first to update statuses
                             auto verify_result = svc_.verify(dat_name);
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

void GuiApp::action_delete_dat() {
  if (is_busy() || selected_dat_index_ < 0 ||
      selected_dat_index_ >= static_cast<int>(dat_versions_.size())) {
    return;
  }

  const auto& dv = dat_versions_[static_cast<std::size_t>(selected_dat_index_)];
  const auto dat_id = dv.id;
  const auto dat_name = dv.name + " v" + dv.version;

  status_message_ = "Deleting DAT... Please wait.";
  pending_task_ = PendingTask{
      .result = std::async(std::launch::async,
                           [this, dat_id, dat_name]() -> std::string {
                             auto result = svc_.delete_dat(dat_id);
                             if (!result) {
                               return "Delete failed: " + result.error().message;
                             }
                             return "Deleted DAT: " + dat_name;
                           }),
      .refresh_dat_versions = true,
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
      .refresh_folders = true,
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
  bool should_refresh_folders = pending_task_->refresh_folders;
  pending_task_.reset();

  // Handle check_dat result: populates checklist from service on main thread
  if (should_refresh_checklist && task_result.starts_with("OK:")) {
    // Re-fetch on main thread to populate both the flat ROM list and the per-game list.
    if (selected_dat_index_ >= 0) {
      const auto& dv = dat_versions_[static_cast<std::size_t>(selected_dat_index_)];
      auto roms = svc_.get_roms_with_status(dv.id);
      if (roms) {
        rom_checklist_.clear();
        rom_checklist_.reserve(roms->size());
        checklist_stats_ = {};
        ++rom_checklist_generation_; // Invalidate the per-game ROM index cache.

        // Accumulate per-game data keyed by game_id; iteration order is not significant.
        std::unordered_map<std::int64_t, GameChecklistEntry> game_map;

        for (const auto& [rom, st] : *roms) {
          // Build ROM checklist entry
          std::string name_lower = rom.name;
          std::ranges::transform(name_lower, name_lower.begin(), ascii_lower);
          rom_checklist_.push_back({
              .game_id = rom.game_id,
              .name = rom.name,
              .name_lower = std::move(name_lower),
              .size = rom.size,
              .sha1 = rom.sha1,
              .md5 = rom.md5,
              .crc32 = rom.crc32,
              .status = st,
          });

          // Update ROM-level stats
          switch (st) {
            case core::RomStatusType::Verified:
              ++checklist_stats_.verified;
              break;
            case core::RomStatusType::Missing:
              ++checklist_stats_.missing;
              break;
            case core::RomStatusType::Unverified:
              ++checklist_stats_.unverified;
              break;
            case core::RomStatusType::Mismatch:
              ++checklist_stats_.mismatch;
              break;
          }

          // Accumulate per-game entry
          auto& game = game_map[rom.game_id];
          if (game.name.empty()) {
            // First ROM seen for this game — initialise the entry.
            game.game_id = rom.game_id;
            game.name = rom.game_name;
            game.name_lower = rom.game_name;
            std::ranges::transform(game.name_lower, game.name_lower.begin(), ascii_lower);
            game.rom_count = 1;
            game.status = st;
          } else {
            ++game.rom_count;
            // Aggregate status priority: Mismatch > Unverified > Missing > Verified.
            // A single Mismatch contaminates the whole game (bad hash found).
            // Any mix of statuses (e.g. some Verified + some Missing) means the game
            // is only partially complete, which we report as Unverified.
            if (st == core::RomStatusType::Mismatch ||
                game.status == core::RomStatusType::Mismatch) {
              game.status = core::RomStatusType::Mismatch;
            } else if (st != game.status) {
              // Mixed statuses (e.g. Verified + Missing) => partial, treated as Unverified.
              game.status = core::RomStatusType::Unverified;
            }
          }
        }

        checklist_stats_.total = static_cast<std::int64_t>(rom_checklist_.size());

        // Convert game map to vector and sort.
        game_checklist_.clear();
        game_checklist_.reserve(game_map.size());
        for (auto& [id, entry] : game_map) {
          game_checklist_.push_back(std::move(entry));
        }
        checklist_stats_.games_total = static_cast<std::int64_t>(game_checklist_.size());

        // Reset selected game if it is no longer present in the new checklist.
        if (selected_game_id_ >= 0) {
          const bool still_present =
              std::ranges::any_of(game_checklist_, [this](const GameChecklistEntry& g) {
                return g.game_id == selected_game_id_;
              });
          if (!still_present) {
            selected_game_id_ = -1;
          }
        }

        apply_checklist_sort();
        apply_game_sort();

        status_message_ = "Check complete: " + std::to_string(checklist_stats_.verified) + " / " +
                          std::to_string(checklist_stats_.total) + " ROMs available.";
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
  if (should_refresh_folders) {
    refresh_folders();
  }

  show_toast(status_message_);
}

bool GuiApp::is_busy() const {
  return pending_task_.has_value() && pending_task_->result.valid();
}

// ═════════════════════════════════════════════════════════════════
// Data Refresh
// ═════════════════════════════════════════════════════════════════

void GuiApp::refresh_dat_versions() {
  auto result = svc_.list_dat_versions();
  if (result) {
    // Remember the currently selected DAT by ID so selection survives the sort.
    std::int64_t prev_selected_id = -1;
    if (selected_dat_index_ >= 0 && selected_dat_index_ < static_cast<int>(dat_versions_.size())) {
      prev_selected_id = dat_versions_[static_cast<std::size_t>(selected_dat_index_)].id;
    }

    dat_versions_ = std::move(*result);

    // Sort alphabetically by name (case-insensitive) so the dropdown is easy to navigate.
    std::ranges::sort(dat_versions_, [](const core::DatVersion& a, const core::DatVersion& b) {
      return std::lexicographical_compare(
          a.name.begin(), a.name.end(), b.name.begin(), b.name.end(), [](char lc, char rc) {
            return ascii_lower(lc) < ascii_lower(rc);
          });
    });

    // Restore selection: prefer the previously selected DAT (by ID), fall back to first.
    selected_dat_index_ = -1;
    for (int i = 0; i < static_cast<int>(dat_versions_.size()); ++i) {
      if (dat_versions_[static_cast<std::size_t>(i)].id == prev_selected_id) {
        selected_dat_index_ = i;
        break;
      }
    }
    if (selected_dat_index_ < 0 && !dat_versions_.empty()) {
      selected_dat_index_ = 0;
    }
    // Clear checklist if the previously selected DAT is no longer available.
    if (selected_dat_index_ < 0 ||
        dat_versions_[static_cast<std::size_t>(selected_dat_index_)].id != prev_selected_id) {
      rom_checklist_.clear();
      game_checklist_.clear();
      selected_game_id_ = -1;
      checklist_stats_ = {};
    }
  } else {
    dat_versions_.clear();
    selected_dat_index_ = -1;
    ROMULUS_WARN("Failed to refresh DAT versions: {}", result.error().message);
  }
}

void GuiApp::refresh_folders() {
  auto result = svc_.get_scan_directories();
  if (result) {
    scanned_dirs_ = std::move(*result);
  } else {
    ROMULUS_WARN("Failed to refresh scan directories: {}", result.error().message);
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
