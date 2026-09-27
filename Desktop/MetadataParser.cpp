#include "MetadataParser.h"

#include <regex>
#include <string>

void MetadataParser::parse(
    LoopItem& item)
{
    // Filename gets priority because this is normally
    // metadata attached directly to the loop itself.
    //
    // Subject is only used as fallback.

    if (const auto filenameBpm =
            parseBpm(item.filename))
    {
        item.bpm = filenameBpm;
        item.bpmSource =
            MetadataSource::provided;
    }
    else if (const auto subjectBpm =
                 parseBpm(item.subject))
    {
        item.bpm = subjectBpm;
        item.bpmSource =
            MetadataSource::provided;
    }

    const auto filenameKey =
        parseKey(item.filename);

    if (filenameKey.isNotEmpty())
    {
        item.key = filenameKey;
        item.keySource =
            MetadataSource::provided;
    }
    else
    {
        const auto subjectKey =
            parseKey(item.subject);

        if (subjectKey.isNotEmpty())
        {
            item.key = subjectKey;
            item.keySource =
                MetadataSource::provided;
        }
    }
}

std::optional<double>
MetadataParser::parseBpm(
    const juce::String& text)
{
    const std::string input =
        text.toStdString();

    // Handles:
    //
    // 154bpm
    // 154 BPM
    // 154-bpm
    // BPM 154
    //
    // We intentionally require "bpm" here.
    // Bare numbers will be handled later only if
    // we have a strong reason to support them.

    static const std::regex
        numberBeforeBpm(
            R"((\d{2,4}(?:\.\d+)?)\s*[-_ ]*\s*bpm\b)",
            std::regex_constants::icase);

    static const std::regex
        bpmBeforeNumber(
            R"(\bbpm\s*[-_: ]*\s*(\d{2,4}(?:\.\d+)?))",
            std::regex_constants::icase);

    std::smatch match;

    double bpm = 0.0;

    if (std::regex_search(
            input,
            match,
            numberBeforeBpm))
    {
        bpm =
            std::stod(
                match[1].str());
    }
    else if (std::regex_search(
                 input,
                 match,
                 bpmBeforeNumber))
    {
        bpm =
            std::stod(
                match[1].str());
    }
    else
    {
        return std::nullopt;
    }

    // Sanity guard.
    //
    // This deliberately rejects things such as the
    // real-world "1348BPM" typo we saw.
    //
    // 40-300 is broad enough that we're not making
    // genre assumptions here.

    if (bpm < 40.0
        || bpm > 300.0)
    {
        return std::nullopt;
    }

    return bpm;
}

juce::String
MetadataParser::parseKey(
    const juce::String& text)
{
    const std::string input =
        text.toStdString();

    // Supported examples:
    //
    // C#min
    // C# min
    // C#m
    // C# minor
    //
    // Fmin
    // F min
    // Fm
    //
    // [Cm]
    // (F# min)
    //
    // A# major
    // A#maj
    //
    // We use boundaries around the musical token so
    // random letters inside filenames don't become keys.

    static const std::regex keyPattern(
        R"((?:^|[^A-Za-z])([A-Ga-g])([#b]?)[\s_-]*(major|minor|maj|min|m)(?=$|[^A-Za-z]))",
        std::regex_constants::icase);

    std::smatch match;

    if (!std::regex_search(
            input,
            match,
            keyPattern))
    {
        return {};
    }

    return normaliseKey(
        juce::String(match[1].str()),
        juce::String(match[2].str()),
        juce::String(match[3].str()));
}

juce::String
MetadataParser::normaliseKey(
    const juce::String& root,
    const juce::String& accidental,
    const juce::String& quality)
{
    if (root.isEmpty())
        return {};

    juce::String normalisedRoot =
        root.toUpperCase();

    normalisedRoot +=
        accidental;

    const auto lowerQuality =
        quality.toLowerCase();

    const bool minor =
        lowerQuality == "m"
        || lowerQuality == "min"
        || lowerQuality == "minor";

    return normalisedRoot
        + (minor
               ? " minor"
               : " major");
}