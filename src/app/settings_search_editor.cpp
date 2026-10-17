#include "settings_search_editor.h"
#include "app_internal.h"
#include "app_hosted_edit.h"
#include "../common/localization.h"
#include "../ui/edit_host.h"
#include <algorithm>
#include <cmath>
#include <cwctype>
#include <windowsx.h>

namespace pulse {
namespace {
constexpr wchar_t kEditorProperty[] = L"Pulse.SettingsSearchEditor";
constexpr UINT_PTR kEditorSubclass = 0x535345;
const UINT kSearchChanged = RegisterWindowMessageW(L"Pulse.SettingsSearchChanged");

struct SearchEditor {
    AppState* state = nullptr;
    HWND edit = nullptr;
    SettingsSearchResults results;
    bool active = false;
    bool composing = false;
    bool pending = false;
    bool cleared = false;
    RECT anchor{};
};

SearchEditor* Editor(HWND owner) {
    return reinterpret_cast<SearchEditor*>(GetPropW(owner, kEditorProperty));
}

bool Shown(HWND edit) {
    return (GetWindowLongPtrW(edit, GWL_STYLE) & WS_VISIBLE) != 0;
}

std::wstring Query(HWND edit) {
    const int length = GetWindowTextLengthW(edit);
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    text.resize(GetWindowTextW(edit, text.data(), length + 1));
    return text;
}

bool HasQuery(const std::wstring& text) {
    return std::any_of(text.begin(), text.end(), [](wchar_t ch) { return std::iswspace(ch) == 0; });
}

void Hide(SearchEditor& editor) {
    if (editor.active && editor.state->menu) editor.state->menu->Dismiss();
    ShowWindow(editor.edit, SW_HIDE);
    if (GetFocus() == editor.edit) SetFocus(editor.state->hwnd);
}

void QueueResults(SearchEditor& editor) {
    if (editor.pending || editor.composing) return;
    editor.pending = true;
    PostMessageW(editor.state->hwnd, kSearchChanged, 0, 0);
}

LRESULT CALLBACK EditProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam,
                         UINT_PTR, DWORD_PTR data) {
    if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, EditProc, 1);
        return DefSubclassProc(hwnd, msg, wparam, lparam);
    }
    auto& s = *reinterpret_cast<AppState*>(data);
    auto* editor = Editor(s.hwnd);
    if (!editor) return DefSubclassProc(hwnd, msg, wparam, lparam);
    if (msg == WM_IME_STARTCOMPOSITION) editor->composing = true;
    if (msg == WM_IME_ENDCOMPOSITION) editor->composing = false;
    if (msg == WM_KEYDOWN && wparam == VK_ESCAPE && !editor->active) {
        Hide(*editor);
        SetFocus(s.hwnd);
        return 0;
    }
    if (msg == WM_CHAR && (wparam == VK_RETURN || wparam == VK_ESCAPE)) return 0;
    LRESULT result = 0;
    if (!HandleHostedEditMessage(s, hwnd, msg, wparam, lparam, result)) {
        result = ui::DefPresentedChildEditProc(s.compositor, HostedEditFormat(s, hwnd),
            HostedEditForeground(s), HostedEditBackground(s), hwnd, msg, wparam, lparam);
    }
    if (msg == WM_IME_ENDCOMPOSITION) QueueResults(*editor);
    return result;
}

