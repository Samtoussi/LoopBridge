#include <JuceHeader.h>

#include "GmailClient.h"
#include "LoopItem.h"
#include "MetadataParser.h"

#include <signalsmith-stretch/signalsmith-stretch.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <future>
#include <limits>
#include <memory>
#include <vector>


class LoopBridgeWindow
    : public juce::DocumentWindow
{
public:
    LoopBridgeWindow()
        : DocumentWindow(
              "LoopBridge",
              juce::Colour::fromRGB(
                  20,
                  20,
                  24),
              DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar(true);
        setResizable(false, false);

        setContentOwned(
            new MainComponent(),
            true);

        centreWithSize(
            900,
            800);

        setVisible(true);
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::
            getInstance()
            ->systemRequestedQuit();
    }

private:
    class MainComponent
        : public juce::AudioAppComponent,
          private juce::Timer
    {
    public:
        MainComponent()
            : receiveSocket(false),
              sendSocket(false)
        {
            listening =
                receiveSocket.bindToPort(
                    49152,
                    "127.0.0.1");

            formatManager
                .registerBasicFormats();

            setWantsKeyboardFocus(true);

            loadButton.setButtonText(
                "Load 100 BPM WAV");

            playButton.setButtonText(
                "Play");

            stopButton.setButtonText(
                "Stop");

            bridgeButton.setButtonText(
                "BRIDGE ON");

            gmailButton.setButtonText(
                "CONNECT GMAIL");

            addAndMakeVisible(
                loadButton);

            addAndMakeVisible(
                playButton);

            addAndMakeVisible(
                stopButton);

            addAndMakeVisible(
                bridgeButton);

            addAndMakeVisible(
                gmailButton);

            playButton.setEnabled(false);
            stopButton.setEnabled(false);

            loadButton.onClick =
                [this]
                {
                    chooseAudioFile();
                };

            playButton.onClick =
                [this]
                {
                    startSoloPreview();
                };

            stopButton.onClick =
                [this]
                {
                    stopSoloPreview();
                };

            bridgeButton.onClick =
                [this]
                {
                    bridgeEnabled =
                        !bridgeEnabled;

                    bridgeButton.setButtonText(
                        bridgeEnabled
                            ? "BRIDGE ON"
                            : "BRIDGE OFF");

                    sendBridgeState();

                    repaint();
                };

            gmailButton.onClick =
                [this]
                {
                    if (gmailClient.getState()
                        == GmailClient::State::connected)
                    {
                        gmailClient.disconnect();

                        gmailButton.setButtonText(
                            "CONNECT GMAIL");

                        gmailStatus =
                            "GMAIL: NOT CONNECTED";

                        loopItems.clear();
                        selectedLoopIndex = -1;
                        browserScrollIndex = 0;

                        repaint();
                        return;
                    }

                    gmailButton.setEnabled(false);

                    gmailStatus =
                        "GMAIL: CONNECTING...";

                    repaint();

                    gmailClient.connect(
                        [this](
                            GmailClient::State state,
                            const juce::String& message)
                        {
                            gmailButton.setEnabled(
                                state
                                != GmailClient::State::authorizing);

                            if (state
                                == GmailClient::State::connected)
                            {
                                gmailButton.setButtonText(
                                    "DISCONNECT GMAIL");

                                gmailStatus =
                                    "GMAIL: CONNECTED";

                                juce::Logger::writeToLog(
                                    "\n==============================\n"
                                    "LOOPBRIDGE GMAIL LIBRARY\n"
                                    "==============================");

                                gmailClient.fetchRecentAudioAttachments(
                                    100,
                                    [this](
                                        const std::vector<
                                            GmailClient::AudioAttachment>& attachments,
                                        const juce::String& error)
                                    {
                                        if (error.isNotEmpty())
                                        {
                                            juce::Logger::writeToLog(
                                                "GMAIL ERROR: "
                                                + error);

                                            gmailStatus =
                                                "GMAIL ERROR: "
                                                + error;

                                            repaint();
                                            return;
                                        }

                                        loopItems.clear();

                                        loopItems.reserve(
                                            attachments.size());

                                        for (const auto& attachment
                                             : attachments)
                                        {
                                            LoopItem item;

                                            item.messageId =
                                                attachment.messageId;

                                            item.attachmentId =
                                                attachment.attachmentId;

                                            item.filename =
                                                attachment.filename;

                                            item.subject =
                                                attachment.subject;

                                            parseSender(
                                                attachment.sender,
                                                item.senderName,
                                                item.senderEmail);

                                            MetadataParser::parse(
                                                item);

                                            loopItems.push_back(
                                                std::move(item));
                                        }

                                        selectedLoopIndex =
                                            loopItems.empty()
                                                ? -1
                                                : 0;

                                        browserScrollIndex =
                                            0;

                                        gmailStatus =
                                            "GMAIL: "
                                            + juce::String(
                                                static_cast<int>(
                                                    loopItems.size()))
                                            + " AUDIO FILES FOUND";

                                        juce::Logger::writeToLog(
                                            "Audio attachments found: "
                                            + juce::String(
                                                static_cast<int>(
                                                    loopItems.size())));

                                        juce::Logger::writeToLog(
                                            "------------------------------");

                                        for (size_t i = 0;
                                             i < loopItems.size();
                                             ++i)
                                        {
                                            const auto& item =
                                                loopItems[i];

                                            juce::String line =
                                                juce::String(
                                                    static_cast<int>(
                                                        i + 1))
                                                + ". "
                                                + item.filename;

                                            line +=
                                                " | "
                                                + getSenderDisplayName(
                                                    item);

                                            line +=
                                                " | BPM: ";

                                            if (item.bpm.has_value())
                                            {
                                                line +=
                                                    juce::String(
                                                        *item.bpm,
                                                        0);
                                            }
                                            else
                                            {
                                                line += "--";
                                            }

                                            line +=
                                                " | KEY: ";

                                            if (item.key.isNotEmpty())
                                            {
                                                line +=
                                                    item.key;
                                            }
                                            else
                                            {
                                                line += "--";
                                            }

                                            juce::Logger::writeToLog(
                                                line);
                                        }

                                        grabKeyboardFocus();

                                        repaint();
                                    });
                            }
                            else if (state
                                     == GmailClient::State::authorizing)
                            {
                                gmailButton.setButtonText(
                                    "CONNECTING...");

                                gmailStatus =
                                    "GMAIL: "
                                    + message;
                            }
                            else if (state
                                     == GmailClient::State::error)
                            {
                                gmailButton.setButtonText(
                                    "CONNECT GMAIL");

                                gmailStatus =
                                    "GMAIL ERROR: "
                                    + message;
                            }
                            else
                            {
                                gmailButton.setButtonText(
                                    "CONNECT GMAIL");

                                gmailStatus =
                                    "GMAIL: NOT CONNECTED";
                            }

                            repaint();
                        });
                };

            // Desktop audio is ONLY used
            // for manual solo preview.
            setAudioChannels(
                0,
                2);

            startTimer(5);
        }

        ~MainComponent() override
        {
            stopTimer();

            if (previewBuildFuture.valid())
            {
                previewBuildFuture.wait();
            }

            shutdownAudio();
        }

        void prepareToPlay(
            int,
            double newSampleRate) override
        {
            deviceSampleRate =
                newSampleRate;

            rebuildSoloBuffer();

            repaint();
        }

        void releaseResources() override
        {
        }

        void getNextAudioBlock(
            const juce::AudioSourceChannelInfo&
                info) override
        {
            info.clearActiveBufferRegion();

            const juce::ScopedLock lock(
                audioLock);

            // Bridge audio is rendered by the VST.
            //
            // Desktop audio callback is ONLY
            // for the manual Play button.
            if (!soloPlaying)
                return;

            if (soloBuffer.getNumSamples() <= 0)
                return;

            const int loopSamples =
                soloBuffer.getNumSamples();

            const int sourceChannels =
                soloBuffer.getNumChannels();

            const int outputChannels =
                info.buffer->getNumChannels();

            int destinationOffset = 0;

            int remaining =
                info.numSamples;

            while (remaining > 0)
            {
                if (soloPlaybackPosition
                    >= loopSamples)
                {
                    soloPlaybackPosition = 0;
                }

                const int available =
                    loopSamples
                    - soloPlaybackPosition;

                const int samplesToCopy =
                    std::min(
                        remaining,
                        available);

                for (int channel = 0;
                     channel < outputChannels;
                     ++channel)
                {
                    const int sourceChannel =
                        std::min(
                            channel,
                            sourceChannels - 1);

                    info.buffer->copyFrom(
                        channel,
                        info.startSample
                            + destinationOffset,
                        soloBuffer,
                        sourceChannel,
                        soloPlaybackPosition,
                        samplesToCopy);
                }

                soloPlaybackPosition +=
                    samplesToCopy;

                destinationOffset +=
                    samplesToCopy;

                remaining -=
                    samplesToCopy;
            }
        }

        bool keyPressed(
            const juce::KeyPress& key) override
        {
            if (loopItems.empty())
                return false;

            if (key
                == juce::KeyPress::downKey)
            {
                selectLoop(
                    std::min(
                        selectedLoopIndex + 1,
                        static_cast<int>(
                            loopItems.size())
                            - 1));

                return true;
            }

            if (key
                == juce::KeyPress::upKey)
            {
                selectLoop(
                    std::max(
                        selectedLoopIndex - 1,
                        0));

                return true;
            }

            if (key
                == juce::KeyPress::homeKey)
            {
                selectLoop(0);
                return true;
            }

            if (key
                == juce::KeyPress::endKey)
            {
                selectLoop(
                    static_cast<int>(
                        loopItems.size())
                    - 1);

                return true;
            }

            return false;
        }

        void mouseDown(
            const juce::MouseEvent& event) override
        {
            if (loopItems.empty())
            {
                grabKeyboardFocus();
                return;
            }

            const int rowsTop =
                browserTop
                + browserHeaderHeight;

            if (event.y < rowsTop
                || event.y
                       >= rowsTop
                              + browserVisibleRows
                                    * browserRowHeight)
            {
                grabKeyboardFocus();
                return;
            }

            const int visibleRow =
                (event.y - rowsTop)
                / browserRowHeight;

            const int itemIndex =
                browserScrollIndex
                + visibleRow;

            if (itemIndex >= 0
                && itemIndex
                       < static_cast<int>(
                           loopItems.size()))
            {
                selectLoop(
                    itemIndex);
            }

            grabKeyboardFocus();
        }

        void paint(
            juce::Graphics& g) override
        {
            g.fillAll(
                juce::Colour::fromRGB(
                    20,
                    20,
                    24));

            g.setColour(
                juce::Colours::white);

            g.setFont(
                juce::Font(
                    juce::FontOptions(
                        32.0f,
                        juce::Font::bold)));

            g.drawFittedText(
                "LOOPBRIDGE",
                0,
                20,
                getWidth(),
                45,
                juce::Justification::centred,
                1);

            g.setFont(
                juce::Font(
                    juce::FontOptions(
                        15.0f)));

            g.setColour(
                listening
                    ? juce::Colours::lightgreen
                    : juce::Colours::red);

            g.drawFittedText(
                listening
                    ? "LISTENING"
                    : "PORT ERROR",
                0,
                67,
                getWidth(),
                25,
                juce::Justification::centred,
                1);

            g.setColour(
                juce::Colours::white);

            g.setFont(
                juce::Font(
                    juce::FontOptions(
                        38.0f,
                        juce::Font::bold)));

            const auto bpmText =
                hostBpm > 0.0
                    ? juce::String(
                          hostBpm,
                          2)
                    : "--";

            g.drawFittedText(
                bpmText,
                0,
                95,
                getWidth(),
                50,
                juce::Justification::centred,
                1);

            g.setColour(
                juce::Colours::white
                    .withAlpha(0.55f));

            g.setFont(
                juce::Font(
                    juce::FontOptions(
                        14.0f)));

            g.drawFittedText(
                "HOST BPM",
                0,
                140,
                getWidth(),
                22,
                juce::Justification::centred,
                1);

            g.setColour(
                hostPlaying
                    ? juce::Colours::lightgreen
                    : juce::Colours::white
                          .withAlpha(0.55f));

            g.setFont(
                juce::Font(
                    juce::FontOptions(
                        14.0f,
                        juce::Font::bold)));

            g.drawFittedText(
                hostPlaying
                    ? "FL PLAYING"
                    : "FL STOPPED",
                0,
                170,
                getWidth(),
                25,
                juce::Justification::centred,
                1);

            g.setColour(
                juce::Colours::white
                    .withAlpha(0.75f));

            g.setFont(
                juce::Font(
                    juce::FontOptions(
                        14.0f)));

            g.drawFittedText(
                "PPQ: "
                    + juce::String(
                        getEstimatedPpq(),
                        3),
                0,
                198,
                getWidth(),
                22,
                juce::Justification::centred,
                1);

            juce::String sourceText =
                "SOURCE: 100 BPM / 4 BARS";

            if (hostBpm > 0.0)
            {
                sourceText +=
                    "  ->  HOST: "
                    + juce::String(
                        hostBpm,
                        0)
                    + " BPM";
            }

            g.setColour(
                juce::Colours::white
                    .withAlpha(0.65f));

            g.drawFittedText(
                sourceText,
                0,
                225,
                getWidth(),
                22,
                juce::Justification::centred,
                1);

            g.setColour(
                juce::Colours::orange);

            g.setFont(
                juce::Font(
                    juce::FontOptions(
                        14.0f,
                        juce::Font::bold)));

            const juce::String flSrText =
                hostSampleRate > 0.0
                    ? juce::String(
                          hostSampleRate,
                          0)
                          + " Hz"
                    : "--";

            const juce::String fileSrText =
                sourceSampleRate > 0.0
                    ? juce::String(
                          sourceSampleRate,
                          0)
                          + " Hz"
                    : "--";

            const juce::String deviceSrText =
                deviceSampleRate > 0.0
                    ? juce::String(
                          deviceSampleRate,
                          0)
                          + " Hz"
                    : "--";

            g.drawFittedText(
                "FL SR: "
                    + flSrText
                    + "   |   FILE SR: "
                    + fileSrText
                    + "   |   DEVICE SR: "
                    + deviceSrText,
                15,
                255,
                getWidth() - 30,
                25,
                juce::Justification::centred,
                1);

            g.setFont(
                juce::Font(
                    juce::FontOptions(
                        13.0f,
                        juce::Font::bold)));

            if (stretching)
            {
                g.setColour(
                    juce::Colours::orange);

                g.drawFittedText(
                    "PREPARING NEW BPM...",
                    0,
                    287,
                    getWidth(),
                    22,
                    juce::Justification::centred,
                    1);
            }
            else if (soloPlaying)
            {
                g.setColour(
                    juce::Colours::lightgreen);

                g.drawFittedText(
                    "SOLO PREVIEW / ORIGINAL 100 BPM",
                    0,
                    287,
                    getWidth(),
                    22,
                    juce::Justification::centred,
                    1);
            }
            else if (hostPreviewReady)
            {
                g.setColour(
                    bridgeEnabled
                        ? juce::Colours::lightgreen
                        : juce::Colours::white
                              .withAlpha(0.55f));

                g.drawFittedText(
                    bridgeEnabled
                        ? "HOST PREVIEW -> FL MIXER"
                        : "BRIDGE DISCONNECTED",
                    0,
                    287,
                    getWidth(),
                    22,
                    juce::Justification::centred,
                    1);
            }

            g.setColour(
                juce::Colours::white
                    .withAlpha(0.75f));

            g.setFont(
                juce::Font(
                    juce::FontOptions(
                        14.0f)));

            const juce::String fileText =
                loadedFileName.isEmpty()
                    ? "No audio file loaded"
                    : loadedFileName;

            g.drawFittedText(
                fileText,
                30,
                320,
                getWidth() - 60,
                30,
                juce::Justification::centred,
                1);

            if (sourceBuffer.getNumSamples() > 0
                && sourceSampleRate > 0.0)
            {
                const double fileSeconds =
                    sourceBuffer.getNumSamples()
                    / sourceSampleRate;

                const double musicalSeconds =
                    loopBeats
                    * 60.0
                    / sourceBpm;

                juce::String lengthText =
                    "File: "
                    + juce::String(
                        fileSeconds,
                        3)
                    + "s / Musical: "
                    + juce::String(
                        musicalSeconds,
                        3)
                    + "s";

                if (hostPreviewBuffer
                            .getNumSamples()
                        > 0
                    && stretchedForHostSampleRate
                           > 0.0)
                {
                    const double
                        stretchedSeconds =
                            hostPreviewBuffer
                                .getNumSamples()
                            / stretchedForHostSampleRate;

                    lengthText +=
                        " -> "
                        + juce::String(
                            stretchedSeconds,
                            3)
                        + "s";
                }

                g.setColour(
                    juce::Colours::white
                        .withAlpha(0.5f));

                g.drawFittedText(
                    lengthText,
                    0,
                    350,
                    getWidth(),
                    22,
                    juce::Justification::centred,
                    1);
            }

            g.setColour(
                gmailClient.getState()
                        == GmailClient::State::connected
                    ? juce::Colours::lightgreen
                    : juce::Colours::white
                          .withAlpha(0.55f));

            g.setFont(
                juce::Font(
                    juce::FontOptions(
                        12.0f,
                        juce::Font::bold)));

            g.drawFittedText(
                gmailStatus,
                20,
                378,
                getWidth() - 40,
                20,
                juce::Justification::centred,
                1);

            drawLoopBrowser(
                g);
        }

        void resized() override
        {
            const int buttonWidth =
                150;

            const int buttonHeight =
                40;

            const int gap =
                12;

            const int totalWidth =
                buttonWidth * 3
                + gap * 2;

            const int startX =
                (getWidth()
                 - totalWidth)
                / 2;

            const int y =
                410;

            loadButton.setBounds(
                startX,
                y,
                buttonWidth,
                buttonHeight);

            playButton.setBounds(
                startX
                    + buttonWidth
                    + gap,
                y,
                buttonWidth,
                buttonHeight);

            stopButton.setBounds(
                startX
                    + (buttonWidth + gap)
                          * 2,
                y,
                buttonWidth,
                buttonHeight);

            bridgeButton.setBounds(
                (getWidth() - 180) / 2,
                460,
                180,
                40);

            gmailButton.setBounds(
                (getWidth() - 180) / 2,
                510,
                180,
                40);
        }

    private:
        static constexpr double sourceBpm =
            100.0;

        static constexpr double loopBeats =
            16.0;

        static constexpr double
            previewDebounceMs = 150.0;

        static constexpr double
            ppqDiscontinuityThreshold = 0.25;

        static constexpr int
            browserTop = 590;

        static constexpr int
            browserLeft = 40;

        static constexpr int
            browserHeaderHeight = 36;

        static constexpr int
            browserRowHeight = 32;

        static constexpr int
            browserVisibleRows = 5;

        struct HostPreviewBuildResult
        {
            juce::AudioBuffer<float>
                buffer;

            double bpm = 0.0;
            double sampleRate = 0.0;

            bool success = false;
        };

        static void parseSender(
            const juce::String& rawSender,
            juce::String& name,
            juce::String& email)
        {
            const int openBracket =
                rawSender.indexOfChar(
                    '<');

            const int closeBracket =
                rawSender.indexOfChar(
                    '>');

            if (openBracket >= 0
                && closeBracket > openBracket)
            {
                name =
                    rawSender
                        .substring(
                            0,
                            openBracket)
                        .trim()
                        .unquoted();

                email =
                    rawSender
                        .substring(
                            openBracket + 1,
                            closeBracket)
                        .trim();

                if (name.isEmpty())
                {
                    name =
                        email
                            .upToFirstOccurrenceOf(
                                "@",
                                false,
                                false);
                }

                return;
            }

            if (rawSender.containsChar(
                    '@'))
            {
                email =
                    rawSender.trim();

                name =
                    email
                        .upToFirstOccurrenceOf(
                            "@",
                            false,
                            false);

                return;
            }

            name =
                rawSender.trim();

            email.clear();
        }

        static juce::String
        getSenderDisplayName(
            const LoopItem& item)
        {
            if (item.senderName.isNotEmpty())
            {
                return item.senderName;
            }

            if (item.senderEmail.isNotEmpty())
            {
                return item.senderEmail
                    .upToFirstOccurrenceOf(
                        "@",
                        false,
                        false);
            }

            return "--";
        }

        void selectLoop(
            int index)
        {
            if (loopItems.empty())
            {
                selectedLoopIndex =
                    -1;

                browserScrollIndex =
                    0;

                repaint();
                return;
            }

            index =
                juce::jlimit(
                    0,
                    static_cast<int>(
                        loopItems.size())
                        - 1,
                    index);

            selectedLoopIndex =
                index;

            if (selectedLoopIndex
                < browserScrollIndex)
            {
                browserScrollIndex =
                    selectedLoopIndex;
            }
            else if (
                selectedLoopIndex
                >= browserScrollIndex
                       + browserVisibleRows)
            {
                browserScrollIndex =
                    selectedLoopIndex
                    - browserVisibleRows
                    + 1;
            }

            const int maxScroll =
                std::max(
                    0,
                    static_cast<int>(
                        loopItems.size())
                        - browserVisibleRows);

            browserScrollIndex =
                juce::jlimit(
                    0,
                    maxScroll,
                    browserScrollIndex);

            const auto& item =
                loopItems[
                    static_cast<size_t>(
                        selectedLoopIndex)];

            juce::Logger::writeToLog(
                "SELECTED LOOP: "
                + item.filename
                + " | "
                + getSenderDisplayName(
                    item));

            repaint();
        }

        void drawLoopBrowser(
            juce::Graphics& g)
        {
            const int browserWidth =
                getWidth()
                - browserLeft * 2;

            const int browserHeight =
                browserHeaderHeight
                + browserVisibleRows
                      * browserRowHeight;

            const juce::Rectangle<int>
                browserBounds(
                    browserLeft,
                    browserTop,
                    browserWidth,
                    browserHeight);

            g.setColour(
                juce::Colour::fromRGB(
                    26,
                    28,
                    32));

            g.fillRoundedRectangle(
                browserBounds.toFloat(),
                7.0f);

            g.setColour(
                juce::Colours::white
                    .withAlpha(0.12f));

            g.drawRoundedRectangle(
                browserBounds.toFloat(),
                7.0f,
                1.0f);

            g.setFont(
                juce::Font(
                    juce::FontOptions(
                        11.5f,
                        juce::Font::bold)));

            g.setColour(
                juce::Colours::white
                    .withAlpha(0.55f));

            const int filenameX =
                browserLeft + 14;

            const int senderX =
                browserLeft
                + browserWidth
                - 330;

            const int bpmX =
                browserLeft
                + browserWidth
                - 180;

            const int keyX =
                browserLeft
                + browserWidth
                - 90;

            g.drawText(
                "LOOP",
                filenameX,
                browserTop,
                senderX
                    - filenameX
                    - 10,
                browserHeaderHeight,
                juce::Justification::
                    centredLeft);

            g.drawText(
                "SENDER",
                senderX,
                browserTop,
                140,
                browserHeaderHeight,
                juce::Justification::
                    centredLeft);

            g.drawText(
                "BPM",
                bpmX,
                browserTop,
                80,
                browserHeaderHeight,
                juce::Justification::
                    centredLeft);

            g.drawText(
                "KEY",
                keyX,
                browserTop,
                75,
                browserHeaderHeight,
                juce::Justification::
                    centredLeft);

            g.setColour(
                juce::Colours::white
                    .withAlpha(0.08f));

            g.drawHorizontalLine(
                browserTop
                    + browserHeaderHeight,
                static_cast<float>(
                    browserLeft),
                static_cast<float>(
                    browserLeft
                    + browserWidth));

            if (loopItems.empty())
            {
                g.setColour(
                    juce::Colours::white
                        .withAlpha(0.4f));

                g.setFont(
                    juce::Font(
                        juce::FontOptions(
                            13.0f)));

                g.drawText(
                    gmailClient.getState()
                            == GmailClient::State::connected
                        ? "No audio loops found"
                        : "Connect Gmail to load loops",
                    browserLeft,
                    browserTop
                        + browserHeaderHeight,
                    browserWidth,
                    browserVisibleRows
                        * browserRowHeight,
                    juce::Justification::
                        centred);

                return;
            }

            const int endIndex =
                std::min(
                    browserScrollIndex
                        + browserVisibleRows,
                    static_cast<int>(
                        loopItems.size()));

            for (int itemIndex =
                     browserScrollIndex;
                 itemIndex < endIndex;
                 ++itemIndex)
            {
                const int visibleIndex =
                    itemIndex
                    - browserScrollIndex;

                const int rowY =
                    browserTop
                    + browserHeaderHeight
                    + visibleIndex
                          * browserRowHeight;

                const bool selected =
                    itemIndex
                    == selectedLoopIndex;

                if (selected)
                {
                    g.setColour(
                        juce::Colour::fromRGB(
                            50,
                            58,
                            64));

                    g.fillRect(
                        browserLeft + 1,
                        rowY,
                        browserWidth - 2,
                        browserRowHeight);
                }
                else if (
                    visibleIndex % 2 != 0)
                {
                    g.setColour(
                        juce::Colours::white
                            .withAlpha(0.018f));

                    g.fillRect(
                        browserLeft + 1,
                        rowY,
                        browserWidth - 2,
                        browserRowHeight);
                }

                const auto& item =
                    loopItems[
                        static_cast<size_t>(
                            itemIndex)];

                g.setFont(
                    juce::Font(
                        juce::FontOptions(
                            12.5f,
                            selected
                                ? juce::Font::bold
                                : juce::Font::plain)));

                g.setColour(
                    selected
                        ? juce::Colours::white
                        : juce::Colours::white
                              .withAlpha(0.82f));

                g.drawFittedText(
                    item.filename,
                    filenameX,
                    rowY,
                    senderX
                        - filenameX
                        - 12,
                    browserRowHeight,
                    juce::Justification::
                        centredLeft,
                    1);

                g.setColour(
                    selected
                        ? juce::Colours::white
                              .withAlpha(0.9f)
                        : juce::Colours::white
                              .withAlpha(0.62f));

                g.drawFittedText(
                    getSenderDisplayName(
                        item),
                    senderX,
                    rowY,
                    135,
                    browserRowHeight,
                    juce::Justification::
                        centredLeft,
                    1);

                const juce::String bpmText =
                    item.bpm.has_value()
                        ? juce::String(
                              *item.bpm,
                              0)
                        : "--";

                g.setColour(
                    item.bpm.has_value()
                        ? juce::Colours::
                              lightgreen
                        : juce::Colours::white
                              .withAlpha(0.3f));

                g.drawText(
                    bpmText,
                    bpmX,
                    rowY,
                    70,
                    browserRowHeight,
                    juce::Justification::
                        centredLeft);

                const juce::String keyText =
                    item.key.isNotEmpty()
                        ? item.key
                        : "--";

                g.setColour(
                    item.key.isNotEmpty()
                        ? juce::Colours::
                              lightgreen
                        : juce::Colours::white
                              .withAlpha(0.3f));

                g.drawFittedText(
                    keyText,
                    keyX,
                    rowY,
                    75,
                    browserRowHeight,
                    juce::Justification::
                        centredLeft,
                    1);

                g.setColour(
                    juce::Colours::white
                        .withAlpha(0.045f));

                g.drawHorizontalLine(
                    rowY
                        + browserRowHeight
                        - 1,
                    static_cast<float>(
                        browserLeft + 8),
                    static_cast<float>(
                        browserLeft
                        + browserWidth
                        - 8));
            }

            if (selectedLoopIndex >= 0
                && selectedLoopIndex
                       < static_cast<int>(
                           loopItems.size()))
            {
                const auto& selected =
                    loopItems[
                        static_cast<size_t>(
                            selectedLoopIndex)];

                g.setColour(
                    juce::Colours::white
                        .withAlpha(0.45f));

                g.setFont(
                    juce::Font(
                        juce::FontOptions(
                            11.0f)));

                const juce::String positionText =
                    juce::String(
                        selectedLoopIndex + 1)
                    + " / "
                    + juce::String(
                        static_cast<int>(
                            loopItems.size()));

                g.drawText(
                    positionText,
                    browserLeft,
                    browserTop
                        + browserHeight
                        + 6,
                    browserWidth,
                    20,
                    juce::Justification::
                        centredRight);

                juce::String metadataText =
                    "Selected: "
                    + selected.filename;

                if (selected.bpm.has_value())
                {
                    metadataText +=
                        "  |  "
                        + juce::String(
                            *selected.bpm,
                            0)
                        + " BPM";
                }

                if (selected.key.isNotEmpty())
                {
                    metadataText +=
                        "  |  "
                        + selected.key;
                }

                g.drawFittedText(
                    metadataText,
                    browserLeft,
                    browserTop
                        + browserHeight
                        + 6,
                    browserWidth - 90,
                    20,
                    juce::Justification::
                        centredLeft,
                    1);
            }
        }

        void chooseAudioFile()
        {
            fileChooser =
                std::make_unique<
                    juce::FileChooser>(
                    "Choose a 100 BPM / 4 bar WAV",
                    juce::File{},
                    "*.wav");

            const auto flags =
                juce::FileBrowserComponent::
                    openMode
                | juce::FileBrowserComponent::
                      canSelectFiles;

            fileChooser->launchAsync(
                flags,
                [this](
                    const juce::FileChooser&
                        chooser)
                {
                    const auto file =
                        chooser.getResult();

                    if (!file.existsAsFile())
                        return;

                    loadAudioFile(
                        file);
                });
        }

        void loadAudioFile(
            const juce::File& file)
        {
            std::unique_ptr<
                juce::AudioFormatReader>
                reader(
                    formatManager
                        .createReaderFor(
                            file));

            if (reader == nullptr)
                return;

            stopSoloPreview();

            sourceSampleRate =
                reader->sampleRate;

            const int channels =
                static_cast<int>(
                    reader->numChannels);

            const int64 totalSamples64 =
                reader->lengthInSamples;

            if (totalSamples64 <= 0
                || totalSamples64
                    > std::numeric_limits<int>::
                          max())
            {
                return;
            }

            const int totalSamples =
                static_cast<int>(
                    totalSamples64);

            sourceBuffer.setSize(
                channels,
                totalSamples);

            reader->read(
                &sourceBuffer,
                0,
                totalSamples,
                0,
                true,
                true);

            const double
                musicalDurationSeconds =
                    loopBeats
                    * 60.0
                    / sourceBpm;

            sourceMusicalSamples =
                std::min(
                    sourceBuffer
                        .getNumSamples(),
                    static_cast<int>(
                        std::llround(
                            musicalDurationSeconds
                            * sourceSampleRate)));

            loadedFileName =
                file.getFileName();

            rebuildSoloBuffer();

            hostPreviewReady =
                false;

            if (hostBpm > 0.0
                && hostSampleRate > 0.0)
            {
                scheduleHostPreviewBuild(
                    true);
            }

            playButton.setEnabled(
                sourceMusicalSamples > 0);

            repaint();
        }

        void rebuildSoloBuffer()
        {
            if (sourceMusicalSamples <= 0
                || sourceSampleRate <= 0.0
                || deviceSampleRate <= 0.0)
            {
                return;
            }

            const int channels =
                sourceBuffer
                    .getNumChannels();

            const int outputSamples =
                std::max(
                    1,
                    static_cast<int>(
                        std::llround(
                            sourceMusicalSamples
                            * deviceSampleRate
                            / sourceSampleRate)));

            juce::AudioBuffer<float>
                newSoloBuffer;

            newSoloBuffer.setSize(
                channels,
                outputSamples);

            const double ratio =
                sourceSampleRate
                / deviceSampleRate;

            for (int channel = 0;
                 channel < channels;
                 ++channel)
            {
                juce::LagrangeInterpolator
                    interpolator;

                interpolator.process(
                    ratio,
                    sourceBuffer
                        .getReadPointer(
                            channel),
                    newSoloBuffer
                        .getWritePointer(
                            channel),
                    outputSamples);
            }

            {
                const juce::ScopedLock lock(
                    audioLock);

                soloBuffer =
                    std::move(
                        newSoloBuffer);

                soloPlaybackPosition =
                    0;
            }
        }

        void scheduleHostPreviewBuild(
            bool immediate)
        {
            if (sourceMusicalSamples <= 0
                || sourceSampleRate <= 0.0
                || hostSampleRate <= 0.0
                || hostBpm <= 0.0)
            {
                return;
            }

            pendingPreviewBpm =
                hostBpm;

            pendingPreviewSampleRate =
                hostSampleRate;

            previewBuildRequestedMs =
                juce::Time::
                    getMillisecondCounterHiRes();

            previewBuildPending =
                true;

            stretching =
                true;

            if (immediate
                && !previewBuildRunning)
            {
                startPendingPreviewBuild();
            }

            repaint();
        }

        void startPendingPreviewBuild()
        {
            if (previewBuildRunning
                || !previewBuildPending)
            {
                return;
            }

            if (sourceMusicalSamples <= 0
                || sourceSampleRate <= 0.0
                || pendingPreviewBpm <= 0.0
                || pendingPreviewSampleRate <= 0.0)
            {
                previewBuildPending =
                    false;

                stretching =
                    false;

                return;
            }

            const double targetBpm =
                pendingPreviewBpm;

            const double targetSampleRate =
                pendingPreviewSampleRate;

            juce::AudioBuffer<float>
                sourceSnapshot;

            sourceSnapshot.makeCopyOf(
                sourceBuffer);

            const int musicalSamples =
                sourceMusicalSamples;

            const double sourceRate =
                sourceSampleRate;

            previewBuildPending =
                false;

            previewBuildRunning =
                true;

            previewBuildFuture =
                std::async(
                    std::launch::async,
                    [
                        sourceSnapshot =
                            std::move(
                                sourceSnapshot),
                        musicalSamples,
                        sourceRate,
                        targetBpm,
                        targetSampleRate
                    ]() mutable
                    {
                        HostPreviewBuildResult
                            result;

                        result.bpm =
                            targetBpm;

                        result.sampleRate =
                            targetSampleRate;

                        if (musicalSamples <= 0
                            || sourceRate <= 0.0
                            || targetBpm <= 0.0
                            || targetSampleRate <= 0.0)
                        {
                            return result;
                        }

                        const int channels =
                            sourceSnapshot
                                .getNumChannels();

                        if (channels <= 0)
                            return result;

                        const double lengthRatio =
                            sourceBpm
                            / targetBpm;

                        const int
                            stretchedSamplesAtSourceRate =
                                std::max(
                                    1,
                                    static_cast<int>(
                                        std::llround(
                                            musicalSamples
                                            * lengthRatio)));

                        signalsmith::stretch::
                            SignalsmithStretch<float>
                                stretch;

                        stretch.presetDefault(
                            channels,
                            sourceRate);

                        stretch.setTransposeFactor(
                            1.0);

                        std::vector<
                            const float*>
                            inputPointers(
                                static_cast<
                                    size_t>(
                                    channels));

                        for (int channel = 0;
                             channel < channels;
                             ++channel)
                        {
                            inputPointers[
                                static_cast<
                                    size_t>(
                                    channel)] =
                                sourceSnapshot
                                    .getReadPointer(
                                        channel);
                        }

                        juce::AudioBuffer<float>
                            stretchedAtSourceRate;

                        stretchedAtSourceRate
                            .setSize(
                                channels,
                                stretchedSamplesAtSourceRate);

                        std::vector<float*>
                            outputPointers(
                                static_cast<
                                    size_t>(
                                    channels));

                        for (int channel = 0;
                             channel < channels;
                             ++channel)
                        {
                            outputPointers[
                                static_cast<
                                    size_t>(
                                    channel)] =
                                stretchedAtSourceRate
                                    .getWritePointer(
                                        channel);
                        }

                        stretch.exact(
                            inputPointers.data(),
                            musicalSamples,
                            outputPointers.data(),
                            stretchedSamplesAtSourceRate);

                        const double
                            resampleRatio =
                                sourceRate
                                / targetSampleRate;

                        const int hostSamples =
                            std::max(
                                1,
                                static_cast<int>(
                                    std::llround(
                                        stretchedSamplesAtSourceRate
                                        * targetSampleRate
                                        / sourceRate)));

                        result.buffer.setSize(
                            channels,
                            hostSamples);

                        for (int channel = 0;
                             channel < channels;
                             ++channel)
                        {
                            juce::LagrangeInterpolator
                                interpolator;

                            interpolator.process(
                                resampleRatio,
                                stretchedAtSourceRate
                                    .getReadPointer(
                                        channel),
                                result.buffer
                                    .getWritePointer(
                                        channel),
                                hostSamples);
                        }

                        result.success =
                            result.buffer
                                    .getNumSamples()
                                > 0;

                        return result;
                    });
        }

        void finishPreviewBuildIfReady()
        {
            if (!previewBuildRunning
                || !previewBuildFuture.valid())
            {
                return;
            }

            const auto status =
                previewBuildFuture.wait_for(
                    std::chrono::milliseconds(
                        0));

            if (status
                != std::future_status::ready)
            {
                return;
            }

            HostPreviewBuildResult result =
                previewBuildFuture.get();

            previewBuildRunning =
                false;

            if (!result.success)
            {
                if (!previewBuildPending)
                    stretching = false;

                return;
            }

            const bool stillCurrent =
                std::abs(
                    result.bpm
                    - hostBpm)
                    <= 0.01
                && std::abs(
                       result.sampleRate
                       - hostSampleRate)
                       <= 0.5;

            if (stillCurrent)
            {
                hostPreviewBuffer =
                    std::move(
                        result.buffer);

                stretchedForHostBpm =
                    result.bpm;

                stretchedForHostSampleRate =
                    result.sampleRate;

                writeHostPreviewCache();
            }

            if (previewBuildPending)
            {
                stretching =
                    true;
            }
            else if (!stillCurrent)
            {
                scheduleHostPreviewBuild(
                    false);
            }
            else
            {
                stretching =
                    false;
            }

            repaint();
        }

        void writeHostPreviewCache()
        {
            if (hostPreviewBuffer
                    .getNumSamples()
                <= 0
                || stretchedForHostSampleRate
                       <= 0.0)
            {
                return;
            }

            const auto cacheDirectory =
                juce::File::
                    getSpecialLocation(
                        juce::File::
                            tempDirectory)
                    .getChildFile(
                        "LoopBridge");

            if (!cacheDirectory.exists())
            {
                cacheDirectory
                    .createDirectory();
            }

            const auto cacheFile =
                cacheDirectory
                    .getChildFile(
                        "preview.wav");

            cacheFile.deleteFile();

            juce::WavAudioFormat
                wavFormat;

            std::unique_ptr<
                juce::FileOutputStream>
                outputStream(
                    cacheFile
                        .createOutputStream());

            if (outputStream == nullptr)
                return;

            std::unique_ptr<
                juce::AudioFormatWriter>
                writer(
                    wavFormat
                        .createWriterFor(
                            outputStream.get(),
                            stretchedForHostSampleRate,
                            static_cast<
                                unsigned int>(
                                hostPreviewBuffer
                                    .getNumChannels()),
                            32,
                            {},
                            0));

            if (writer == nullptr)
                return;

            outputStream.release();

            const bool wrote =
                writer
                    ->writeFromAudioSampleBuffer(
                        hostPreviewBuffer,
                        0,
                        hostPreviewBuffer
                            .getNumSamples());

            writer.reset();

            if (!wrote)
                return;

            sendLoadCommand(
                cacheFile);

            sendBridgeState();

            if (haveHostState)
            {
                sendRephaseCommand(
                    hostPpq);
            }

            hostPreviewReady =
                true;
        }

        void sendBridgeState()
        {
            const juce::String message =
                bridgeEnabled
                    ? "BRIDGE:1"
                    : "BRIDGE:0";

            sendSocket.write(
                "127.0.0.1",
                49153,
                message.toRawUTF8(),
                static_cast<int>(
                    message
                        .getNumBytesAsUTF8()));
        }

        void sendLoadCommand(
            const juce::File& file)
        {
            const juce::String message =
                "LOAD:"
                + file.getFullPathName();

            sendSocket.write(
                "127.0.0.1",
                49153,
                message.toRawUTF8(),
                static_cast<int>(
                    message
                        .getNumBytesAsUTF8()));
        }

        void sendRephaseCommand(
            double absolutePpq)
        {
            if (!bridgeEnabled)
                return;

            double loopPhase =
                std::fmod(
                    absolutePpq,
                    loopBeats);

            if (loopPhase < 0.0)
            {
                loopPhase +=
                    loopBeats;
            }

            const juce::String message =
                "SEEK:"
                + juce::String(
                    loopPhase,
                    9);

            sendSocket.write(
                "127.0.0.1",
                49153,
                message.toRawUTF8(),
                static_cast<int>(
                    message
                        .getNumBytesAsUTF8()));

            juce::Logger::writeToLog(
                "LOOPBRIDGE REPHASE -> PPQ "
                + juce::String(
                    absolutePpq,
                    6)
                + " | LOOP PHASE "
                + juce::String(
                    loopPhase,
                    6));
        }

        void startSoloPreview()
        {
            if (soloBuffer
                    .getNumSamples()
                <= 0)
            {
                return;
            }

            {
                const juce::ScopedLock lock(
                    audioLock);

                soloPlaybackPosition =
                    0;

                soloPlaying =
                    true;
            }

            playButton.setEnabled(
                false);

            stopButton.setEnabled(
                true);

            repaint();
        }

        void stopSoloPreview()
        {
            {
                const juce::ScopedLock lock(
                    audioLock);

                soloPlaying =
                    false;

                soloPlaybackPosition =
                    0;
            }

            playButton.setEnabled(
                sourceMusicalSamples > 0);

            stopButton.setEnabled(
                false);

            repaint();
        }

        double getEstimatedPpq() const
        {
            if (!haveHostState)
                return hostPpq;

            if (!hostPlaying
                || hostBpm <= 0.0)
            {
                return hostPpq;
            }

            const double nowMs =
                juce::Time::
                    getMillisecondCounterHiRes();

            const double elapsedSeconds =
                (nowMs
                 - hostStateReceivedMs)
                / 1000.0;

            return hostPpq
                + elapsedSeconds
                      * (hostBpm / 60.0);
        }

        void processBridgeMessage(
            const juce::String& message)
        {
            if (!message.startsWith(
                    "BPM:"))
            {
                return;
            }

            double newBpm =
                hostBpm;

            bool newPlaying =
                hostPlaying;

            double newPpq =
                hostPpq;

            double newHostSampleRate =
                hostSampleRate;

            juce::StringArray parts;

            parts.addTokens(
                message,
                ";",
                "");

            for (const auto& part : parts)
            {
                if (part.startsWith(
                        "BPM:"))
                {
                    newBpm =
                        part
                            .fromFirstOccurrenceOf(
                                "BPM:",
                                false,
                                false)
                            .getDoubleValue();
                }
                else if (
                    part.startsWith(
                        "PLAYING:"))
                {
                    newPlaying =
                        part
                            .fromFirstOccurrenceOf(
                                "PLAYING:",
                                false,
                                false)
                            .getIntValue()
                        == 1;
                }
                else if (
                    part.startsWith(
                        "PPQ:"))
                {
                    newPpq =
                        part
                            .fromFirstOccurrenceOf(
                                "PPQ:",
                                false,
                                false)
                            .getDoubleValue();
                }
                else if (
                    part.startsWith(
                        "SR:"))
                {
                    newHostSampleRate =
                        part
                            .fromFirstOccurrenceOf(
                                "SR:",
                                false,
                                false)
                            .getDoubleValue();
                }
            }

            const bool
                hadPreviousHostState =
                    haveHostState;

            const bool wasPlaying =
                hostPlaying;

            double expectedPpq =
                hostPpq;

            if (hadPreviousHostState
                && wasPlaying
                && hostBpm > 0.0)
            {
                const double nowMs =
                    juce::Time::
                        getMillisecondCounterHiRes();

                const double elapsedSeconds =
                    (nowMs
                     - hostStateReceivedMs)
                    / 1000.0;

                expectedPpq +=
                    elapsedSeconds
                    * (hostBpm / 60.0);
            }

            const bool playbackStarted =
                hadPreviousHostState
                && !wasPlaying
                && newPlaying;

            const bool playbackJumped =
                hadPreviousHostState
                && wasPlaying
                && newPlaying
                && std::abs(
                       newPpq
                       - expectedPpq)
                       > ppqDiscontinuityThreshold;

            const bool bpmChanged =
                newBpm > 0.0
                && std::abs(
                       newBpm
                       - hostBpm)
                       > 0.01;

            const bool sampleRateChanged =
                newHostSampleRate > 0.0
                && std::abs(
                       newHostSampleRate
                       - hostSampleRate)
                       > 0.5;

            hostBpm =
                newBpm;

            hostPlaying =
                newPlaying;

            hostPpq =
                newPpq;

            hostSampleRate =
                newHostSampleRate;

            hostStateReceivedMs =
                juce::Time::
                    getMillisecondCounterHiRes();

            haveHostState =
                true;

            if (playbackStarted
                || playbackJumped)
            {
                sendRephaseCommand(
                    newPpq);
            }

            const bool needsInitialPreview =
                !hostPreviewReady
                && !previewBuildPending
                && !previewBuildRunning;

            if (sourceMusicalSamples > 0
                && (bpmChanged
                    || sampleRateChanged
                    || needsInitialPreview))
            {
                scheduleHostPreviewBuild(
                    false);
            }
        }

        void timerCallback() override
        {
            while (true)
            {
                char buffer[256] {};

                const int bytesRead =
                    receiveSocket.read(
                        buffer,
                        sizeof(buffer) - 1,
                        false);

                if (bytesRead <= 0)
                    break;

                buffer[bytesRead] =
                    '\0';

                processBridgeMessage(
                    juce::String::fromUTF8(
                        buffer,
                        bytesRead));
            }

            finishPreviewBuildIfReady();

            if (previewBuildPending
                && !previewBuildRunning)
            {
                const double nowMs =
                    juce::Time::
                        getMillisecondCounterHiRes();

                if (nowMs
                        - previewBuildRequestedMs
                    >= previewDebounceMs)
                {
                    startPendingPreviewBuild();
                }
            }

            repaint();
        }

        juce::DatagramSocket
            receiveSocket;

        juce::DatagramSocket
            sendSocket;

        bool listening =
            false;

        bool bridgeEnabled =
            true;

        double hostBpm =
            0.0;

        bool hostPlaying =
            false;

        double hostPpq =
            0.0;

        double hostSampleRate =
            0.0;

        double sourceSampleRate =
            0.0;

        double deviceSampleRate =
            0.0;

        bool haveHostState =
            false;

        double hostStateReceivedMs =
            0.0;

        bool stretching =
            false;

        bool hostPreviewReady =
            false;

        bool soloPlaying =
            false;

        double stretchedForHostBpm =
            0.0;

        double stretchedForHostSampleRate =
            0.0;

        bool previewBuildPending =
            false;

        bool previewBuildRunning =
            false;

        double previewBuildRequestedMs =
            0.0;

        double pendingPreviewBpm =
            0.0;

        double pendingPreviewSampleRate =
            0.0;

        std::future<
            HostPreviewBuildResult>
            previewBuildFuture;

        int sourceMusicalSamples =
            0;

        int soloPlaybackPosition =
            0;

        juce::AudioFormatManager
            formatManager;

        juce::AudioBuffer<float>
            sourceBuffer;

        juce::AudioBuffer<float>
            soloBuffer;

        juce::AudioBuffer<float>
            hostPreviewBuffer;

        juce::CriticalSection
            audioLock;

        std::unique_ptr<
            juce::FileChooser>
            fileChooser;

        juce::TextButton
            loadButton;

        juce::TextButton
            playButton;

        juce::TextButton
            stopButton;

        juce::TextButton
            bridgeButton;

        juce::TextButton
            gmailButton;

        GmailClient
            gmailClient;

        juce::String gmailStatus =
            "GMAIL: NOT CONNECTED";

        juce::String
            loadedFileName;

        std::vector<LoopItem>
            loopItems;

        int selectedLoopIndex =
            -1;

        int browserScrollIndex =
            0;
    };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(
        LoopBridgeWindow)
};


class LoopBridgeApplication
    : public juce::JUCEApplication
{
public:
    const juce::String
    getApplicationName() override
    {
        return "LoopBridge";
    }

    const juce::String
    getApplicationVersion() override
    {
        return "0.1.0";
    }

    void initialise(
        const juce::String&) override
    {
        mainWindow =
            std::make_unique<
                LoopBridgeWindow>();
    }

    void shutdown() override
    {
        mainWindow.reset();
    }

private:
    std::unique_ptr<
        LoopBridgeWindow>
        mainWindow;
};


START_JUCE_APPLICATION(
    LoopBridgeApplication)