#include "app_internal.h"
#include "app_input.h"
#include "../ui/ui_renderer_internal.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

#ifdef PULSE_WITH_SELFTEST
using namespace pulse;
void Render(pulse::AppState&);

namespace {
D2D1_RECT_F SectionContent(const ui::SettingsLayout& l, ui::SettingsSection section) {
    using S = ui::SettingsSection;
    switch (section) {
    case S::Appearance: return l.group[0];
    case S::Integration: return l.integration_card;
    case S::Startup: return l.group[1];
    case S::FileList: return l.group[2];
    case S::FilenameSearch: return l.global_search_row;
    case S::ContentSearch: return l.group[1];
    case S::PulseMenu: return l.pulse_card;
    case S::About: return l.about_card;
    case S::Apps: return l.apps_card;
    case S::Diagnostics: return l.diagnostics_card;
    case S::Updates: return l.update_card;
    case S::Releases: return l.release_card;
    case S::DuplicateOptions: return l.duplicate_options;
    case S::PreviewTrigger: return l.trigger_row;
    case S::PreviewSystem: return l.preview_codec_group;
    case S::MediaPack: return l.pack_card;
    case S::ImagePacks: return l.pack_images_card;
    case S::ArchivePack: return l.pack_archive_card;
    case S::PreviewAdvanced: return l.pack_group;
    default: return {};
    }
}

bool HeadersSeparate(const ui::SettingsLayout& layout) {
    float bottom = -1000000;
    for (const auto& section : layout.sections) {
        if (section.header.top < bottom - 0.1f || section.header.bottom <= section.header.top) return false;
        bottom = section.header.bottom;
    }
    return true;
}
}

