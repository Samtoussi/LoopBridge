#include "MetadataParser.h"

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

        // BPM typo / invalid value.
        {
            "KAYN GREY 1348BPM.mp3",
            std::nullopt,
            ""
        },

        // No metadata at all.
        {
            "cool melody idea.wav",
            std::nullopt,
            ""
        },

        // Subject fallback test is handled separately below.
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