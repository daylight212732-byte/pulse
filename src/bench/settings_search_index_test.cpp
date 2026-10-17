#include "../app/settings_search_index.h"

#include <iostream>

int main() {
    using namespace pulse;
    using I = l10n::StringId;
    using app::SettingsSearchAliases;
    using app::SettingsSearchScore;
    int failures = 0;
    const auto check = [&](bool ok, const char* label) {
        std::cout << (ok ? "[PASS] " : "[FAIL] ") << label << '\n';
        if (!ok) ++failures;
    };
    const auto font = [&](std::wstring_view query) {
        return SettingsSearchScore(query, L"界面字号", L"常规", SettingsSearchAliases(I::SettingsUiFontSize));
    };
    check(font(L"字体") > 0, "Chinese font synonym finds interface text size");
    check(font(L"字體") > 0, "Traditional Chinese font synonym finds interface text size");
    check(font(L"FONT") > 0, "English aliases ignore case");
    check(font(L" text  size ") > 0, "Whitespace-normalized phrase matches aliases");
    check(font(L"界 面 字 号") > 0, "Spaces do not break title matches");
    check(font(L"字号 界面") > 0, "All query tokens can match in any order");
    check(font(L"常规 字体") > 0, "All query tokens can span page and aliases");
    check(font(L"字体 根本不存在") == -1, "One matching token cannot mask a missing token");
    check(font(L"界号") > 0, "Ordered noncontinuous Chinese characters match");
    check(font(L"号界") == -1, "Ordered matching preserves character order");
    check(font(L"没有这个设置") == -1, "Unrelated Chinese query is a miss");
    check(font(L"") == 0 && font(L" \t\r\n") == 0, "Empty and whitespace queries have stable zero scores");
    check(SettingsSearchScore(L"search", L"Search", L"General", L"") >
          SettingsSearchScore(L"search", L"Global search", L"General", L""), "Exact titles rank above title substrings");
    check(SettingsSearchScore(L"font", L"Font size", L"General", L"") > font(L"font"),
          "Title matches rank above aliases");
    check(font(L"font") > SettingsSearchScore(L"font", L"Other", L"Font", L""),
          "Aliases rank above page-only matches");
    check(SettingsSearchScore(L"自动 更新", L"自动更新", L"关于", SettingsSearchAliases(I::SettingsAutoUpdate)) > 0,
          "Spaced automatic-update query finds update setting");
    check(SettingsSearchScore(L"startup tray", L"启动时最小化到托盘", L"常规", SettingsSearchAliases(I::SettingsStartInTray)) > 0,
          "English tray-startup phrase finds Chinese setting");
    check(SettingsSearchScore(L"隐藏文件", L"显示隐藏项目", L"常规", SettingsSearchAliases(I::SettingsShowHidden)) > 0,
          "Hidden-files synonym finds visibility setting");
    check(SettingsSearchScore(L"预览增强包", L"快速预览", L"快速预览", SettingsSearchAliases(I::QuickPreview)) > 0,
          "Preview-pack synonym finds preview page");
    check(SettingsSearchScore(L"wallpape", L"Wallpaper", L"General", L"") > 0,
          "One missing letter in a long English word matches");
    check(SettingsSearchScore(L"wallpaperr", L"Wallpaper", L"General", L"") > 0,
          "One extra letter in a long English word matches");
    check(SettingsSearchScore(L"wallpaxer", L"Wallpaper", L"General", L"") > 0,
          "One wrong letter in a long English word matches");
    check(SettingsSearchScore(L"wallpaepr", L"Wallpaper", L"General", L"") > 0,
          "Adjacent transposed letters match");
    check(SettingsSearchScore(L"wxllpxper", L"Wallpaper", L"General", L"") == -1,
          "Two replacement errors are rejected");
    check(SettingsSearchScore(L"foxt", L"Font", L"General", L"") == -1,
          "Short English words do not enable typo matching");
    check(SettingsSearchScore(L"sear", L"System configuration", L"General", L"") == -1,
          "Unrelated long text is not an arbitrary subsequence match");
    check(SettingsSearchScore(L"", L"", L"", L"") == 0 &&
          SettingsSearchScore(L"query", L"", L"", L"") == -1,
          "Empty index fields have defined behavior");
    check(SettingsSearchAliases(I::SettingsThemeLight).empty(), "Unknown synonym key returns an empty view");
    std::cout << "failures=" << failures << '\n';
    return failures ? 1 : 0;
}
