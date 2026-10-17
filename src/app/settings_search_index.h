#pragma once

#include "../common/localization.h"
#include <string_view>

namespace pulse::app {

// Higher scores are better. Empty/whitespace queries score 0; misses score -1.
int SettingsSearchScore(std::wstring_view query, std::wstring_view title,
                       std::wstring_view page, std::wstring_view aliases);

std::wstring_view SettingsSearchAliases(l10n::StringId title);

} // namespace pulse::app
