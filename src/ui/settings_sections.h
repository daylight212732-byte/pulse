#pragma once
#include <cstdint>

namespace pulse::ui {
enum class SettingsSection : unsigned {
    Appearance, Integration, Startup, FileList, FilenameSearch, ContentSearch,
    PulseMenu, About, Apps, Diagnostics, Updates, Releases, DuplicateOptions,
    PreviewTrigger, PreviewSystem, MediaPack, ImagePacks, ArchivePack, PreviewAdvanced,
    Count
};
constexpr uint32_t SettingsSectionBit(SettingsSection section) {
    return 1u << static_cast<unsigned>(section);
}
}
