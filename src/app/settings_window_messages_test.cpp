#include "app_internal.h"
#include "../ui/ui_renderer_internal.h"
#include <filesystem>
#include <fstream>

#ifdef PULSE_WITH_SELFTEST
using namespace pulse;
void Render(pulse::AppState&);
namespace {
struct SearchObservation {
    AppState* state = nullptr;
    HWND edit = nullptr;
    RECT anchor{};
    bool opened = false, anchored = false, compact = false, middle_click = false, cleared = false;
    bool clear = false;
};
SearchObservation observation;
BOOL CALLBACK FindPopup(HWND hwnd, LPARAM data) {
    wchar_t name[64]{};
    GetClassNameW(hwnd, name, ARRAYSIZE(name));
    if (wcscmp(name, L"PulseFluentMenu") == 0 && IsWindowVisible(hwnd)) {
        *reinterpret_cast<HWND*>(data) = hwnd;
        return FALSE;
    }
    return TRUE;
}
}

int RunSettingsWindowMessagesTest(AppState& s, const wchar_t* output) {
    using H = ui::HitTestResult;
    std::ofstream log{std::filesystem::path(output)};
    int failures = 0;
    auto check = [&](bool ok, const char* label) {
        log << (ok ? "[PASS] " : "[FAIL] ") << label << std::endl;
        if (!ok) ++failures;
    };
    s.appPrefs.persist = false;
    const auto bounds = D2D1::RectF(0, 0, static_cast<float>(s.compositor.Width()),
                                  static_cast<float>(s.compositor.Height()));
    auto click = [&](float x, float y) {
        const LPARAM point = MAKELPARAM(static_cast<int>(x), static_cast<int>(y));
        SendMessageW(s.hwnd, WM_MOUSEMOVE, 0, point);
        SendMessageW(s.hwnd, WM_SETCURSOR, reinterpret_cast<WPARAM>(s.hwnd), MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
        SendMessageW(s.hwnd, WM_LBUTTONDOWN, MK_LBUTTON, point);
        SendMessageW(s.hwnd, WM_LBUTTONUP, 0, point);
    };
    auto vm = std::make_unique<ui::WindowViewModel>(BuildVm(s, false));
    bool settings_button = false;
    for (float x = bounds.right - 220*s.scale; x < bounds.right; x += 4*s.scale) {
        if (s.renderer.HitTest(*vm, bounds, x, 20*s.scale).region != H::SettingsButton) continue;
        click(x, 20*s.scale);
        settings_button = true;
        break;
    }
    *vm = BuildVm(s, false);
    check(settings_button && vm->settings_open, "window mouse messages open Settings");
    const auto all = (1u << static_cast<unsigned>(ui::SettingsSection::Count)) - 1;
    check(s.settingsCollapsed == all && s.settingsExpanded == 0, "settings start with all groups collapsed");
    Render(s);
    ui::fluent::Painter painter(&s.compositor);
    auto layout = std::make_unique<ui::SettingsLayout>(ui::MakeSettingsLayout(*vm, bounds,
        s.scale, s.renderer.TitleBarHeight(), 28*s.scale, &painter));
    const auto header = layout->sections.front().header;
    const auto bit = ui::SettingsSectionBit(layout->sections.front().id);
    for (int i = 0; i < 4; ++i) {
        const auto before = s.settingsCollapsed;
        click(header.right - 16*s.scale, (header.top + header.bottom)/2);
        check(s.settingsCollapsed == (before ^ bit), "window mouse messages toggle the section");
        Render(s);
    }
    const auto find = s.renderer.SettingsSearchBounds(bounds.right, bounds.bottom);
    click((find.left + find.right)/2, (find.top + find.bottom)/2);
    const HWND edit = GetFocus();
    wchar_t edit_class[64]{};
    GetClassNameW(edit, edit_class, ARRAYSIZE(edit_class));
    check(_wcsicmp(edit_class, L"EDIT") == 0 && GetParent(edit) == s.hwnd,
          "clicking the middle focuses an editor inside the original settings field");
    check(!s.menu || !s.menu->IsOpen(), "clicking an empty search field does not open a popup");
    const UINT changed = RegisterWindowMessageW(L"Pulse.SettingsSearchChanged");
    SetWindowTextW(edit, L"   ");
    SendMessageW(s.hwnd, changed, 0, 0);
    check(!s.menu || !s.menu->IsOpen(), "whitespace does not open suggestions");
    auto search = [&](const wchar_t* query, bool clear) {
        observation = {};
        observation.state = &s;
        observation.edit = edit;
        observation.clear = clear;
        POINT points[] = {{static_cast<LONG>(find.left), static_cast<LONG>(find.top)},
                          {static_cast<LONG>(find.right), static_cast<LONG>(find.bottom)}};
        MapWindowPoints(s.hwnd, nullptr, points, 2);
        observation.anchor = {points[0].x, points[0].y, points[1].x, points[1].y};
        SetTimer(s.hwnd, 0x5357, 40, [](HWND hwnd, UINT, UINT_PTR id, DWORD) {
            KillTimer(hwnd, id);
            auto& o = observation;
            o.opened = o.state->menu && o.state->menu->IsOpen();
            HWND popup = nullptr;
            EnumThreadWindows(GetCurrentThreadId(), FindPopup, reinterpret_cast<LPARAM>(&popup));
            RECT rect{};
            if (popup && GetWindowRect(popup, &rect)) {
                o.anchored = std::abs(rect.left + ui::FluentMenu::kShadowMargin - o.anchor.left) <= 1 &&
                    std::abs(rect.top + ui::FluentMenu::kShadowMargin - o.anchor.bottom - 4*o.state->scale) <= 1;
                o.compact = rect.bottom - rect.top <= 320*o.state->scale + 2*ui::FluentMenu::kShadowMargin;
            }
            GetClientRect(o.edit, &rect);
            const LPARAM middle = MAKELPARAM((rect.right - rect.left)/2, (rect.bottom - rect.top)/2);
            SendMessageW(o.edit, WM_LBUTTONDOWN, MK_LBUTTON, middle);
            SendMessageW(o.edit, WM_LBUTTONUP, 0, middle);
            o.middle_click = GetFocus() == o.edit && o.state->menu->IsOpen();
            SendMessageW(hwnd, WM_SETCURSOR, reinterpret_cast<WPARAM>(hwnd), MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
            if (o.clear) {
                SetWindowTextW(o.edit, L"");
            } else PostThreadMessageW(GetCurrentThreadId(), WM_KEYDOWN, VK_RETURN, 0);
        });
        SetWindowTextW(edit, query);
        SendMessageW(s.hwnd, changed, 0, 0);
        if (clear) observation.cleared = !s.menu->IsOpen();
        check(observation.opened, "typing a query opens matching suggestions");
        check(observation.anchored, "suggestions align below the original search field");
        check(observation.compact, "broad queries retain a compact results panel");
        check(observation.middle_click, "clicking the input middle keeps focus and suggestions alive");
        if (clear) check(observation.cleared, "clearing input closes suggestions");
    };
    search(l10n::Get(l10n::StringId::SettingsGeneral).c_str(), true);
    check(GetFocus() == edit && IsWindow(s.hwnd), "clearing results keeps the original editor usable");
    search(L"字体", false);
    *vm = BuildVm(s, false);
    check(vm->settings_page == 0 && !(s.settingsCollapsed & bit), "keyboard selection expands its destination");
    check((GetWindowLongPtrW(edit, GWL_STYLE) & WS_VISIBLE) == 0, "selection closes the editor after navigation");
    log << "failures=" << failures << std::endl;
    return failures ? 1 : 0;
}
#endif