LRESULT CALLBACK OwnerProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam,
                          UINT_PTR, DWORD_PTR data) {
    auto* owner = reinterpret_cast<std::shared_ptr<SearchEditor>*>(data);
    const auto keep_alive = *owner;
    auto* editor = keep_alive.get();
    if (msg == WM_NCDESTROY) {
        RemovePropW(hwnd, kEditorProperty);
        RemoveWindowSubclass(hwnd, OwnerProc, kEditorSubclass);
        delete owner;
        return DefSubclassProc(hwnd, msg, wparam, lparam);
    }
    if (msg == WM_COMMAND && reinterpret_cast<HWND>(lparam) == editor->edit && HIWORD(wparam) == EN_CHANGE) {
        if (editor->active && !HasQuery(Query(editor->edit))) editor->cleared = true;
        QueueResults(*editor);
        return 0;
    }
    if (msg == kSearchChanged) {
        editor->pending = false;
        if (editor->composing || !Shown(editor->edit)) return 0;
        const auto query = Query(editor->edit);
        if (editor->active) {
            if (!HasQuery(query) && editor->state->menu) editor->state->menu->Dismiss();
            return 0;
        }
        if (!HasQuery(query) || GetFocus() != editor->edit) return 0;
        editor->active = true;
        editor->cleared = false;
        const bool selected = editor->results(query, editor->edit);
        editor->active = false;
        if (!IsWindow(hwnd)) return 0;
        if (selected) {
            SetWindowTextW(editor->edit, L"");
            Hide(*editor);
            SetFocus(hwnd);
        } else if (editor->cleared && Shown(editor->edit) && HasQuery(Query(editor->edit))) {
            QueueResults(*editor);
        }
        return 0;
    }
    if (msg == WM_CLOSE && editor->active) {
        // Let the modal loop return before WM_DESTROY releases its shared menu.
        if (editor->state->menu) editor->state->menu->Dismiss();
        SetTimer(hwnd, kEditorSubclass, 100, nullptr);
        return 0;
    }
    if (msg == WM_TIMER && wparam == kEditorSubclass) {
        KillTimer(hwnd, kEditorSubclass);
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
        return 0;
    }
    if (msg == WM_DESTROY && IsWindow(editor->edit)) DestroyWindow(editor->edit);
    if (msg == WM_LBUTTONDOWN && Shown(editor->edit)) {
        const auto box = editor->state->renderer.SettingsSearchBounds(
            static_cast<float>(editor->state->compositor.Width()), static_cast<float>(editor->state->compositor.Height()));
        const float x = static_cast<float>(GET_X_LPARAM(lparam)), y = static_cast<float>(GET_Y_LPARAM(lparam));
        if (x >= box.left && x < box.right && y >= box.top && y < box.bottom) {
            SetFocus(editor->edit);
            return 0;
        }
        Hide(*editor);
    }
    return DefSubclassProc(hwnd, msg, wparam, lparam);
}
}

void LayoutSettingsSearchEditor(AppState& s) {
    auto* editor = Editor(s.hwnd);
    if (!editor || !Shown(editor->edit)) return;
    if (!IsSettingsTab(ActiveTab(s))) { Hide(*editor); return; }
    EnsureEditVisuals(s);
    if (reinterpret_cast<HFONT>(SendMessageW(editor->edit, WM_GETFONT, 0, 0)) != s.editFont)
        SendMessageW(editor->edit, WM_SETFONT, reinterpret_cast<WPARAM>(s.editFont), FALSE);
    auto box = s.renderer.SettingsSearchBounds(static_cast<float>(s.compositor.Width()),
        static_cast<float>(s.compositor.Height()));
    POINT corners[] = {{static_cast<LONG>(std::lround(box.left)), static_cast<LONG>(std::lround(box.top))},
                       {static_cast<LONG>(std::lround(box.right)), static_cast<LONG>(std::lround(box.bottom))}};
    MapWindowPoints(s.hwnd, nullptr, corners, 2);
    const RECT anchor{corners[0].x, corners[0].y, corners[1].x, corners[1].y};
    if (!EqualRect(&editor->anchor, &anchor)) {
        editor->anchor = anchor;
        if (editor->active && s.menu) {
            s.menu->SetAnchorRect(anchor);
            s.menu->SetFilterMinWidth((box.right - box.left)/s.scale);
            s.menu->RequestFilterRefresh();
        }
    }
    // Match DrawTextField's leading icon and persistent shortcut reservation.
    ui::fluent::Painter metrics(&s.compositor);
    metrics.SetScale(s.scale);
    box.left += 34*s.scale;
    box.right -= metrics.MeasureBadgeWidth(L"Ctrl+F") + 16*s.scale;
    PlaceHostedEdit(editor->edit, s.hwnd, box, s.scale, 0, 0);
}

void FocusSettingsSearchEditor(AppState& s, SettingsSearchResults results) {
    auto* editor = Editor(s.hwnd);
    if (editor && editor->active) { SetFocus(editor->edit); return; }
    if (!editor) {
        auto created = std::make_shared<SearchEditor>();
        created->state = &s;
        created->edit = CreateHostedEdit(s, EditProc);
        if (!created->edit) return;
        auto owner = std::make_unique<std::shared_ptr<SearchEditor>>(created);
        if (!SetPropW(s.hwnd, kEditorProperty, created.get()) ||
            !SetWindowSubclass(s.hwnd, OwnerProc, kEditorSubclass, reinterpret_cast<DWORD_PTR>(owner.get()))) {
            RemovePropW(s.hwnd, kEditorProperty);
            DestroyWindow(created->edit);
            return;
        }
        editor = created.get();
        owner.release();
    }
    editor->results = std::move(results);
    SendMessageW(editor->edit, EM_SETCUEBANNER, TRUE,
        reinterpret_cast<LPARAM>(l10n::Get(l10n::StringId::SettingsFind).c_str()));
    ShowWindow(editor->edit, SW_SHOW);
    LayoutSettingsSearchEditor(s);
    SetFocus(editor->edit);
    if (HasQuery(Query(editor->edit))) QueueResults(*editor);
}
}
