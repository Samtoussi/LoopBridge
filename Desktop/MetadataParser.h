#pragma once

#include <juce_core/juce_core.h>

#include "LoopItem.h"

class MetadataParser
{
public:
    static void parse(LoopItem& item);

    static std::optional<MusicalKey> musicalKeyFromLabel(const juce::String& label);
    static int previewSemitones(const std::optional<MusicalKey>& source,
                               MusicalKey target, bool syncEnabled, int manual);

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
