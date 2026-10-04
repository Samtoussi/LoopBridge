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

    item.key.clear();
    item.keySource = MetadataSource::unknown;
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
    item.musicalKey = musicalKeyFromLabel(item.key);
}

std::optional<double>
MetadataParser::parseBpm(
    const juce::String& text)
{
    const std::string input =
        text.toStdString();

    // --------------------------------------------------
    // Explicit BPM
    // --------------------------------------------------
    //
    // Handles:
    //
    // 154bpm
    // 154 BPM
    // 154-bpm
    // BPM 154
    //
    // Explicit BPM always gets priority.

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

        if (bpm >= 40.0
            && bpm <= 300.0)
        {
            return bpm;
        }

        return std::nullopt;
    }

    if (std::regex_search(
            input,
            match,
            bpmBeforeNumber))
    {
        bpm =
            std::stod(
                match[1].str());

        if (bpm >= 40.0
            && bpm <= 300.0)
        {
            return bpm;
        }

        return std::nullopt;
    }

    // --------------------------------------------------
    // Bare BPM
    // --------------------------------------------------
    //
    // Real LoopBridge Gmail filenames often look like:
    //
    // (rylo) sure you are 159 a#m season.mp3
    // (pain) pray for me 167 season.mp3
    // (veeze foxbd) walka 147 em season.mp3
    // (unique) feel 180 amin season.mp3
    // (yb unique) unison 151 season.mp3
    //
    // There is no literal "bpm", so we allow a bare
    // integer only when it looks strongly like tempo:
    //
    // - exactly 2 or 3 digits
    // - standalone numeric token
    // - between 40 and 300
    //
    // We collect all candidates instead of blindly taking
    // the first number. A filename containing more than one
    // plausible bare tempo is considered ambiguous and is
    // rejected.

    static const std::regex
        bareNumber(
            R"((?:^|[^0-9])(\d{2,3})(?=$|[^0-9]))");

    auto begin =
        std::sregex_iterator(
            input.begin(),
            input.end(),
            bareNumber);

    const auto end =
        std::sregex_iterator();

    std::optional<double> candidate;

    for (auto it = begin;
         it != end;
         ++it)
    {
        const auto value =
            std::stod(
                (*it)[1].str());

        if (value < 40.0
            || value > 300.0)
        {
            continue;
        }

        // More than one plausible bare BPM means we do not
        // have enough information to safely choose one.
        if (candidate.has_value())
        {
            return std::nullopt;
        }

        candidate = value;
    }

    return candidate;
}

juce::String
MetadataParser::parseKey(
    const juce::String& text)
{
    const std::string input =
        text.replaceCharacter(0x266f, '#').replaceCharacter(0x266d, 'b').toStdString();

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
        R"((?:^|[^A-Za-z])([A-Ga-g])[\s_-]*(#|b|sharp|flat)?[\s_-]*(major|minor|maj|min|m)(?=$|[^A-Za-z]))",
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

    const auto lowerAccidental = accidental.toLowerCase();
    normalisedRoot += lowerAccidental == "sharp" ? "#"
        : lowerAccidental == "flat" ? "b" : lowerAccidental;

    const auto lowerQuality =
        quality.toLowerCase();

    const bool minor =
        (quality == "m")
        || lowerQuality == "min"
        || lowerQuality == "minor";

    return normalisedRoot
        + (minor
               ? " minor"
               : " major");
}

std::optional<MusicalKey> MetadataParser::musicalKeyFromLabel(const juce::String& label)
{
    if (label.isEmpty())
        return std::nullopt;
    const auto root = juce::String("C D EF G A B").indexOfChar(label[0]);
    if (root < 0)
        return std::nullopt;
    int pitchClass = root;
    if (label.length() > 1 && label[1] == '#') ++pitchClass;
    if (label.length() > 1 && label[1] == 'b') --pitchClass;
    return MusicalKey { (pitchClass + 12) % 12, label.endsWith("minor") };
}

int MetadataParser::previewSemitones(const std::optional<MusicalKey>& source,
                                    MusicalKey target, bool syncEnabled, int manual)
{
    int automatic = 0;
    if (syncEnabled && source && source->minor == target.minor)
    {
        automatic = (target.pitchClass - source->pitchClass + 12) % 12;
        if (automatic > 6) automatic -= 12;
    }
    return juce::jlimit(-12, 12, automatic + manual);
}
