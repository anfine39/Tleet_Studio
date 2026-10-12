#pragma once

#include "PluginState.h"
#include <vector>

namespace tleet
{
    // Snapshot-based undo/redo. Every committed entry is a full parameter
    // snapshot, so undoing a preset load or an A/B switch is as reliable as
    // undoing a single knob move. Message thread only.
    class StateHistory
    {
    public:
        explicit StateHistory (size_t maxEntries = 100) : limit (maxEntries) {}

        // Records `s` as the newest state if it differs from the current one.
        // Returns true when an entry was added.
        bool commit (const ParamSnapshot& s);

        // Starts a fresh history from `s` (after project load).
        void resetTo (const ParamSnapshot& s);

        bool canUndo() const noexcept { return index > 0; }
        bool canRedo() const noexcept { return index + 1 < entries.size(); }

        const ParamSnapshot* undo();
        const ParamSnapshot* redo();

    private:
        std::vector<ParamSnapshot> entries;
        size_t index = 0, limit;
    };
}
