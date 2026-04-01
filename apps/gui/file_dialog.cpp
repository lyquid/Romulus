#include "file_dialog.hpp"

#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #define NOMINMAX
  #include <windows.h>
  // NOLINTNEXTLINE(misc-include-cleaner) — commdlg.h requires windows.h first
  #include <commdlg.h>
  // NOLINTNEXTLINE(misc-include-cleaner) — shobjidl.h requires windows.h first
  #include <shobjidl.h>
#else
  #include <cstdio>
  #include <cstdlib>
#endif

#include <string>

namespace romulus::gui {

#ifdef _WIN32

// ── Windows implementation ───────────────────────────────────
// Uses IFileOpenDialog (Vista+) for folder picker,
// GetOpenFileNameA for file picker.

auto open_folder_dialog() -> std::string {
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

  IFileOpenDialog* dialog = nullptr;
  HRESULT hr =
      CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
  if (FAILED(hr)) {
    CoUninitialize();
    return "";
  }

  DWORD options = 0;
  dialog->GetOptions(&options);
  dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
  dialog->SetTitle(L"Select ROM Directory");

  hr = dialog->Show(nullptr);
  std::string result;

  if (SUCCEEDED(hr)) {
    IShellItem* item = nullptr;
    hr = dialog->GetResult(&item);
    if (SUCCEEDED(hr) && item != nullptr) {
      PWSTR path = nullptr;
      hr = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
      if (SUCCEEDED(hr) && path != nullptr) {
        int len = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
        if (len > 0) {
          result.resize(static_cast<std::size_t>(len - 1));
          WideCharToMultiByte(CP_UTF8, 0, path, -1, result.data(), len, nullptr, nullptr);
        }
        CoTaskMemFree(path);
      }
      item->Release();
    }
  }

  dialog->Release();
  CoUninitialize();
  return result;
}

auto open_file_dialog() -> std::string {
  char filename[MAX_PATH] = "";
  OPENFILENAMEA ofn = {};
  ofn.lStructSize = sizeof(ofn);
  ofn.lpstrFilter = "DAT Files (*.dat)\0*.dat\0All Files (*.*)\0*.*\0";
  ofn.lpstrFile = filename;
  ofn.nMaxFile = MAX_PATH;
  ofn.lpstrTitle = "Select DAT File";
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

  if (GetOpenFileNameA(&ofn) != 0) {
    return filename;
  }
  return "";
}

#else // Linux / macOS

// ── POSIX implementation ─────────────────────────────────────
// Tries zenity (GNOME), then kdialog (KDE).
// Returns empty string if no dialog tool is available.

namespace {

auto try_command(const char* cmd) -> std::string {
  // NOLINTNEXTLINE(cert-env33-c) — commands are compile-time constants, not user input
  FILE* pipe = popen(cmd, "r");
  if (pipe == nullptr) {
    return "";
  }

  std::string result;
  char buf[256];
  while (std::fgets(buf, sizeof(buf), pipe) != nullptr) {
    result += buf;
  }

  int status = pclose(pipe);
  if (status != 0 || result.empty()) {
    return "";
  }

  while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
    result.pop_back();
  }
  return result;
}

auto has_display() -> bool {
  return std::getenv("DISPLAY") != nullptr || std::getenv("WAYLAND_DISPLAY") != nullptr;
}

} // namespace

auto open_folder_dialog() -> std::string {
  if (!has_display()) {
    return "";
  }

  auto result =
      try_command("zenity --file-selection --directory --title='Select ROM Directory' 2>/dev/null");
  if (!result.empty()) {
    return result;
  }

  result =
      try_command("kdialog --getexistingdirectory ~ --title 'Select ROM Directory' 2>/dev/null");
  return result;
}

auto open_file_dialog() -> std::string {
  if (!has_display()) {
    return "";
  }

  auto result = try_command("zenity --file-selection --title='Select DAT File' "
                            "--file-filter='DAT files|*.dat' 2>/dev/null");
  if (!result.empty()) {
    return result;
  }

  result = try_command("kdialog --getopenfilename ~ 'DAT files (*.dat)' "
                       "--title 'Select DAT File' 2>/dev/null");
  return result;
}

#endif

} // namespace romulus::gui
