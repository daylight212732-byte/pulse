#pragma once
#include <functional>
#include <string>
#include <windows.h>

namespace pulse {
struct AppState;
using SettingsSearchResults = std::function<bool(const std::wstring&, HWND)>;
void FocusSettingsSearchEditor(AppState& s, SettingsSearchResults results);
void LayoutSettingsSearchEditor(AppState& s);
}
