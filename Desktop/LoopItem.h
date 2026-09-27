#pragma once

#include <juce_core/juce_core.h>

#include <optional>

enum class MetadataSource
{
    unknown,
    provided,
    detected
};

struct LoopItem
{
    // Gmail identity
    juce::String messageId;
    juce::String attachmentId;

    // Original attachment
    juce::String filename;

    // Email metadata
    juce::String senderName;
    juce::String senderEmail;
    juce::String subject;
    juce::String category;

    juce::Time receivedAt;

    // Musical metadata
    std::optional<double> bpm;
    juce::String key;

    MetadataSource bpmSource =
        MetadataSource::unknown;

    MetadataSource keySource =
        MetadataSource::unknown;

    // Filled once the attachment has actually
    // been downloaded/cached.
    juce::File cachedFile;
};