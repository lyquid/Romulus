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
#include <ranges>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace romulus::gui {

namespace {

constexpr int k_WindowWidth = 1280;
constexpr int k_WindowHeight = 720;
constexpr auto* k_WindowTitle = "ROMULUS — ROM Collection Verifier";
constexpr auto* k_GlslVersion = "#version 130";
constexpr float k_ToastDuration = 2.5F;
constexpr float k_ToastPaddingH = 16.0F; // horizontal padding on each side
constexpr float k_ToastPaddingV = 10.0F; // vertical padding on each side
constexpr float k_ToastMarginRight = 10.0F;
constexpr float k_ToastMarginBottom = 50.0F;
constexpr float k_ToastRounding = 5.0F;
// Toast colours — encoded as per-channel alpha multipliers (0–255 range before scaling).
constexpr float k_ToastBgAlpha = 220.0F;
constexpr float k_ToastBorderAlpha = 180.0F;
constexpr float k_ToastTextAlpha = 255.0F;

// Active DAT banner — extra vertical padding (px) added to the computed text height.
constexpr float k_BannerExtraPadding = 6.0F;

// ROM checklist / detail panel column indices
constexpr int k_ColStatus = 0;
constexpr int k_ColRomName = 1;
constexpr int k_ColSize = 2;
constexpr int k_ColSha1 = 3;
constexpr int k_ColMd5 = 4;
constexpr int k_ColCrc32 = 5;

// Game panel column indices
constexpr int k_GameColStatus = 0;
constexpr int k_GameColName = 1;

// Status colours
constexpr ImVec4 k_ColorVerified{0.2F, 0.9F, 0.3F, 1.0F};   // green
constexpr ImVec4 k_ColorMissing{1.0F, 0.3F, 0.3F, 1.0F};    // red
constexpr ImVec4 k_ColorUnverified{1.0F, 0.9F, 0.2F, 1.0F}; // yellow
constexpr ImVec4 k_ColorMismatch{1.0F, 0.5F, 0.0F, 1.0F};   // orange

// Log panel colour scheme (RGBA)
constexpr ImVec4 k_ColorLogWarn{1.0F, 0.75F, 0.1F, 1.0F};   // amber  — warnings
constexpr ImVec4 k_ColorLogError{1.0F, 0.3F, 0.3F, 1.0F};   // red    — errors / critical
constexpr ImVec4 k_ColorLogDebug{0.6F, 0.6F, 0.6F, 1.0F};   // grey   — debug / trace
constexpr ImVec4 k_ColorLogDefault{1.0F, 1.0F, 1.0F, 1.0F}; // white  — info

// Status icon labels — ASCII symbols compatible with ImGui's default font.
// The Unicode checkmarks (✓ U+2713, ✗ U+2717) are not in the built-in ProggyClean
// font and show as '?' on most systems.  Use plain ASCII equivalents instead.
constexpr auto* k_IconVerified = "[OK] Verified";
constexpr auto* k_IconMissing = "[--] Missing";
constexpr auto* k_SymbolMissing = "[--]"; // standalone, for summary badges

/// ASCII-only character case fold: maps [A-Z] → [a-z], all other bytes pass through unchanged.
/// Safe for UTF-8 strings because non-ASCII bytes are always ≥ 0x80 and never in [A-Z].
/// Intended to be used with std::ranges::transform to fold an entire string.
[[nodiscard]] auto ascii_lower(char c) noexcept -> char {
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
}

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
      return k_IconVerified;
    case core::RomStatusType::Missing:
      return k_IconMissing;
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

/// Compact single-badge icon for a status — used in the games table Status column.
auto status_icon(core::RomStatusType status) -> const char* {
  switch (status) {
    case core::RomStatusType::Verified:
      return "[OK]";
    case core::RomStatusType::Missing:
      return "[--]";
    case core::RomStatusType::Unverified:
      return "[??]";
    case core::RomStatusType::Mismatch:
      return "[!!]";
  }
  return "[??]";
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

void GuiApp::render_dats_tab() {
  const bool busy = is_busy();

  // ── DAT controls ──────────────────────────────────────────────
  ImGui::BeginDisabled(busy);
  if (ImGui::Button("Import DAT")) {
    action_import_dat();
  }
  ImGui::EndDisabled();

  ImGui::SameLine();

  // DAT selector dropdown — expands to fill available width, leaving just room for "Check DAT".
  {
    std::string preview = "(No DAT selected)";
    if (selected_dat_index_ >= 0 && selected_dat_index_ < static_cast<int>(dat_versions_.size())) {
      const auto& dv = dat_versions_[static_cast<std::size_t>(selected_dat_index_)];
      preview = dv.name + " v" + dv.version;
    }

    ImGui::PushItemWidth(-110);
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
  const std::int64_t cnt_unverified = checklist_stats_.unverified;
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
  if (cnt_unverified > 0) {
    ImGui::SameLine(0.0F, 14.0F);
    ImGui::TextColored(
        k_ColorUnverified, "[??] %lld unverified", static_cast<long long>(cnt_unverified));
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
        "All", "Verified", "Missing", "Unverified", "Mismatch"};
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
  ImGui::BeginChild("##roms_panel", ImVec2(right_w, 0.0F), true);
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

        for (std::size_t i = 0; i < rom_checklist_.size(); ++i) {
          const auto& entry = rom_checklist_[i];
          if (entry.game_id != selected_game_id_) {
            continue;
          }

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
  constexpr int k_FolderCols = 2;
  if (ImGui::BeginTable("folders_table",
                        k_FolderCols,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                            ImGuiTableFlags_SizingStretchProp,
                        ImVec2(0, -30))) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_None, 6.0F);
    ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_None, 1.0F);
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

// ─────────────────────────────────────────────────────────────────
// DB Tab
// ─────────────────────────────────────────────────────────────────

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
      db_tab_loaded_ = true;
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
  ImGui::TextDisabled("(%zu rows)", db_table_data_.rows.size());

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

  // ── Data table (read-only) ────────────────────────────────────
  const int col_count = static_cast<int>(db_table_data_.columns.size());
  constexpr ImGuiTableFlags k_TableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                           ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY |
                                           ImGuiTableFlags_SizingFixedFit |
                                           ImGuiTableFlags_Resizable;

  if (ImGui::BeginTable("##db_table_view", col_count, k_TableFlags, ImVec2(0, -30))) {
    ImGui::TableSetupScrollFreeze(0, 1);
    for (int c = 0; c < col_count; ++c) {
      ImGui::TableSetupColumn(db_table_data_.columns[static_cast<std::size_t>(c)].name.c_str(),
                              ImGuiTableColumnFlags_None);
    }
    ImGui::TableHeadersRow();

    for (std::size_t r = 0; r < db_table_data_.rows.size(); ++r) {
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

void GuiApp::apply_checklist_sort() {
  if (checklist_sort_col_ < 0 || rom_checklist_.empty()) {
    return;
  }
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

        // Accumulate per-game data in insertion order using a map keyed by game_id.
        std::unordered_map<std::int64_t, GameChecklistEntry> game_map;
        game_map.reserve(roms->size()); // upper bound; actual games <= ROMs

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
          const bool still_present = std::ranges::any_of(
              game_checklist_,
              [this](const GameChecklistEntry& g) { return g.game_id == selected_game_id_; });
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

auto GuiApp::is_busy() const -> bool {
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
