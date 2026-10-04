#include "MetadataParser.h"

#include <cmath>
#include <iostream>
#include <vector>

struct TestCase
{
    juce::String filename;

    std::optional<double> expectedBpm;
    juce::String expectedKey;
};

static bool bpmMatches(
    const std::optional<double>& actual,
    const std::optional<double>& expected)
{
    if (!actual.has_value()
        && !expected.has_value())
    {
        return true;
    }

    if (actual.has_value()
        != expected.has_value())
    {
        return false;
    }

    return std::abs(
               actual.value()
               - expected.value())
        < 0.001;
}

int main()
{
    const std::vector<TestCase> tests =
    {
        // --------------------------------------------------
        // Explicit keys, accidental spellings and boundary guards.
        { "loop Bbmaj.wav", std::nullopt, "Bb major" },
        { "loop eb_min.wav", std::nullopt, "Eb minor" },
        { juce::String::fromUTF8("loop C\xe2\x99\xaf minor.wav"), std::nullopt, "C# minor" },
        { juce::String::fromUTF8("loop G\xe2\x99\xad major.wav"), std::nullopt, "Gb major" },
        { "loop C sharp minor.wav", std::nullopt, "C# minor" },
        { "loop D-flat-maj.wav", std::nullopt, "Db major" },
        { "loop CM.wav", std::nullopt, "C major" },
        { "loop cm.wav", std::nullopt, "C minor" },
        { "loop F#.wav", std::nullopt, "" },
        { "warm_chamber.wav", std::nullopt, "" },

        // Existing explicit-BPM cases
        // --------------------------------------------------

        {
            "Past Thoughts 154bpm.wav",
            154.0,
            ""
        },
        {
            "Why did u leave Emin 78bpm.wav",
            78.0,
            "E minor"
        },
        {
            "reload C#min 131bpm.wav",
            131.0,
            "C# minor"
        },
        {
            "PROSTHETIC [Cm] 147BPM.wav",
            147.0,
            "C minor"
        },
        {
            "Raven (F# min) 118 BPM.wav",
            118.0,
            "F# minor"
        },
        {
            "bulletproof D#minor 144bpm.wav",
            144.0,
            "D# minor"
        },
        {
            "aurora C#min 135bpm.wav",
            135.0,
            "C# minor"
        },
        {
            "Sample 11 Gmin 141bpm.wav",
            141.0,
            "G minor"
        },
        {
            "Sample 1 A#min 138bpm.wav",
            138.0,
            "A# minor"
        },
        {
            "something Am 146 BPM.wav",
            146.0,
            "A minor"
        },

        // --------------------------------------------------
        // Real Gmail filename patterns
        // --------------------------------------------------

        {
            "(rylo) sure you are 159 a#m season.mp3",
            159.0,
            "A# minor"
        },
        {
            "(pain) pray for me 167 season.mp3",
            167.0,
            ""
        },
        {
            "(veeze foxbd) walka 147 em season.mp3",
            147.0,
            "E minor"
        },
        {
            "(unique) feel 180 amin season.mp3",
            180.0,
            "A minor"
        },
        {
            "(yb unique) unison 151 season.mp3",
            151.0,
            ""
        },

        // --------------------------------------------------
        // Guardrails
        // --------------------------------------------------

        // Explicit BPM typo / invalid value.
        {
            "KAYN GREY 1348BPM.mp3",
            std::nullopt,
            ""
        },

        // Bare number below plausible BPM range.
        {
            "Sample 11 Gmin.wav",
            std::nullopt,
            "G minor"
        },

        // Bare number above plausible BPM range.
        {
            "loop version 350.wav",
            std::nullopt,
            ""
        },

        // Multiple plausible bare numbers are ambiguous.
        {
            "loop 120 version 140.wav",
            std::nullopt,
            ""
        },

        // No metadata at all.
        {
            "cool melody idea.wav",
            std::nullopt,
            ""
        }
    };

    int passed = 0;
    int failed = 0;

    std::cout
        << "\nLoopBridge Metadata Parser Tests\n"
        << "================================\n\n";

    for (const auto& test : tests)
    {
        LoopItem item;

        item.filename =
            test.filename;

        MetadataParser::parse(item);

        const bool bpmOk =
            bpmMatches(
                item.bpm,
                test.expectedBpm);

        const bool keyOk =
            item.key
            == test.expectedKey;

        const bool success =
            bpmOk && keyOk;

        if (success)
            ++passed;
        else
            ++failed;

        std::cout
            << (success
                    ? "[PASS] "
                    : "[FAIL] ")
            << test.filename
            << "\n";

        std::cout
            << "       BPM: ";

        if (item.bpm.has_value())
        {
            std::cout
                << item.bpm.value();
        }
        else
        {
            std::cout << "--";
        }

        std::cout
            << " | expected: ";

        if (test.expectedBpm.has_value())
        {
            std::cout
                << test.expectedBpm.value();
        }
        else
        {
            std::cout << "--";
        }

        std::cout
            << "\n       Key: "
            << (item.key.isEmpty()
                    ? "--"
                    : item.key.toStdString())
            << " | expected: "
            << (test.expectedKey.isEmpty()
                    ? "--"
                    : test.expectedKey.toStdString())
            << "\n\n";
    }

    // --------------------------------------------------
    // Subject fallback
    // --------------------------------------------------

    {
        LoopItem item;

        item.filename =
            "mystery_loop.wav";

        item.subject =
            "New loop C#min 142 BPM";

        MetadataParser::parse(item);

        const bool success =
            bpmMatches(
                item.bpm,
                142.0)
            && item.key
                   == "C# minor";

        if (success)
            ++passed;
        else
            ++failed;

        std::cout
            << (success
                    ? "[PASS] "
                    : "[FAIL] ")
            << "Subject fallback"
            << "\n";

        std::cout
            << "       BPM: ";

        if (item.bpm.has_value())
        {
            std::cout
                << item.bpm.value();
        }
        else
        {
            std::cout << "--";
        }

        std::cout
            << " | expected: 142"
            << "\n";

        std::cout
            << "       Key: "
            << (item.key.isEmpty()
                    ? "--"
                    : item.key.toStdString())
            << " | expected: C# minor"
            << "\n\n";
    }

    const auto check = [&](bool ok, const char* name)
    {
        if (ok) ++passed; else ++failed;
        std::cout << (ok ? "[PASS] " : "[FAIL] ") << name << "\n";
    };
    LoopItem priority;
    priority.filename = "loop Db minor.wav";
    priority.subject = "C major 120 BPM";
    MetadataParser::parse(priority);
    check(priority.key == "Db minor" && priority.musicalKey
        && priority.musicalKey->pitchClass == 1 && priority.musicalKey->minor, "Filename priority and flat pitch class");
    const auto c = MetadataParser::musicalKeyFromLabel("C major");
    check(MetadataParser::previewSemitones(c, { 6, false }, true, 0) == 6, "Six-semitone tie is positive");
    check(MetadataParser::previewSemitones(c, { 11, false }, true, 0) == -1, "Nearest downward interval");
    check(MetadataParser::previewSemitones(c, { 2, true }, true, 3) == 3, "Mode mismatch keeps manual adjustment");
    check(MetadataParser::previewSemitones(std::nullopt, { 2, false }, true, -4) == -4, "Unknown key keeps manual adjustment");
    check(MetadataParser::previewSemitones(c, { 2, false }, false, 3) == 3, "Sync disabled keeps manual adjustment");
    check(MetadataParser::previewSemitones(c, { 2, false }, true, 3) == 5, "Automatic and manual offsets add");
    check(MetadataParser::previewSemitones(c, { 6, false }, true, 12) == 12, "Positive effective pitch limit");
    check(MetadataParser::previewSemitones(c, { 7, false }, true, -12) == -12, "Negative effective pitch limit");
    const int classes[] = { 0, 2, 4, 5, 7, 9, 11 };
    int index = 0;
    for (const auto* root : { "C", "D", "E", "F", "G", "A", "B" })
    {
        const auto key = MetadataParser::musicalKeyFromLabel(juce::String(root) + " major");
        check(key && key->pitchClass == classes[index++], root);
    }
    const auto cs = MetadataParser::musicalKeyFromLabel("C# minor");
    const auto db = MetadataParser::musicalKeyFromLabel("Db minor");
    check(cs && db && cs->pitchClass == db->pitchClass, "Enharmonic equivalence");

    std::cout
        << "================================\n"
        << "Passed: "
        << passed
        << "\n"
        << "Failed: "
        << failed
        << "\n\n";

    if (failed == 0)
    {
        std::cout
            << "ALL TESTS PASSED\n";

        return 0;
    }

    std::cout
        << "TESTS FAILED\n";

    return 1;
}