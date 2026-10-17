#include "settings_search_index.h"

#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

namespace pulse::app {
namespace {

std::wstring Compact(std::wstring_view text) {
    std::wstring result;
    result.reserve(text.size());
    for (const wchar_t ch : text) {
        if (!std::iswspace(ch)) result.push_back(static_cast<wchar_t>(std::towlower(ch)));
    }
    return result;
}

std::vector<std::wstring> Tokens(std::wstring_view text) {
    std::vector<std::wstring> tokens;
    size_t start = 0;
    while (start < text.size()) {
        while (start < text.size() && std::iswspace(text[start])) ++start;
        size_t end = start;
        while (end < text.size() && !std::iswspace(text[end])) ++end;
        if (end > start) tokens.push_back(Compact(text.substr(start, end - start)));
        start = end;
    }
    return tokens;
}

bool AsciiWord(std::wstring_view word) {
    return !word.empty() && std::all_of(word.begin(), word.end(), [](wchar_t ch) {
        return ch >= L'a' && ch <= L'z';
    });
}

bool SingleEdit(std::wstring_view query, std::wstring_view word) {
    if (query.size() > word.size() + 1 || word.size() > query.size() + 1) return false;
    size_t first = 0;
    while (first < query.size() && first < word.size() && query[first] == word[first]) ++first;
    if (first == query.size() || first == word.size()) return true;
    if (query.size() == word.size()) {
        if (query.substr(first + 1) == word.substr(first + 1)) return true;
        return first + 1 < query.size() && query[first] == word[first + 1] &&
            query[first + 1] == word[first] && query.substr(first + 2) == word.substr(first + 2);
    }
    return query.size() > word.size() ? query.substr(first + 1) == word.substr(first)
                                    : query.substr(first) == word.substr(first + 1);
}

bool TypoMatch(std::wstring_view query, std::wstring_view text) {
    // Short words and non-Latin text require literal or ordered matches.
    if (query.size() < 5 || !AsciiWord(query)) return false;
    // Preserve word boundaries: compacting spaces would join unrelated aliases.
    std::wstring word;
    for (size_t i = 0; i <= text.size(); ++i) {
        const wchar_t ch = i < text.size() ? static_cast<wchar_t>(std::towlower(text[i])) : L' ';
        if (ch >= L'a' && ch <= L'z') {
            word.push_back(ch);
        } else if (!word.empty()) {
            if (word.size() >= 5 && SingleEdit(query, word)) return true;
            word.clear();
        }
    }
    return false;
}

bool OrderedMatch(std::wstring_view query, std::wstring_view text) {
    if (query.size() < (AsciiWord(query) ? 4u : 2u)) return false;
    for (size_t start = text.find(query.front()); start != std::wstring_view::npos;
         start = text.find(query.front(), start + 1)) {
        size_t pos = start + 1;
        size_t matched = 1;
        while (matched < query.size() && pos < text.size()) {
            if (text[pos++] == query[matched]) ++matched;
        }
        if (matched == query.size() && pos - start <= query.size() * 2 + 2) return true;
    }
    return false;
}

int TokenScore(std::wstring_view query, std::wstring_view title,
               std::wstring_view page, std::wstring_view aliases,
               std::wstring_view original_title, std::wstring_view original_page,
               std::wstring_view original_aliases) {
    if (title == query) return 1000;
    if (title.find(query) != std::wstring_view::npos) return 900;
    if (aliases.find(query) != std::wstring_view::npos) return 700;
    if (page.find(query) != std::wstring_view::npos) return 500;
    if (OrderedMatch(query, title)) return 300;
    if (OrderedMatch(query, aliases)) return 250;
    if (TypoMatch(query, original_title)) return 200;
    if (TypoMatch(query, original_aliases)) return 150;
    if (TypoMatch(query, original_page)) return 100;
    return -1;
}

} // namespace

int SettingsSearchScore(std::wstring_view query, std::wstring_view title,
                       std::wstring_view page, std::wstring_view aliases) {
    const auto needle = Compact(query);
    if (needle.empty()) return 0;
    const auto title_key = Compact(title);
    const auto page_key = Compact(page);
    const auto alias_key = Compact(aliases);
    int score = TokenScore(needle, title_key, page_key, alias_key, title, page, aliases);
    const auto tokens = Tokens(query);
    if (tokens.size() > 1) {
        int weakest = 1000;
        for (const auto& token : tokens) {
            const int part = TokenScore(token, title_key, page_key, alias_key, title, page, aliases);
            if (part < 0) return score;
            weakest = (std::min)(weakest, part);
        }
        score = (std::max)(score, weakest - 25);
    }
    return score;
}

std::wstring_view SettingsSearchAliases(l10n::StringId title) {
    using I = l10n::StringId;
    switch (title) {
    case I::SettingsUiFontSize: return L"字体 字體 字号 字號 文字大小 界面缩放 font text size typography scaling";
    case I::SettingsTextRender: return L"字体 字體 文字 抗锯齿 清晰度 font rendering antialias cleartype";
    case I::SettingsTheme: return L"主题 主題 外观 深色 浅色 暗色 夜间 亮色 theme appearance dark light mode";
    case I::SettingsThemeColor: return L"主题颜色 主題顏色 强调色 配色 accent theme color colour";
    case I::SettingsWindowEffect: return L"透明 玻璃 磨砂 模糊 云母 亚克力 背景 效果 transparent blur mica acrylic backdrop material";
    case I::MenuAcrylic: return L"菜单 菜單 透明 模糊 亚克力 menu acrylic transparency blur";
    case I::SettingsLanguage: return L"语言 語言 中文 英文 简体 繁体 language locale chinese english";
    case I::SettingsWallpaper: return L"壁纸 壁紙 背景 图片 图片背景 wallpaper background image";
    case I::SettingsWallpaperLook: return L"壁纸 透明 明暗 亮度 不透明 wallpaper opacity brightness appearance";
    case I::SettingsWallpaperBlur: return L"壁纸 背景 模糊 磨砂 wallpaper background blur";
    case I::SettingsLaunch: return L"开机启动 开机自启 启动项 登录 launch startup autostart login";
    case I::SettingsStartInTray: return L"启动托盘 启动最小化 后台启动 startup tray minimized background";
    case I::SettingsKeepRunning: return L"后台 关闭 托盘 常驻 keep running background close tray";
    case I::SettingsNotifyIcon: return L"托盘 通知区域 图标 tray notification icon";
    case I::SettingsTrayIcon: return L"托盘 图标 大小 tray icon size";
    case I::SettingsShowHidden: return L"隐藏文件 隱藏檔案 隐藏文件夹 hidden files folders visibility";
    case I::SettingsShowProtected: return L"系统文件 受保护 隐藏 protected system hidden files";
    case I::SettingsHomeFolder: return L"主页 主頁 默认目录 默认文件夹 home folder directory";
    case I::SettingsStartupOpen: return L"启动 打开目录 恢复会话 startup open restore session";
    case I::SettingsNewTabOpen: return L"新建标签 新标签页 默认目录 new tab folder";
    case I::SettingsExternalOpen: return L"外部打开 新窗口 复用 external open window reuse";
    case I::SettingsCloseLastTab: return L"关闭 最后标签 退出 close last tab exit";
    case I::SettingsCloseTabDoubleClick: return L"双击 关闭标签 double click close tab";
    case I::SettingsRowHeight: return L"行高 密度 紧凑 间距 row height density spacing compact";
    case I::SettingsFolderSort: return L"排序 文件夹优先 sort folders first order";
    case I::SettingsConfirmDelete: return L"删除 刪除 确认 回收站 delete confirmation recycle";
    case I::SettingsIntegration:
    case I::SettingsDefaultManager: return L"系统集成 默认文件管理器 替代资源管理器 integration default file manager explorer";
    case I::SettingsOpenFolders: return L"打开文件夹 接管 资源管理器 open folders takeover explorer";
    case I::SettingsWinE: return L"快捷键 快捷鍵 热键 熱鍵 win e windows explorer hotkey shortcut";
    case I::SettingsThisPc: return L"此电脑 计算机 this pc computer";
    case I::SettingsRecycleBin: return L"回收站 删除文件 recycle bin trash";
    case I::SettingsExplorerWindows: return L"资源管理器 窗口 接管 explorer windows takeover";
    case I::SettingsShellTags: return L"标签 標籤 资源管理器 shell tags explorer";
    case I::SettingsVerticalTabs: return L"垂直标签 竖向标签 侧边标签 vertical tabs sidebar";
    case I::PinnedNames: return L"固定 名称 重命名 pinned names rename";
    case I::SettingsHints: return L"提示 引导 帮助 tips hints help onboarding";
    case I::SettingsHintsReset: return L"提示 重置 引导 reset hints tips onboarding";
    case I::SettingsBlankClickBack: return L"空白 单击 双击 返回 blank click back";
    case I::SettingsChangeTracking: return L"变更跟踪 文件变化 更改 标记 change tracking modified";
    case I::GlobalSearch: return L"全局搜索 全局搜尋 搜索文件 查找 global search find files";
    case I::GlobalSearchHotkey: return L"全局搜索 快捷键 快捷鍵 热键 熱鍵 keyboard hotkey shortcut search";
    case I::SearchPinyin: return L"拼音 首字母 中文 搜索 pinyin initials chinese search";
    case I::ContentIndexManage: return L"正文 全文 内容索引 索引管理 搜索 文本 content full text index search";
    case I::IndexLocation: return L"索引 位置 缓存 存储 路径 index location cache storage path";
    case I::LocalDrives: return L"索引 磁盘 硬盘 驱动器 范围 index disk drive volume scope";
    case I::Exclusions: return L"索引 排除 忽略 文件夹 范围 index exclude ignore folders scope";
    case I::ServerFolders: return L"索引 网络 共享 服务器 文件夹 nas unc index network shared server folders";
    case I::QuickPreview: return L"快速预览 快速預覽 预览 预览增强包 支持的格式 系统扩展 现代图像格式 RAW 相机照片 压缩包 空格 quick preview quicklook preview packs supported formats system extensions modern image formats raw camera photos archives space";
    case I::SettingsContextMenu: return L"右键菜单 右鍵選單 上下文 菜单项 context menu right click shell";
    case I::SettingsDuplicates: return L"重复文件 重複檔案 去重 清理 duplicates duplicate files cleanup";
    case I::SettingsAboutDiagnostics: return L"关于 关于版本 版本信息 about version information";
    case I::Diagnostics: return L"诊断 診斷 日志 日誌 性能 diagnostics logs performance";
    case I::SettingsShowPerformance: return L"性能 帧率 内存 状态栏 performance fps memory status";
    case I::SettingsAutoUpdate: return L"自动更新 自動更新 升级 检查更新 update upgrade automatic check";
    case I::ListSmartDate: return L"日期 时间 智能 相对 date time smart relative";
    case I::ListZebraRows: return L"斑马纹 交替 行背景 zebra alternating rows";
    case I::ListSizeBar: return L"大小 容量 条形 size bar capacity";
    case I::ListTagNameColor: return L"标签 颜色 文件名 tag name color colour";
    case I::ListSelectionOutline: return L"选中 轮廓 边框 selection outline border";
    case I::ListThumbnailBadges: return L"缩略图 缩略图标 徽标 thumbnail badges icons";
    default: return {};
    }
}

} // namespace pulse::app
