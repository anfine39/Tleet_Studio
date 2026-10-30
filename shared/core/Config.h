#pragma once

// Suite-wide constants. Donation and website URLs stay empty until the owner
// fills them in; empty URLs render as disabled buttons in the About panel.
// Nothing here is ever fetched from the network.

namespace tleet::config
{
    // Brand: РИЛАЙ (Realie Studio). The Cyrillic name is spelled with UTF-8
    // escapes so every compiler reads it the same; wrap it in
    // juce::CharPointer_UTF8 before use.
    inline constexpr const char* brandNameUtf8 = "\xd0\xa0\xd0\x98\xd0\x9b\xd0\x90\xd0\x99";
    inline constexpr const char* brandNameLatin = "Realie";
    inline constexpr const char* companyName   = "Realie Studio Plugins";
    // Folder name used for user presets before the rebrand; read once to
    // carry old presets over.
    inline constexpr const char* legacyCompanyName = "TLEET Audio";
    inline constexpr const char* websiteUrl    = "";
    inline constexpr const char* vkDonutUrl    = "";
    inline constexpr const char* boostyUrl     = "";
    inline constexpr const char* sourceCodeUrl = "https://github.com/anfine39/Tleet_Studio";

    // Bump when the saved-state layout changes; add a migration step in
    // TleetProcessorBase::migrateState for every bump.
    inline constexpr int stateSchemaVersion  = 1;
    inline constexpr int presetFormatVersion = 1;
}
