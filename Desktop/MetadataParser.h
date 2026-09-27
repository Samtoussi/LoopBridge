#pragma once

#include <juce_core/juce_core.h>

#include "LoopItem.h"

class MetadataParser
{
public:
    static void parse(LoopItem& item);

private:
    static std::optional<double> parseBpm(
        const juce::String& text);

    static juce::String parseKey(
        const juce::String& text);

    static juce::String normaliseKey(
        const juce::String& root,
        const juce::String& accidental,
        const juce::String& quality);
};