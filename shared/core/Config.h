#pragma once

// Suite-wide constants. Donation and website URLs stay empty until the owner
// fills them in; empty URLs render as disabled buttons in the About panel.
// Nothing here is ever fetched from the network.

namespace tleet::config
{
    inline constexpr const char* companyName   = "TLEET Audio";
    inline constexpr const char* websiteUrl    = "";
    inline constexpr const char* vkDonutUrl    = "";
    inline constexpr const char* boostyUrl     = "";
    inline constexpr const char* sourceCodeUrl = "https://github.com/anfine39/Tleet_Studio";

    // Bump when the saved-state layout changes; add a migration step in
    // TleetProcessorBase::migrateState for every bump.
    inline constexpr int stateSchemaVersion  = 1;
    inline constexpr int presetFormatVersion = 1;
}