int RunSettingsSectionsSearchTest(AppState& s, const wchar_t* output) {
    using H = ui::HitTestResult;
    using S = ui::SettingsSection;
    using I = l10n::StringId;
    std::ofstream log{std::filesystem::path(output)};
    int failures = 0;
    auto check = [&](bool ok, const char* label) {
        log << (ok ? "[PASS] " : "[FAIL] ") << label << std::endl;
        if (!ok) ++failures;
    };
    s.appPrefs.persist = false;
    const float original_scale = s.scale;
    const bool original_dark = s.darkMode;
    const unsigned original_expanded = s.settingsExpanded;
    const uint32_t original_collapsed = s.settingsCollapsed;
    log << "opening settings" << std::endl;
    OpenSettingsTab(s, 0);
    log << "opened settings" << std::endl;
    ui::fluent::Painter painter(&s.compositor);
    const uint32_t all = (1u << static_cast<unsigned>(S::Count)) - 1;
    check(s.settingsCollapsed == all && s.settingsExpanded == 0,
          "new settings window starts with sections and advanced disclosures collapsed");
    Render(s);
    check(s.compositor.SaveSnapshot((std::filesystem::path(output).parent_path()/L"settings-default-collapsed.png").c_str()),
          "capture default collapsed settings");
    for (float scale : {1.0f, 1.5f, 2.0f}) {
        // HitTest measures text through the renderer's painter. Recreate its
        // caches together with the compositor's formats, then prime a frame.
        s.scale = scale;
        log << "invalidate renderer scale=" << scale << std::endl;
        s.renderer.InvalidateTypography();
        painter.InvalidateTypography();
        log << "recreate formats" << std::endl;
        s.compositor.RecreateTextFormats(scale);
        s.renderer.SetScale(scale);
        painter.SetScale(scale);
        log << "prime frame" << std::endl;
        Render(s);
        log << "frame ready" << std::endl;
        for (float width : {620.0f, 1280.0f}) {
            const auto window = D2D1::RectF(0, 0, width*scale, 20000*scale);
            auto layout = [&](const ui::WindowViewModel& vm) {
                return ui::MakeSettingsLayout(vm, window, scale, s.renderer.TitleBarHeight(), 28*scale, &painter);
            };
            for (int page = 0; page <= 5; ++page) {
                // BuildVm fills page-specific data only for the active settings
                // tab; overriding settings_page on a file-pane VM skips it.
                OpenSettingsTab(s, page);
                auto vm = BuildVm(s, false);
                vm.settings_open = true;
                vm.settings_page = page;
                vm.settings_scroll = 0;
                vm.settings_expanded = 0xffffffffu;
                vm.settings_collapsed = 0;
                vm.dup_scanning = false;
                const auto expanded = layout(vm);
                log << "page=" << page << " scale=" << scale << " width=" << width << std::endl;
                check(!expanded.sections.empty(), "settings page exposes collapsible sections");
                check(HeadersSeparate(expanded), "expanded headers do not overlap");
                log << "hit fixed search" << std::endl;
                const auto search = s.renderer.HitTest(vm, window,
                    (expanded.search.left + expanded.search.right)/2,
                    (expanded.search.top + expanded.search.bottom)/2);
                check(search.region == H::SettingsFind, "drawn fixed search field has a real hit target");
                for (const auto& section : expanded.sections) {
                    log << "section=" << static_cast<unsigned>(section.id) << std::endl;
                    vm.settings_collapsed = ui::SettingsSectionBit(section.id);
                    const auto collapsed = layout(vm);
                    check(collapsed.content_h < expanded.content_h - 0.1f, "single collapse reduces content height");
                    check(HeadersSeparate(collapsed), "collapse keeps adjacent headers separate");
                    const auto hidden = SectionContent(collapsed, section.id);
                    check(hidden.bottom < collapsed.content.top, "collapsed content container is outside the viewport");
                    const auto hidden_hit = s.renderer.HitTest(vm, window,
                        (hidden.left + hidden.right)/2, (hidden.top + hidden.bottom)/2);
                    check(hidden_hit.region != H::SettingsToggle && hidden_hit.region != H::SettingsTheme &&
                          hidden_hit.region != H::SettingsDropdown && hidden_hit.region != H::SettingsSectionToggle,
                          "hidden body does not hit its original settings controls");
                    const auto found = std::find_if(collapsed.sections.begin(), collapsed.sections.end(),
                        [&](const auto& entry) { return entry.id == section.id; });
                    if (found != collapsed.sections.end()) {
                        const auto toggle = s.renderer.HitTest(vm, window,
                            found->header.right - 16*scale, (found->header.top + found->header.bottom)/2);
                        check(toggle.region == H::SettingsSectionToggle && toggle.index == static_cast<int>(section.id),
                              "collapsed section header remains interactive");
                    } else check(false, "collapsed section keeps its header");
                    vm.settings_collapsed = 0;
                    check(std::abs(layout(vm).content_h - expanded.content_h) < 0.1f,
                          "expanding restores original content height");
                }
                vm.settings_collapsed = all;
                const auto collapsed = layout(vm);
                check(HeadersSeparate(collapsed) && collapsed.content_h < expanded.content_h,
                      "all collapsed sections retain valid compact geometry");
                const auto short_window = D2D1::RectF(0, 0, width*scale, 600*scale);
                const float max_scroll = s.renderer.SettingsMaxScroll(vm, short_window.right, short_window.bottom);
                check(max_scroll >= 0 && max_scroll <= (std::max)(0.0f, expanded.content_h),
                      "all-collapsed scroll range remains bounded");
            }
            OpenSettingsTab(s, 4);
            auto duplicates = BuildVm(s, false);
            duplicates.settings_scroll = 0;
            duplicates.settings_collapsed = ui::SettingsSectionBit(S::DuplicateOptions);
            duplicates.dup_scanning = false;
            duplicates.dup_show_progress = false;
            duplicates.dup_empty.clear();
            duplicates.dup_groups.assign(8, {L"Fixture group", {{L"a.txt", L"", L"fixture", true},
                                                               {L"b.txt", L"", L"fixture", false}}});
            const auto short_window = D2D1::RectF(0, 0, width*scale, 600*scale);
            const auto compact_results = ui::MakeSettingsLayout(duplicates, short_window, scale,
                s.renderer.TitleBarHeight(), 28*scale, &painter);
            const auto& revealed = compact_results.dup_group_cards[1];
            const float row_y = revealed.top + 64*scale;
            check(row_y < compact_results.content.bottom, "collapse reveals second duplicate result row");
            const auto keep = s.renderer.HitTest(duplicates, short_window, revealed.left + 40*scale, row_y);
            const auto open = s.renderer.HitTest(duplicates, short_window, revealed.left + 140*scale, row_y);
            check(keep.region == H::SettingsDupKeep && keep.index == 1 && keep.sub_index == 0,
                  "newly revealed duplicate keep control remains clickable");
            check(open.region == H::SettingsDupOpen && open.index == 1 && open.sub_index == 0,
                  "newly revealed duplicate file remains clickable");
            duplicates.dup_scanning = true;
            const auto scanning = ui::MakeSettingsLayout(duplicates, short_window, scale,
                s.renderer.TitleBarHeight(), 28*scale, &painter);
            check(scanning.sections.empty() && scanning.dup_cancel.top > scanning.content.top,
                  "active scan keeps options and cancellation visible despite collapse state");
        }
    }
    s.scale = original_scale;
    s.renderer.InvalidateTypography();
    painter.InvalidateTypography();
    s.compositor.RecreateTextFormats(original_scale);
    s.renderer.SetScale(original_scale);
    painter.SetScale(original_scale);
    OpenSettingsTab(s, 0);
    s.settingsCollapsed = 0;
    s.settingsExpanded = 0;
    s.settings.SetScroll(0, 0);
    const auto bounds = D2D1::RectF(0, 0, static_cast<float>(s.compositor.Width()),
                                  static_cast<float>(s.compositor.Height()));
    auto live_layout = [&] {
        return ui::MakeSettingsLayout(BuildVm(s, false), bounds, s.scale,
            s.renderer.TitleBarHeight(), 28*s.scale, &painter);
    };
    s.settings.SetScroll(100000, 100000);
    H toggle;
    toggle.region = H::SettingsSectionToggle;
    for (const auto& section : live_layout().sections) {
        toggle.index = static_cast<int>(section.id);
        HandleSettingsControl(s, toggle);
    }
    check(s.settings.scroll() <= s.renderer.SettingsMaxScroll(BuildVm(s, false), bounds.right, bounds.bottom) + 0.1f,
          "real collapse handler clamps an existing large scroll offset");
    static std::wstring query;
    static bool cancel = false;
    auto search = [&](std::wstring text, bool escape) {
        query = std::move(text);
        cancel = escape;
        EnsureMenu(s);
        s.menu->SetHoverFirstOnOpen(true);
        H find;
        find.region = H::SettingsFind;
        HandleSettingsControl(s, find);
        const HWND edit = GetFocus();
        SetWindowTextW(edit, query.c_str());
        SetTimer(s.hwnd, 0x5353, 20, [](HWND hwnd, UINT, UINT_PTR id, DWORD) {
            KillTimer(hwnd, id);
            SetWindowTextW(GetFocus(), query.c_str());
            PostThreadMessageW(GetCurrentThreadId(), WM_KEYDOWN, cancel ? VK_ESCAPE : VK_RETURN, 0);
        });
        SendMessageW(s.hwnd, RegisterWindowMessageW(L"Pulse.SettingsSearchChanged"), 0, 0);
        if (escape) SendMessageW(edit, WM_KEYDOWN, VK_ESCAPE, 0);
    };
    s.settingsCollapsed = all;
    search(L"字体", false);
    auto vm = BuildVm(s, false);
    check(vm.settings_page == 0 && !(s.settingsCollapsed & ui::SettingsSectionBit(S::Appearance)),
          "font synonym opens and expands Appearance");
    const float expected = (std::min)(s.renderer.SettingsDestinationOffset(vm, static_cast<int>(I::SettingsUiFontSize),
        bounds.right, bounds.bottom), s.renderer.SettingsMaxScroll(vm, bounds.right, bounds.bottom));
    check(std::abs(s.settings.scroll() - expected) < 0.5f, "font synonym selects and scrolls to interface text size");
    s.settingsExpanded = 0;
    search(l10n::Get(I::SettingsWallpaper), false);
    check(BuildVm(s, false).settings_page == 0 && (s.settingsExpanded & 1u),
          "search still expands hidden advanced wallpaper settings");
    s.settingsCollapsed = all;
    search(L"version", false);
    check(BuildVm(s, false).settings_page == 3 && !(s.settingsCollapsed & ui::SettingsSectionBit(S::About)),
          "version search expands the About card");
    s.settingsCollapsed = all;
    search(L"logs", false);
    vm = BuildVm(s, false);
    const float diagnostics_offset = (std::min)(s.renderer.SettingsDestinationOffset(vm, static_cast<int>(I::Diagnostics),
        bounds.right, bounds.bottom), s.renderer.SettingsMaxScroll(vm, bounds.right, bounds.bottom));
    check(vm.settings_page == 3 && !(s.settingsCollapsed & ui::SettingsSectionBit(S::Diagnostics)) &&
          std::abs(s.settings.scroll() - diagnostics_offset) < 0.5f,
          "logs search expands and scrolls to Diagnostics");
    const auto before_cancel = BuildVm(s, false);
    const uint32_t before_mask = s.settingsCollapsed;
    search(L"no-such-setting-123", true);
    check(BuildVm(s, false).settings_page == before_cancel.settings_page &&
          s.settingsCollapsed == before_mask && std::abs(s.settings.scroll() - before_cancel.settings_scroll) < 0.1f,
          "no-result search closes with Escape without changing destination");
    auto snapshot = [&](const wchar_t* name) {
        Render(s);
        Sleep(250); // Wait for the 200 ms navigation highlight before capturing.
        Render(s);
        return s.compositor.SaveSnapshot((std::filesystem::path(output).parent_path()/name).c_str());
    };
    OpenSettingsTab(s, 0);
    s.settings.SetScroll(0, 0);
    s.darkMode = false;
    s.settingsCollapsed = 0;
    check(snapshot(L"settings-sections-light.png"), "capture expanded light settings");
    s.darkMode = true;
    s.settingsCollapsed = all;
    check(snapshot(L"settings-sections-dark-collapsed.png"), "capture collapsed dark settings");
    struct PageSnapshot { int page; const wchar_t* expanded; const wchar_t* collapsed; };
    const PageSnapshot pages[] = {
        {1, L"settings-sections-index-expanded.png", L"settings-sections-index-collapsed.png"},
        {2, L"settings-sections-menu-expanded.png", L"settings-sections-menu-collapsed.png"},
        {3, L"settings-sections-about-expanded.png", L"settings-sections-about-collapsed.png"},
        {4, L"settings-sections-duplicates-expanded.png", L"settings-sections-duplicates-collapsed.png"},
        {5, L"settings-sections-preview-expanded.png", L"settings-sections-preview-collapsed.png"},
    };
    s.settingsExpanded = 0xffffffffu;
    for (const auto& page : pages) {
        OpenSettingsTab(s, page.page);
        s.settings.SetScroll(0, 0);
        s.settingsCollapsed = 0;
        log << "snapshot page=" << page.page << std::endl;
        check(snapshot(page.expanded), "capture expanded page for comparison");
        s.settingsCollapsed = all;
        s.settings.SetScroll(0, 0);
        check(snapshot(page.collapsed), "capture collapsed page for orphaned text and overlap inspection");
    }
    s.settingsExpanded = 0;
    check(snapshot(L"settings-sections-preview-compact.png"), "capture preview with existing format disclosure closed");
    const UINT original_width = s.compositor.Width(), original_height = s.compositor.Height();
    s.compositor.Resize(static_cast<UINT>(620*original_scale), static_cast<UINT>(800*original_scale));
    auto narrow_snapshot = [&](const wchar_t* name) {
        auto* dc = s.compositor.Dc();
        const auto window = D2D1::RectF(0, 0, 620*original_scale, 800*original_scale);
        for (int frame = 0; frame < 2; ++frame) {
            dc->BeginDraw();
            s.renderer.Render(BuildVm(s, false), window, ui::MakeTheme(s.darkMode, s.accentColor));
            if (FAILED(dc->EndDraw())) return false;
            if (frame == 0) Sleep(250);
        }
        return s.compositor.SaveSnapshot((std::filesystem::path(output).parent_path()/name).c_str());
    };
    OpenSettingsTab(s, 0);
    s.settingsExpanded = 0;
    s.settingsCollapsed = 0;
    s.settings.SetScroll(0, 0);
    check(narrow_snapshot(L"settings-sections-narrow-expanded.png"), "capture narrow expanded settings");
    s.settingsCollapsed = all;
    check(narrow_snapshot(L"settings-sections-narrow-collapsed.png"), "capture narrow collapsed settings");
    s.compositor.Resize(original_width, original_height);
    s.darkMode = original_dark;
    s.settingsExpanded = original_expanded;
    s.settingsCollapsed = original_collapsed;
    s.settings.SetScroll(0, 0);
    log << "failures=" << failures << std::endl;
    return failures ? 1 : 0;
}
#endif
