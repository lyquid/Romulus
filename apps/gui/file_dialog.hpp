#pragma once

/// @file file_dialog.hpp
/// @brief Cross-platform native file/folder picker dialogs.
/// Uses Windows Shell API on Windows, zenity/kdialog on Linux.
/// Returns empty string if the user cancels or no dialog tool is available.

#include <string>

namespace romulus::gui {

/// Opens a native folder picker dialog.
/// @return Selected folder path, or empty string on cancel/unavailable.
[[nodiscard]] std::string open_folder_dialog();

/// Opens a native file picker dialog filtered for DAT files.
/// @return Selected file path, or empty string on cancel/unavailable.
[[nodiscard]] std::string open_file_dialog();

} // namespace romulus::gui
