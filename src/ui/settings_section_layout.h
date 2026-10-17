// Included after SettingsLayout and its page layout helpers. Both drawing and
// hit testing consume the same compacted rectangles, including hidden controls.
void ApplySettingsSections(SettingsLayout& l, const WindowViewModel& vm, float scale) {
    using S = SettingsSection;
    using I = l10n::StringId;
    auto add = [&](S id, D2D1_RECT_F header, D2D1_RECT_F body,
                   std::wstring title, bool inset = false) {
        if (header.right <= header.left || body.bottom <= body.top) return;
        l.sections.push_back({id, header, body, std::move(title), inset,
            (vm.settings_collapsed & SettingsSectionBit(id)) != 0});
    };
    auto group = [&](S id, D2D1_RECT_F header, D2D1_RECT_F body, I title) {
        add(id, header, body, l10n::Get(title));
    };
    auto card = [&](S id, D2D1_RECT_F body, I title) {
        auto header = body;
        header.bottom = std::min(header.bottom, header.top + 56*scale);
        add(id, header, body, l10n::Get(title), true);
    };
    if (vm.settings_page == 0) {
        group(S::Appearance, l.section[0], l.group[0], I::SettingsAppearance);
        group(S::Integration, l.integration_section, l.integration_card, I::SettingsIntegration);
        group(S::Startup, l.section[1], l.group[1], I::SettingsStartupShutdown);
        group(S::FileList, l.section[2], l.group[2], I::SettingsFileList);
    } else if (vm.settings_page == 1) {
        auto filenames = l.group[0];
        filenames.bottom = l.section[1].top - 14*scale;
        group(S::FilenameSearch, l.section[0], filenames, I::SearchModeName);
        group(S::ContentSearch, l.section[1], l.group[1], I::SearchModeContent);
    } else if (vm.settings_page == 2) {
        card(S::PulseMenu, l.pulse_card, I::SettingsContextMenu);
        // Leave room beside the chevron for the existing restore action.
        l.pulse_restore.left -= 32*scale;
        l.pulse_restore.right -= 32*scale;
    } else if (vm.settings_page == 3) {
        card(S::About, l.about_card, I::SettingsAboutDiagnostics);
        card(S::Apps, l.apps_card, I::AboutMoreApps);
        card(S::Diagnostics, l.diagnostics_card, I::Diagnostics);
        card(S::Updates, l.update_card, I::Update);
        card(S::Releases, l.release_card, I::ReleaseNotes);
        l.release_all.left -= 32*scale;
        l.release_all.right -= 32*scale;
    } else if (vm.settings_page == 4 && !vm.dup_scanning) {
        // Keep cancel/progress visible while a scan runs.
        group(S::DuplicateOptions, l.section[0], l.duplicate_options, I::SettingsDuplicates);
    } else if (vm.settings_page == 5) {
        auto trigger = l.trigger_row;
        trigger.bottom = l.preview_section.top - 24*scale;
        add(S::PreviewTrigger, l.trigger_section, trigger, pack_text::TriggerSection());
        add(S::PreviewSystem, l.preview_codec_section, l.preview_codec_group, pack_text::SystemTitle());
        auto media = l.pack_card;
        media.bottom = l.pack_source_group.bottom;
        add(S::MediaPack, l.pack_media_section, media, pack_text::Media());
        auto images = l.pack_images_card;
        images.bottom = l.pack_raw_card.bottom;
        add(S::ImagePacks, l.pack_images_section, images, pack_text::Images());
        add(S::ArchivePack, l.pack_archive_section, l.pack_archive_card, pack_text::Archive());
        add(S::PreviewAdvanced, l.pack_advanced_section, l.pack_group, pack_text::Advanced());
    }
    if (l.sections.empty()) return;
    const auto original = l.sections;
    auto map_y = [&](float y) {
        float result = y;
        for (const auto& section : original) {
            if (!section.collapsed) continue;
            const float start = section.inset ? section.body.top + 56*scale : section.body.top;
            result -= std::clamp(y - start, 0.0f, std::max(0.0f, section.body.bottom - start));
        }
        return result;
    };
    auto transform = [&](D2D1_RECT_F& rect) {
        if (rect.right <= rect.left || rect.bottom <= rect.top) return;
        for (const auto& section : original) {
            if (section.collapsed && rect.top >= section.body.top - 0.01f &&
                rect.bottom <= section.body.bottom + 0.01f) {
                // Offscreen (rather than a zero rectangle) also hides content
                // whose drawing computes child positions from its parent card.
                rect.top -= 1000000*scale;
                rect.bottom -= 1000000*scale;
                return;
            }
        }
        rect.top = map_y(rect.top);
        rect.bottom = map_y(rect.bottom);
    };
    auto visit = [&](auto&& self, auto& value) -> void {
        if constexpr (std::is_same_v<std::remove_cvref_t<decltype(value)>, D2D1_RECT_F>) transform(value);
        else for (auto& rect : value) self(self, rect);
    };
    // Explicit content geometry only: navigation, viewport and fixed search
    // remain stationary. Arrays/vectors include every actionable child.
    auto transform_all = [&](auto&... values) { (visit(visit, values), ...); };
    transform_all(
        l.context_rows, l.pulse_card, l.pulse_restore, l.pulse_preset, l.pulse_tabs, l.pulse_tab,
        l.pulse_fixed, l.pulse_preview, l.pulse_order_reset, l.context_system_hint, l.duplicate_options, l.section,
        l.group, l.footer, l.theme_row, l.theme_tile, l.effect_choice, l.language_choice,
        l.performance_row, l.disclosure, l.filename_status, l.integration_card, l.integration_status, l.integration_summary,
        l.integration_hint, l.integration_retry, l.integration_restore, l.integration_section, l.integration_badge, l.integration_bar,
        l.integration_list_head, l.integration_chip, l.integration_cover, l.integration_options_head, l.integration_try, l.integration_hint_text,
        l.trigger_section, l.trigger_row, l.trigger_choice, l.hover_delay_row, l.hover_delay_choice, l.hover_size_row,
        l.hover_size_choice, l.preview_section, l.preview_group, l.preview_formats, l.preview_legend, l.preview_legend_note,
        l.preview_header_title, l.preview_header_summary, l.preview_legend_chip, l.preview_codec_section, l.preview_codec_group, l.preview_codec_hint,
        l.pack_section, l.preview_codec_text, l.preview_codec_badge, l.preview_codec_row, l.preview_codec_button, l.pack_summary,
        l.pack_open, l.pack_media_section, l.pack_card, l.pack_badge, l.pack_enable, l.pack_primary,
        l.pack_update, l.pack_notice, l.pack_advanced_section, l.pack_custom_row, l.pack_path_row, l.pack_detect,
        l.pack_browse, l.pack_remove_row, l.pack_group, l.pack_note, l.pack_source_group, l.pack_source_status,
        l.pack_auto_update_row, l.pack_images_section, l.pack_images_card, l.pack_images_badge, l.pack_images_enable, l.pack_images_primary,
        l.pack_images_update, l.pack_images_notice, l.pack_raw_card, l.pack_raw_badge, l.pack_raw_enable, l.pack_raw_primary,
        l.pack_raw_update, l.pack_raw_notice, l.pack_archive_section, l.pack_archive_card, l.pack_archive_badge, l.pack_archive_enable,
        l.pack_archive_primary, l.pack_archive_update, l.pack_archive_notice, l.content_header, l.content_types, l.content_pause,
        l.content_options, l.content_rebuild, l.content_empty, l.ctx_intro, l.ctx_layer, l.accent_card,
        l.accent_picker, l.effect_card, l.effect_row, l.menu_acrylic_row, l.density_card, l.list_style_row,
        l.density_row, l.folder_sort_card, l.folder_sort_row, l.home_folder_card, l.home_folder_choose, l.home_folder_reset,
        l.startup_open_card, l.startup_open_row, l.notify_icon_card, l.notify_icon_row, l.new_tab_open_card, l.new_tab_open_row,
        l.external_open_card, l.external_open_row, l.close_last_tab_row, l.close_tab_double_click_row, l.confirm_delete_row, l.start_in_tray_row,
        l.text_render_card, l.text_render_row, l.ui_font_size_card, l.ui_font_size_row, l.tray_icon_card, l.tray_icon_row,
        l.language_card, l.language_segment, l.wallpaper_card, l.wallpaper_preview, l.wallpaper_choose, l.wallpaper_clear,
        l.wallpaper_look_card, l.wallpaper_look_row, l.wallpaper_blur_card, l.wallpaper_blur_row, l.startup_row, l.hidden_files_row,
        l.protected_files_row, l.pinned_names_row, l.vertical_tabs_row, l.hints_row, l.hints_reset_row, l.hints_reset_button,
        l.blank_click_row, l.blank_click_choice, l.win_e_row, l.this_pc_row, l.recycle_bin_row, l.explorer_windows_row,
        l.default_manager_row, l.shell_tags_row, l.change_tracking_row, l.change_days_row, l.change_days, l.search_pinyin_row,
        l.global_search_row, l.global_search_hotkey_row, l.global_search_hotkey_button, l.content_index_row, l.index_info, l.index_status,
        l.index_path, l.index_action, l.index_volume_rows, l.index_exclude_action, l.index_exclude_empty, l.index_system_card,
        l.index_system_row, l.index_system_more, l.index_system_chip, l.index_system_group, l.index_exclude_rows, l.index_exclude_remove,
        l.network_action, l.network_rows, l.network_remove, l.about_card, l.apps_card, l.apps_row,
        l.diagnostics_card, l.diagnostics_perf, l.diagnostics_action, l.update_card, l.update_action, l.update_auto_row,
        l.about_action, l.release_card, l.release_all, l.release_rows, l.release_body, l.dup_scope,
        l.dup_browse, l.dup_scan, l.dup_cancel, l.dup_min_size, l.dup_drives, l.dup_progress,
        l.dup_delete_all, l.dup_group_cards, l.dup_keep, l.dup_open, l.dup_group_delete);
    for (auto& section : l.sections) {
        section.header.top = map_y(section.header.top);
        section.header.bottom = map_y(section.header.bottom);
    }
    l.content_h = std::max(0.0f, map_y(l.content_origin + l.content_h) - l.content_origin);
}
