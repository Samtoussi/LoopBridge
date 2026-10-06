#pragma once

#include "LoopItem.h"
#include <set>
#include <vector>

namespace LibraryFilter
{
inline juce::String producer(const LoopItem& item)
{
    if (item.senderName.trim().isNotEmpty()) return item.senderName.trim();
    return item.senderEmail.upToFirstOccurrenceOf("@", false, false).trim();
}

inline juce::String normalizeProducer(const juce::String& name)
{
    return name.trim().toLowerCase();
}

inline std::vector<int> visibleItems(const std::vector<LoopItem>& items,
                                    const std::set<juce::String>& favorites,
                                    bool favoritesOnly, const juce::String& search)
{
    std::vector<int> result;
    const auto query = search.trim();
    for (int i = 0; i < static_cast<int>(items.size()); ++i)
    {
        const auto& item = items[static_cast<size_t>(i)];
        const auto sender = producer(item);
        if (favoritesOnly && favorites.count(normalizeProducer(sender)) == 0) continue;
        if (query.isNotEmpty() && !item.filename.containsIgnoreCase(query)
            && !sender.containsIgnoreCase(query) && !item.senderEmail.containsIgnoreCase(query)) continue;
        result.push_back(i);
    }
    return result;
}

inline int underlyingIndex(const std::vector<int>& visible, int row)
{
    return row >= 0 && row < static_cast<int>(visible.size()) ? visible[static_cast<size_t>(row)] : -1;
}
}
