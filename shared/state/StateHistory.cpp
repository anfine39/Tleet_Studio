#include "StateHistory.h"

namespace tleet
{
    bool StateHistory::commit (const ParamSnapshot& s)
    {
        if (! entries.empty() && entries[index].approximatelyEquals (s))
            return false;

        if (! entries.empty())
            entries.erase (entries.begin() + (std::ptrdiff_t) index + 1, entries.end());

        entries.push_back (s);

        if (entries.size() > limit)
            entries.erase (entries.begin());

        index = entries.size() - 1;
        return true;
    }

    void StateHistory::resetTo (const ParamSnapshot& s)
    {
        entries.clear();
        entries.push_back (s);
        index = 0;
    }

    const ParamSnapshot* StateHistory::undo()
    {
        if (! canUndo())
            return nullptr;
        return &entries[--index];
    }

    const ParamSnapshot* StateHistory::redo()
    {
        if (! canRedo())
            return nullptr;
        return &entries[++index];
    }
}
