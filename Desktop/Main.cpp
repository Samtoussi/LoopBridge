#include <JuceHeader.h>

#include "GmailClient.h"
#include "LoopItem.h"
#include "MetadataParser.h"
#include "PreviewRenderer.h"

#include <signalsmith-stretch/signalsmith-stretch.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <future>
#include <limits>
#include <memory>
#include <map>
#include <set>
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

            volumeSlider.setRange(0.0, 100.0, 1.0);
            volumeSlider.setValue(100.0, juce::dontSendNotification);
            volumeSlider.setTextValueSuffix("%");
            volumeSlider.setSliderStyle(juce::Slider::LinearHorizontal);
            volumeSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 65, 24);
            volumeSlider.setName("Preview volume");
            volumeSlider.setTooltip("Preview volume");
            volumeSlider.onValueChange = [this]
            {
                {
                    const juce::ScopedLock lock(audioLock);
                    previewVolume = static_cast<float>(volumeSlider.getValue() / 100.0);
                }
                sendPreviewState();
            };
            addAndMakeVisible(volumeSlider);
            juce::PropertiesFile::Options options;
            options.applicationName = "LoopBridge";
            options.filenameSuffix = ".settings";
            options.osxLibrarySubFolder = "Application Support";
            settings = std::make_unique<juce::PropertiesFile>(options);
            projectKeyId = juce::jlimit(1, 24, settings->getIntValue("projectKey", 1));
            const char* notes[] = { "C", "C#/Db", "D", "D#/Eb", "E", "F", "F#/Gb", "G", "G#/Ab", "A", "A#/Bb", "B" };
            for (int mode = 0; mode < 2; ++mode)
                for (int note = 0; note < 12; ++note)
                    projectKeySelector.addItem(juce::String(notes[note]) + (mode ? " minor" : " major"), mode * 12 + note + 1);
            projectKeySelector.setSelectedId(projectKeyId, juce::dontSendNotification);
            projectKeySelector.setTooltip("Project key");
            keySyncButton.setButtonText("Key Sync");
            keySyncButton.setToggleState(settings->getBoolValue("keySync", false), juce::dontSendNotification);
            pitchMinus.setButtonText("-");
            pitchPlus.setButtonText("+");
            pitchMinus.setWantsKeyboardFocus(false);
            pitchPlus.setWantsKeyboardFocus(false);
            keySyncButton.setWantsKeyboardFocus(false);
            projectKeySelector.onChange = [this]
            {
                projectKeyId = projectKeySelector.getSelectedId();
                keySettingsChanged(true);
                grabKeyboardFocus();
            };
            keySyncButton.onClick = [this] { keySettingsChanged(true); grabKeyboardFocus(); };
            pitchMinus.onClick = [this] { adjustManualPitch(-1); };
            pitchPlus.onClick = [this] { adjustManualPitch(1); };
            addAndMakeVisible(projectKeySelector);
            addAndMakeVisible(keySyncButton);
            addAndMakeVisible(pitchMinus);
            addAndMakeVisible(pitchPlus);
            addAndMakeVisible(pitchLabel);
            addAndMakeVisible(keyStatusLabel);
            updatePitchControls();

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
                    if (pendingBrowserPreview.isNotEmpty())
                    {
                        cancelPendingBrowserPreview();
                        hostPreviewEnabled = false;
                        stopSoloPreview();
                        sendPreviewState();
                        return;
                    }
                    if (bridgeEnabled)
                    {
                        cancelPendingBrowserPreview();
                        hostPreviewEnabled = !hostPreviewEnabled;
                        stopSoloPreview();
                        sendPreviewState();
                    }
                    else if (soloPreviewIntent) pauseSoloPreview();
                    else startSoloPreview();
                };

            stopButton.onClick =
                [this]
                {
                    cancelPendingBrowserPreview();
                    if (bridgeEnabled)
                    {
                        hostPreviewEnabled = false;
                        sendPreviewState();
                    }
                    else
                        stopSoloPreview();
                };

            bridgeButton.onClick =
                [this]
                {
                    bridgeEnabled =
                        !bridgeEnabled;
                    stopSoloPreview();

                    bridgeButton.setButtonText(
                        bridgeEnabled
                            ? "BRIDGE ON"
                            : "BRIDGE OFF");

                    rebuildSoloBuffer();
                    sendBridgeState();
                    sendPreviewState();

                    repaint();
                };

            gmailButton.onClick =
                [this]
                {
                    if (gmailClient.getState()
                        == GmailClient::State::connected)
                    {
                        gmailClient.disconnect();
                        ++sourceGeneration;
                        nearbyRenders.clear();
                        nearbyRenderCentre = -1;
                        previewBuildPending = false;
                        stretching = false;
                        pendingPrefetchCentre = -1;
                        cancelPendingBrowserPreview();
                        downloadsInFlight.clear();
                        loadedBrowserLoopIndex = -1;

                        gmailButton.setButtonText(
                            "CONNECT GMAIL");

                        gmailStatus =
                            "GMAIL: NOT CONNECTED";

                        // Keep known rows available for local/offline preview.

                        repaint();
                        return;
                    }

                    cancelPendingBrowserPreview();
                    downloadsInFlight.clear();
                    pendingPrefetchCentre = -1;
                    gmailClient.discardQueuedNavigation();
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

                                startGmailSync();
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

            gmailClient.restoreLibrary([this](const GmailClient::LibraryUpdate& update)
            {
                mergeGmailLibrary(update);
                if (!loopItems.empty())
                    gmailStatus = "GMAIL: " + juce::String(static_cast<int>(loopItems.size()))
                        + " KNOWN AUDIO FILES (OFFLINE)";
                repaint();
            });

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
            gmailClient.shutdown();

            if (previewBuildFuture.valid())
            {
                previewBuildFuture.wait();
            }

            shutdownAudio();
            preparedDirectory.deleteRecursively();
        }

        void prepareToPlay(
            int,
            double newSampleRate) override
        {
            deviceSampleRate =
                newSampleRate;

            const juce::Component::SafePointer<MainComponent> safeThis(this);
            juce::MessageManager::callAsync([safeThis]
            {
                if (safeThis != nullptr) safeThis->rebuildSoloBuffer();
            });
            const juce::ScopedLock lock(audioLock);
            soloGain.reset(newSampleRate, 0.02);
            soloGain.setCurrentAndTargetValue(previewVolume);

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

            if (!soloBuffer || soloBuffer->getNumSamples() <= 0)
                return;

            const int loopSamples =
                soloBuffer->getNumSamples();

            const int sourceChannels =
                soloBuffer->getNumChannels();

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
                        *soloBuffer,
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
            soloGain.setTargetValue(previewVolume);
            for (int sample = 0; sample < info.numSamples; ++sample)
            {
                const float gain = soloGain.getNextValue();
                for (int channel = 0; channel < outputChannels; ++channel)
                    info.buffer->getWritePointer(channel, info.startSample)[sample] *= gain;
            }
        }

        bool keyPressed(
            const juce::KeyPress& key) override
        {
            if (loopItems.empty())
                return false;

            if (key == juce::KeyPress::downKey)
            {
                selectLoop(std::min(
                    selectedLoopIndex + 1,
                    static_cast<int>(loopItems.size()) - 1));
                previewSelectedLoop();
                return true;
            }

            if (key == juce::KeyPress::upKey)
            {
                selectLoop(std::max(selectedLoopIndex - 1, 0));
                previewSelectedLoop();
                return true;
            }

            if (key == juce::KeyPress::homeKey)
            {
                selectLoop(0);
                return true;
            }

            if (key == juce::KeyPress::endKey)
            {
                selectLoop(static_cast<int>(loopItems.size()) - 1);
                return true;
            }

            if (key.getKeyCode() == ' ')
            {
                toggleSelectedLoopPreview();
                return true;
            }

            if (key == juce::KeyPress::returnKey)
            {
                loadSelectedGmailLoop();
                return true;
            }

            return false;
        }

        void mouseDown(
            const juce::MouseEvent& event) override
        {
            dragCandidateIndex = -1;

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
                selectLoop(itemIndex);

                const int downloadLeft =
                    browserLeft + getWidth()
                    - browserLeft * 2 - 42;

                if (event.x >= downloadLeft
                    && event.x < downloadLeft + 30)
                {
                    downloadSelectedLoop();
                    grabKeyboardFocus();
                    return;
                }

                const int playButtonLeft =
                    browserLeft + 10;

                const int playButtonRight =
                    playButtonLeft + 28;

                if (event.x >= playButtonLeft
                    && event.x < playButtonRight)
                {
                    toggleSelectedLoopPreview();
                }
                else if (event.mods.isLeftButtonDown()
                         && getDownloadedLoopFile(
                             loopItems[static_cast<size_t>(itemIndex)])
                             .existsAsFile())
                {
                    dragCandidateIndex = itemIndex;
                }
            }

            grabKeyboardFocus();
        }

        void mouseDrag(
            const juce::MouseEvent& event) override
        {
            if (dragCandidateIndex < 0
                || event.getDistanceFromDragStart() < 5)
                return;

            const int index = dragCandidateIndex;
            dragCandidateIndex = -1;

            if (index >= static_cast<int>(loopItems.size()))
                return;

            const auto file = getDownloadedLoopFile(
                loopItems[static_cast<size_t>(index)]);

            if (!file.existsAsFile())
                return;

            juce::StringArray files;
            files.add(file.getFullPathName());

            juce::DragAndDropContainer::performExternalDragDropOfFiles(
                files, false, this);
        }

        void mouseUp(
            const juce::MouseEvent&) override
        {
            dragCandidateIndex = -1;
        }

        void mouseWheelMove(
            const juce::MouseEvent& event,
            const juce::MouseWheelDetails& wheel) override
        {
            if (loopItems.empty())
                return;

            const int rowsTop =
                browserTop
                + browserHeaderHeight;

            const int rowsBottom =
                rowsTop
                + browserVisibleRows
                      * browserRowHeight;

            if (event.y < browserTop
                || event.y >= rowsBottom)
            {
                return;
            }

            const int maxScroll =
                std::max(
                    0,
                    static_cast<int>(
                        loopItems.size())
                        - browserVisibleRows);

            if (maxScroll <= 0)
                return;

            int scrollDelta = 0;

            if (wheel.deltaY < 0.0f)
                scrollDelta = 1;
            else if (wheel.deltaY > 0.0f)
                scrollDelta = -1;

            if (scrollDelta == 0)
                return;

            browserScrollIndex =
                juce::jlimit(
                    0,
                    maxScroll,
                    browserScrollIndex
                        + scrollDelta);

            repaint();
            grabKeyboardFocus();
        }

        void mouseDoubleClick(
            const juce::MouseEvent& event) override
        {
            if (loopItems.empty())
                return;

            const int rowsTop =
                browserTop
                + browserHeaderHeight;

            if (event.y < rowsTop
                || event.y
                       >= rowsTop
                              + browserVisibleRows
                                    * browserRowHeight)
            {
                return;
            }

            const int visibleRow =
                (event.y - rowsTop)
                / browserRowHeight;

            const int itemIndex =
                browserScrollIndex
                + visibleRow;

            if (itemIndex < 0
                || itemIndex
                       >= static_cast<int>(
                           loopItems.size()))
            {
                return;
            }

            const int downloadLeft =
                browserLeft + getWidth()
                - browserLeft * 2 - 42;

            if (event.x >= downloadLeft
                && event.x < downloadLeft + 30)
                return;

            selectLoop(
                itemIndex);

            loadSelectedGmailLoop();

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
                sourceBpm > 0.0
                    ? "SOURCE: "
                          + juce::String(sourceBpm, 0)
                          + " BPM / 4 BARS"
                    : "SOURCE: BPM UNKNOWN / 4 BARS";

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
                          deviceSampleRate.load(),
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

            if (bridgeEnabled && !isHostConnected())
            {
                g.setColour(juce::Colours::orange);
                g.drawFittedText("BRIDGE DISCONNECTED / WAITING FOR VST",
                    0, 287, getWidth(), 22, juce::Justification::centred, 1);
            }
            else if (renderError.isNotEmpty())
            {
                g.setColour(juce::Colours::orange);
                g.drawFittedText(renderError, 0, 287, getWidth(), 22, juce::Justification::centred, 1);
            }
            else if (stretching)
            {
                g.setColour(
                    juce::Colours::orange);

                g.drawFittedText(
                    "PREPARING PREVIEW...",
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
                    sourceBpm > 0.0
                        ? "SOLO PREVIEW / ORIGINAL "
                              + juce::String(sourceBpm, 0)
                              + " BPM"
                        : "SOLO PREVIEW / BPM UNKNOWN",
                    0,
                    287,
                    getWidth(),
                    22,
                    juce::Justification::centred,
                    1);
            }
            else if (bridgeEnabled)
            {
                g.setColour(
                    bridgeEnabled
                        ? juce::Colours::lightgreen
                        : juce::Colours::white
                              .withAlpha(0.55f));

                g.drawFittedText(
                    !isHostConnected() ? "BRIDGE DISCONNECTED / WAITING FOR VST"
                        : !hostPreviewEnabled ? "HOST PREVIEW PAUSED"
                        : !hostPreviewReady ? "WAITING FOR HOST PREVIEW"
                        : !hostPlaying ? "WAITING FOR FL TRANSPORT"
                        : "HOST PREVIEW -> FL MIXER",
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

            if (sourceTotalSamples > 0
                && sourceSampleRate > 0.0)
            {
                const double fileSeconds =
                    sourceTotalSamples
                    / sourceSampleRate;

                juce::String lengthText =
                    "File: "
                    + juce::String(
                        fileSeconds,
                        3)
                    + "s";

                if (sourceBpm > 0.0)
                {
                    const double musicalSeconds =
                        loopBeats
                        * 60.0
                        / sourceBpm;

                    lengthText +=
                        " / Musical: "
                        + juce::String(
                            musicalSeconds,
                            3)
                        + "s";
                }
                else
                {
                    lengthText +=
                        " / Musical: BPM unknown";
                }

                if (hostPreviewBuffer && hostPreviewBuffer->getNumSamples() > 0
                    && stretchedForHostSampleRate
                           > 0.0)
                {
                    const double
                        stretchedSeconds =
                            hostPreviewBuffer->getNumSamples()
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
            volumeSlider.setBounds(15, 460, 235, 40);
            projectKeySelector.setBounds(35, 553, 175, 28);
            keySyncButton.setBounds(225, 553, 95, 28);
            pitchMinus.setBounds(345, 553, 28, 28);
            pitchLabel.setBounds(377, 553, 95, 28);
            pitchPlus.setBounds(476, 553, 28, 28);
            keyStatusLabel.setBounds(525, 553, getWidth() - 550, 28);
        }

    private:
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

        using HostPreviewBuildResult = PreviewRenderer::Result;
        struct CachedPreview
        {
            HostPreviewBuildResult result;
            uint64_t lastUse = 0;
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

        juce::String getLoopCacheKey(
            const LoopItem& item) const
        {
            return item.messageId
                + "|"
                + item.attachmentId;
        }

        void mergeGmailLibrary(const GmailClient::LibraryUpdate& update)
        {
            if (update.account.isEmpty())
                return;
            if (libraryAccount.isNotEmpty() && libraryAccount != update.account)
            {
                // An authenticated account switch must not retain the old rows
                // or pending browser work. Same-account sync never takes this path.
                cancelPendingBrowserPreview();
                gmailClient.discardQueuedNavigation();
                ++sourceGeneration;
                nearbyRenders.clear();
                nearbyRenderCentre = -1;
                previewBuildPending = false;
                stretching = false;
                pendingPrefetchCentre = -1;
                downloadsInFlight.clear();
                loadedBrowserLoopIndex = -1;
                loopItems.clear();
                selectedLoopIndex = -1;
                browserScrollIndex = 0;
                gmailLoopCache.clear();
                gmailLoopDurations.clear();
            }
            libraryAccount = update.account;
            std::set<juce::String> known;
            for (const auto& item : loopItems)
                known.insert(getLoopCacheKey(item));
            for (const auto& attachment : update.attachments)
            {
                if (!known.insert(attachment.messageId + "|" + attachment.attachmentId).second)
                    continue;
                LoopItem item;
                item.messageId = attachment.messageId;
                item.attachmentId = attachment.attachmentId;
                item.filename = attachment.filename;
                item.subject = attachment.subject;
                parseSender(attachment.sender, item.senderName, item.senderEmail);
                MetadataParser::parse(item);
                // Append instead of replacing/reordering: indices captured by
                // active and pending previews, selection, and prefetch stay valid.
                loopItems.push_back(std::move(item));
            }
            if (selectedLoopIndex < 0 && !loopItems.empty())
                selectedLoopIndex = 0;
        }

        void startGmailSync()
        {
            gmailClient.fetchRecentAudioAttachments(100, [this](const GmailClient::LibraryUpdate& update)
            {
                mergeGmailLibrary(update);
                nextGmailSyncMs = juce::Time::getMillisecondCounterHiRes() + update.nextSyncDelayMs;
                if (update.error.isNotEmpty())
                {
                    juce::Logger::writeToLog("GMAIL SYNC: " + update.error);
                    gmailStatus = "GMAIL: SYNC PAUSED; KNOWN FILES AVAILABLE";
                }
                else
                    gmailStatus = "GMAIL: " + juce::String(static_cast<int>(loopItems.size()))
                        + " AUDIO FILES";
                repaint();
            });
        }

        juce::File getLoopCacheDirectory() const
        {
            auto directory =
                juce::File::getSpecialLocation(
                    juce::File::tempDirectory)
                    .getChildFile("LoopBridge")
                    .getChildFile("gmail-loops");

            return directory;
        }

        juce::File getDownloadedLoopDirectory(
            const LoopItem& item) const
        {
            return juce::File::getSpecialLocation(
                       juce::File::userDocumentsDirectory)
                .getChildFile("LoopBridge")
                .getChildFile("Downloads")
                .getChildFile(juce::String::toHexString(
                    getLoopCacheKey(item).hashCode64()));
        }

        juce::File getDownloadedLoopFile(
            const LoopItem& item) const
        {
            const auto directory =
                getDownloadedLoopDirectory(item);

            juce::Array<juce::File> files;
            directory.findChildFiles(
                files, juce::File::findFiles, false);

            for (const auto& file : files)
                if (file.getSize() > 0)
                    return file;
            return {};
        }

        void acquireLoop(const LoopItem& item, GmailClient::Priority priority,
                         GmailClient::DownloadCallback callback)
        {
            const auto key = getLoopCacheKey(item);
            const auto saved = getDownloadedLoopFile(item);
            const auto cached = gmailLoopCache.find(key);
            const auto local = saved.existsAsFile() ? saved
                : cached != gmailLoopCache.end() ? cached->second : juce::File{};
            const auto session = gmailClient.getSessionGeneration();
            const juce::Component::SafePointer<MainComponent> safeThis(this);
            gmailClient.downloadAudioAttachment(item.messageId, item.attachmentId, item.filename,
                getLoopCacheDirectory(),
                [safeThis, session, key, callback](const juce::File& file, const juce::String& error)
                {
                    if (safeThis == nullptr
                        || safeThis->gmailClient.getSessionGeneration() != session)
                        return;
                    if (error.isEmpty() && file.existsAsFile())
                    {
                        safeThis->gmailLoopCache[key] = file;
                        safeThis->rememberLoopDuration(key, file);
                    }
                    callback(file, error);
                }, priority, local);
        }

        void downloadSelectedLoop()
        {
            if (selectedLoopIndex < 0
                || selectedLoopIndex >= static_cast<int>(loopItems.size()))
                return;

            const auto item =
                loopItems[static_cast<size_t>(selectedLoopIndex)];
            const auto key = getLoopCacheKey(item);

            if (getDownloadedLoopFile(item).existsAsFile()
                || downloadsInFlight.count(key) > 0)
                return;

            const auto directory = getDownloadedLoopDirectory(item);

            downloadsInFlight.insert(key);
            repaint();

            const auto finish = [this, item, key](
                const juce::File& file, const juce::String& error)
            {
                downloadsInFlight.erase(key);

                if (error.isEmpty() && file.existsAsFile())
                    gmailStatus = "DOWNLOADED: " + item.filename;
                else
                    gmailStatus = "DOWNLOAD ERROR: "
                        + (error.isNotEmpty() ? error : "FILE NOT FOUND");

                repaint();
            };

            acquireLoop(item, GmailClient::Priority::download,
                [this, directory, finish](
                    const juce::File& file,
                    const juce::String& error)
                {
                    if (error.isNotEmpty() || !file.existsAsFile())
                    {
                        finish(file, error);
                        return;
                    }

                    gmailClient.saveAttachment(file, directory, finish);
                });
        }

        double getAudioDurationSeconds(
            const juce::File& file)
        {
            std::unique_ptr<juce::AudioFormatReader> reader(
                formatManager.createReaderFor(file));

            if (reader == nullptr
                || reader->sampleRate <= 0.0)
            {
                return 0.0;
            }

            return static_cast<double>(reader->lengthInSamples)
                / reader->sampleRate;
        }

        void rememberLoopDuration(
            const juce::String& cacheKey,
            const juce::File& file)
        {
            if (gmailLoopDurations.count(cacheKey) > 0)
                return;
            const double seconds =
                getAudioDurationSeconds(file);

            if (seconds > 0.0)
            {
                gmailLoopDurations[cacheKey] = seconds;
                repaint();
            }
        }

        static juce::String formatDuration(
            double seconds)
        {
            if (seconds <= 0.0)
                return "--";

            const int totalSeconds =
                static_cast<int>(std::llround(seconds));

            const int minutes = totalSeconds / 60;
            const int remainingSeconds = totalSeconds % 60;

            return juce::String(minutes)
                + ":"
                + juce::String(remainingSeconds).paddedLeft('0', 2);
        }

        void prefetchLoop(
            int index)
        {
            if (index < 0
                || index >= static_cast<int>(loopItems.size()))
                return;

            const auto item =
                loopItems[static_cast<size_t>(index)];

            acquireLoop(item, GmailClient::Priority::prefetch,
                [this](const juce::File& file, const juce::String& error)
                {
                    if (error.isEmpty() && file.existsAsFile())
                    {
                        nearbyRenderCentre = selectedLoopIndex;
                        nearbyRenderRequestedMs = juce::Time::getMillisecondCounterHiRes();
                    }
                });
        }

        void prefetchAroundLoop(
            int centreIndex)
        {
            // Wait for navigation to settle, then fetch one neighbour per
            // timer turn so pending keyboard input can be handled between them.
            pendingPrefetchCentre = centreIndex;
            pendingPrefetchStep = 0;
            prefetchRequestedMs = juce::Time::getMillisecondCounterHiRes();
        }

        void loadSelectedGmailLoop(
            bool autoPlay = false)
        {
            if (selectedLoopIndex < 0
                || selectedLoopIndex
                       >= static_cast<int>(
                           loopItems.size()))
            {
                return;
            }

            const int requestedIndex = selectedLoopIndex;

            const auto item =
                loopItems[
                    static_cast<size_t>(
                        selectedLoopIndex)];

            const auto requestId = ++previewRequestId;
            const auto cacheKey = getLoopCacheKey(item);
            const auto session = gmailClient.getSessionGeneration();
            pendingBrowserPreview = cacheKey;
            pendingBrowserAutoPlay = autoPlay;

            const auto useFile =
                [this, item, requestedIndex, autoPlay, requestId, cacheKey, session](
                    const juce::File& file)
                {
                    if (!isCurrentPreview(requestedIndex, cacheKey, requestId, session)
                        || !file.existsAsFile())
                        return;
                    pendingBrowserPreview.clear();

                    gmailStatus =
                        "GMAIL: LOADED "
                        + item.filename;

                    loadAudioFile(
                        file,
                        item.bpm.has_value()
                            ? *item.bpm
                            : 0.0, item.key, cacheKey);

                    loadedBrowserLoopIndex = requestedIndex;

                    if (autoPlay
                        && requestId == previewRequestId)
                    {
                        startPreview();
                    }

                    prefetchAroundLoop(requestedIndex);

                    grabKeyboardFocus();
                    repaint();
                };

            gmailStatus =
                "GMAIL: DOWNLOADING "
                + item.filename;

            repaint();

            acquireLoop(item, GmailClient::Priority::preview,
                [this, requestedIndex, requestId, cacheKey, session, useFile](
                    const juce::File& file,
                    const juce::String& error)
                {
                    if (!isCurrentPreview(requestedIndex, cacheKey, requestId, session))
                        return;
                    if (error.isNotEmpty())
                    {
                        pendingBrowserPreview.clear();
                        gmailStatus =
                            "GMAIL ERROR: "
                            + error;

                        juce::Logger::writeToLog(
                            "GMAIL DOWNLOAD ERROR: "
                            + error);

                        repaint();
                        return;
                    }

                    if (!file.existsAsFile())
                    {
                        pendingBrowserPreview.clear();
                        gmailStatus =
                            "GMAIL ERROR: DOWNLOADED FILE NOT FOUND";

                        repaint();
                        return;
                    }

                    juce::Logger::writeToLog(
                        "GMAIL LOOP DOWNLOADED: "
                        + file.getFullPathName());

                    useFile(file);
                });
        }

        bool isCurrentPreview(int index, const juce::String& key,
                              uint64_t request, uint64_t session) const
        {
            return request == previewRequestId
                && session == gmailClient.getSessionGeneration()
                && index == selectedLoopIndex && index >= 0
                && index < static_cast<int>(loopItems.size())
                && key == getLoopCacheKey(loopItems[static_cast<size_t>(index)]);
        }

        void cancelPendingBrowserPreview()
        {
            ++previewRequestId;
            pendingBrowserPreview.clear();
        }

        void previewSelectedLoop()
        {
            if (selectedLoopIndex < 0
                || selectedLoopIndex >= static_cast<int>(loopItems.size()))
                return;

            if (loadedBrowserLoopIndex == selectedLoopIndex
                && sourceMusicalSamples > 0)
            {
                startPreview();
                return;
            }

            loadSelectedGmailLoop(true);
        }

        void pauseSoloPreview()
        {
            {
                const juce::ScopedLock lock(audioLock);
                soloPlaying = false;
                soloPreviewIntent = false;
            }

            playButton.setEnabled(sourceMusicalSamples > 0);
            stopButton.setEnabled(soloPlaybackPosition > 0);
            repaint();
        }

        void toggleSelectedLoopPreview()
        {
            if (selectedLoopIndex < 0
                || selectedLoopIndex >= static_cast<int>(loopItems.size()))
                return;

            if (pendingBrowserPreview == getLoopCacheKey(
                    loopItems[static_cast<size_t>(selectedLoopIndex)]))
            {
                cancelPendingBrowserPreview();
                hostPreviewEnabled = false;
                stopSoloPreview();
                sendPreviewState();
                return;
            }

            if (loadedBrowserLoopIndex != selectedLoopIndex
                || sourceMusicalSamples <= 0)
            {
                previewSelectedLoop();
                return;
            }

            if (bridgeEnabled)
            {
                hostPreviewEnabled = !hostPreviewEnabled;
                stopSoloPreview();
                sendPreviewState();
                repaint();
                return;
            }

            if (soloPreviewIntent)
            {
                pauseSoloPreview();
                return;
            }

            startSoloPreview();
        }

        void selectLoop(
            int index)
        {
            pendingPrefetchCentre = -1;
            if (index != selectedLoopIndex)
            {
                cancelPendingBrowserPreview();
                gmailClient.discardQueuedNavigation(index >= 0 && index < static_cast<int>(loopItems.size())
                    ? getLoopCacheKey(loopItems[static_cast<size_t>(index)]) : juce::String{});
                ++sourceGeneration;
                manualSemitones = 0;
                nearbyRenders.clear();
                nearbyRenderCentre = -1;
                previewBuildPending = false;
                stretching = false;
                soloPreviewReady = false;
                appliedRenderKey.clear();
                stopSoloPreview();
                hostPreviewReady = false;
                sendPreviewState();
            }

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
            updatePitchControls();

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

            const int playX =
                browserLeft + 10;

            const int filenameX =
                browserLeft + 46;

            const int senderX =
                browserLeft
                + browserWidth
                - 460;

            const int bpmX =
                browserLeft
                + browserWidth
                - 300;

            const int keyX =
                browserLeft
                + browserWidth
                - 220;

            const int durationX =
                browserLeft
                + browserWidth
                - 132;

            const int downloadX =
                browserLeft + browserWidth - 42;

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

            g.drawText(
                "DURATION",
                durationX,
                browserTop,
                76,
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

                const bool thisLoopPlaying =
                    itemIndex == loadedBrowserLoopIndex
                    && (bridgeEnabled
                        ? isHostConnected() && hostPreviewEnabled && hostPreviewReady && hostPlaying
                        : soloPlaying);

                g.setColour(
                    thisLoopPlaying
                        ? juce::Colours::lightgreen
                        : juce::Colours::cornflowerblue);

                g.setFont(
                    juce::Font(
                        juce::FontOptions(
                            16.0f,
                            juce::Font::bold)));

                g.drawText(
                    thisLoopPlaying ? "||" : ">",
                    playX,
                    rowY,
                    28,
                    browserRowHeight,
                    juce::Justification::centred);

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
                        : juce::Colours::white.withAlpha(0.82f));

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

                const auto durationIt =
                    gmailLoopDurations.find(
                        getLoopCacheKey(item));

                const juce::String durationText =
                    durationIt != gmailLoopDurations.end()
                        ? formatDuration(durationIt->second)
                        : "--";

                g.setColour(
                    durationIt != gmailLoopDurations.end()
                        ? juce::Colours::white.withAlpha(0.72f)
                        : juce::Colours::white.withAlpha(0.3f));

                g.drawText(
                    durationText,
                    durationX,
                    rowY,
                    76,
                    browserRowHeight,
                    juce::Justification::centredLeft);

                const bool downloaded =
                    getDownloadedLoopFile(item).existsAsFile();
                const bool downloading =
                    downloadsInFlight.count(getLoopCacheKey(item)) > 0;

                g.setColour(
                    downloaded ? juce::Colours::lightgreen
                    : downloading ? juce::Colours::orange
                                  : juce::Colours::cornflowerblue);
                g.setFont(juce::Font(juce::FontOptions(
                    18.0f, juce::Font::bold)));
                g.drawText(
                    downloaded ? juce::String::fromUTF8("\xE2\x9C\x93")
                    : downloading ? "..." : "+",
                    downloadX,
                    rowY,
                    30,
                    browserRowHeight,
                    juce::Justification::centred);

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

                    loadedBrowserLoopIndex = -1;
                    pendingPrefetchCentre = -1;
                    gmailClient.discardQueuedNavigation();
                    pendingBrowserPreview.clear();
                    ++previewRequestId;

                    loadAudioFile(
                        file,
                        100.0);
                });
        }

        MusicalKey projectKey() const
        {
            return { (projectKeyId - 1) % 12, projectKeyId > 12 };
        }

        int effectivePitch(const std::optional<MusicalKey>& key, int manual) const
        {
            return MetadataParser::previewSemitones(key, projectKey(), keySyncButton.getToggleState(), manual);
        }

        void updatePitchControls()
        {
            auto key = sourceKey;
            if ((loadedBrowserLoopIndex >= 0 || pendingBrowserPreview.isNotEmpty())
                && selectedLoopIndex >= 0 && selectedLoopIndex < static_cast<int>(loopItems.size()))
                key = loopItems[static_cast<size_t>(selectedLoopIndex)].musicalKey;
            const int pitch = effectivePitch(key, manualSemitones);
            pitchLabel.setText((pitch > 0 ? "+" : "") + juce::String(pitch) + " st", juce::dontSendNotification);
            pitchLabel.setJustificationType(juce::Justification::centred);
            pitchMinus.setEnabled(pitch > -12);
            pitchPlus.setEnabled(pitch < 12);
            keyStatusLabel.setText(!key ? "Source key unknown"
                : keySyncButton.getToggleState() && key->minor != projectKey().minor
                    ? "Mode mismatch: manual only" : "", juce::dontSendNotification);
            keyStatusLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.6f));
            pitchLabel.setTooltip(!key ? "Unknown source key: manual pitch only"
                : keySyncButton.getToggleState() && key->minor != projectKey().minor
                    ? "Major/minor mismatch: manual pitch only" : "Effective preview pitch shift");
        }

        void adjustManualPitch(int direction)
        {
            auto key = sourceKey;
            if ((loadedBrowserLoopIndex >= 0 || pendingBrowserPreview.isNotEmpty())
                && selectedLoopIndex >= 0 && selectedLoopIndex < static_cast<int>(loopItems.size()))
                key = loopItems[static_cast<size_t>(selectedLoopIndex)].musicalKey;
            const int automatic = effectivePitch(key, 0);
            manualSemitones = juce::jlimit(-12, 12, effectivePitch(key, manualSemitones) + direction) - automatic;
            keySettingsChanged(false);
            grabKeyboardFocus();
        }

        void keySettingsChanged(bool persist)
        {
            updatePitchControls();
            if (persist)
            {
                settings->setValue("projectKey", projectKeyId);
                settings->setValue("keySync", keySyncButton.getToggleState());
            }
            ++sourceGeneration;
            nearbyRenders.clear();
            nearbyRenderCentre = selectedLoopIndex;
            nearbyRenderRequestedMs = juce::Time::getMillisecondCounterHiRes();
            scheduleHostPreviewBuild(true);
            repaint();
        }

        PreviewRenderer::Request makeRenderRequest(const juce::File& file,
            const juce::String& identity, double bpm, int pitch) const
        {
            PreviewRenderer::Request request;
            request.source = file;
            request.generation = sourceGeneration;
            request.sourceBpm = bpm;
            request.host = bridgeEnabled;
            request.bpm = request.host ? hostBpm : 0.0;
            request.sampleRate = request.host ? hostSampleRate : deviceSampleRate.load();
            request.semitones = pitch;
            const auto descriptor = PreviewRenderer::cacheDescriptor(request, identity);
            request.key = juce::SHA256(descriptor.toRawUTF8(), descriptor.getNumBytesAsUTF8()).toHexString();
            request.hostFile = preparedDirectory.getChildFile(request.key + ".wav");
            return request;
        }

        PreviewRenderer::Request currentRenderRequest() const
        {
            return makeRenderRequest(originalPreviewFile, originalPreviewIdentity,
                sourceBpm, effectivePitch(sourceKey, manualSemitones));
        }

        bool hasCurrentSource() const
        {
            return sourceMusicalSamples > 0 && originalPreviewFile.existsAsFile()
                && (loadedBrowserLoopIndex < 0
                    || (loadedBrowserLoopIndex == selectedLoopIndex && selectedLoopIndex >= 0
                        && selectedLoopIndex < static_cast<int>(loopItems.size())
                        && originalPreviewIdentity == getLoopCacheKey(loopItems[static_cast<size_t>(selectedLoopIndex)])));
        }

        void loadAudioFile(const juce::File& file, double bpm,
                           const juce::String& key = {}, const juce::String& identity = {})
        {
            ++sourceGeneration;
            previewBuildPending = false;
            nearbyRenders.clear();
            nearbyRenderCentre = -1;
            hostPreviewReady = false;
            soloPreviewReady = false;
            appliedRenderKey.clear();
            renderError.clear();
            stopSoloPreview();
            sendPreviewState();
            std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
            sourceMusicalSamples = 0;
            sourceTotalSamples = 0;
            if (!reader || reader->sampleRate <= 0.0 || reader->lengthInSamples <= 0
                || reader->lengthInSamples > std::numeric_limits<int>::max())
                return;
            originalPreviewFile = file;
            originalPreviewIdentity = identity.isNotEmpty() ? identity : file.getFullPathName();
            sourceKey = MetadataParser::musicalKeyFromLabel(key);
            if (identity.isEmpty())
            {
                LoopItem local;
                local.filename = file.getFileName();
                MetadataParser::parse(local);
                sourceKey = local.musicalKey;
                manualSemitones = 0;
            }
            sourceBpm = bpm;
            sourceSampleRate = reader->sampleRate;
            sourceTotalSamples = static_cast<int>(reader->lengthInSamples);
            sourceMusicalSamples = sourceBpm > 0.0 ? std::min(sourceTotalSamples,
                static_cast<int>(std::llround(loopBeats * 60.0 / sourceBpm * sourceSampleRate))) : sourceTotalSamples;
            loadedFileName = file.getFileName();
            updatePitchControls();
            // The browser callback installs its loaded index immediately after
            // this returns. All decoding/rendering is deferred to the worker.
            previewBuildPending = true;
            previewBuildRequestedMs = juce::Time::getMillisecondCounterHiRes() - previewDebounceMs;
            stretching = true;
            playButton.setEnabled(sourceMusicalSamples > 0);
            repaint();
        }

        void rebuildSoloBuffer()
        {
            nearbyRenders.clear();
            nearbyRenderCentre = selectedLoopIndex;
            nearbyRenderRequestedMs = juce::Time::getMillisecondCounterHiRes();
            scheduleHostPreviewBuild(true);
        }

        void applyPreparedPreview(const HostPreviewBuildResult& result)
        {
            appliedRenderKey = result.request.key;
            renderError.clear();
            if (result.request.host)
            {
                hostPreviewBuffer = result.buffer;
                stretchedForHostBpm = result.request.bpm;
                stretchedForHostSampleRate = result.request.sampleRate;
                activeHostPreviewFile = result.request.hostFile;
                sendLoadCommand(activeHostPreviewFile);
                sendBridgeState();
                if (haveHostState) sendRephaseCommand(hostPpq);
                hostPreviewReady = true;
                sendPreviewState();
            }
            else
            {
                const juce::ScopedLock lock(audioLock);
                soloBuffer = result.buffer;
                soloPlaybackPosition %= soloBuffer->getNumSamples();
                soloPreviewReady = true;
                soloPlaying = soloPreviewIntent && !bridgeEnabled;
                soloGain.setCurrentAndTargetValue(0.0f);
            }
            stretching = false;
            previewBuildPending = false;
            nearbyRenderCentre = selectedLoopIndex;
            nearbyRenderRequestedMs = juce::Time::getMillisecondCounterHiRes();
            repaint();
        }

        void scheduleHostPreviewBuild(bool immediate)
        {
            if (!hasCurrentSource()) return;
            const auto request = currentRenderRequest();
            if (request.key == appliedRenderKey && (request.host ? hostPreviewReady : soloPreviewReady)) return;
            // Never play the old pitch while preparing the new one. Intent is
            // kept separately, so Pause during a render still wins.
            hostPreviewReady = false;
            soloPreviewReady = false;
            {
                const juce::ScopedLock lock(audioLock);
                soloPlaying = false;
            }
            sendPreviewState();
            nearbyRenders.clear();
            renderError.clear();
            if (request.sampleRate <= 0.0 || (request.host && (sourceBpm <= 0.0 || hostBpm <= 0.0)))
            {
                previewBuildPending = false;
                stretching = false;
                return;
            }
            const auto cached = preparedPreviews.find(request.key);
            if (cached != preparedPreviews.end())
            {
                cached->second.lastUse = ++previewCacheClock;
                applyPreparedPreview(cached->second.result);
                return;
            }
            previewBuildPending = true;
            previewBuildRequestedMs = juce::Time::getMillisecondCounterHiRes()
                - (immediate ? previewDebounceMs : 0.0);
            stretching = true;
            if (immediate && !previewBuildRunning) startPendingPreviewBuild();
            repaint();
        }

        void startPendingPreviewBuild()
        {
            if (previewBuildRunning) return;
            PreviewRenderer::Request request;
            if (previewBuildPending)
            {
                if (!hasCurrentSource()) { previewBuildPending = false; stretching = false; return; }
                request = currentRenderRequest();
                if (request.sampleRate <= 0.0 || (request.host && (request.sourceBpm <= 0.0 || request.bpm <= 0.0)))
                { previewBuildPending = false; stretching = false; return; }
                const auto cached = preparedPreviews.find(request.key);
                if (cached != preparedPreviews.end())
                {
                    cached->second.lastUse = ++previewCacheClock;
                    applyPreparedPreview(cached->second.result);
                    return;
                }
                previewBuildPending = false;
            }
            else
            {
                if (nearbyRenders.empty()) return;
                request = nearbyRenders.front();
                nearbyRenders.erase(nearbyRenders.begin());
                if (preparedPreviews.count(request.key) > 0) return;
            }
            previewBuildRunning = true;
            previewBuildFuture = std::async(std::launch::async,
                [request] { return PreviewRenderer::render(request); });
        }

        void trimPreparedCache()
        {
            for (;;)
            {
                int64_t bytes = 0;
                for (const auto& entry : preparedPreviews)
                    bytes += static_cast<int64_t>(entry.second.result.buffer->getNumSamples())
                        * entry.second.result.buffer->getNumChannels() * sizeof(float);
                if (preparedPreviews.size() <= 12 && bytes <= 128 * 1024 * 1024) return;
                auto oldest = preparedPreviews.end();
                for (auto it = preparedPreviews.begin(); it != preparedPreviews.end(); ++it)
                    if (it->first != appliedRenderKey
                        && (oldest == preparedPreviews.end() || it->second.lastUse < oldest->second.lastUse)) oldest = it;
                if (oldest == preparedPreviews.end()) return;
                if (oldest->second.result.request.host && oldest->second.result.request.hostFile != activeHostPreviewFile)
                    oldest->second.result.request.hostFile.deleteFile();
                preparedPreviews.erase(oldest);
            }
        }

        void finishPreviewBuildIfReady()
        {
            if (!previewBuildRunning || !previewBuildFuture.valid()
                || previewBuildFuture.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) return;
            auto result = previewBuildFuture.get();
            previewBuildRunning = false;
            const bool current = hasCurrentSource() && result.request.generation == sourceGeneration
                && result.request.key == currentRenderRequest().key;
            if (result.buffer)
            {
                juce::Logger::writeToLog("PREVIEW RENDER: " + juce::String(result.milliseconds, 1)
                    + " ms / " + juce::String(result.request.semitones) + " st / "
                    + (result.request.host ? "host" : "solo"));
                preparedPreviews[result.request.key] = { result, ++previewCacheClock };
                if (current) applyPreparedPreview(result);
                trimPreparedCache();
            }
            else if (current)
            {
                renderError = result.error;
                stretching = false;
                previewBuildPending = false;
                juce::Logger::writeToLog(renderError);
            }
            repaint();
        }

        void prepareNearbyLoops()
        {
            if (nearbyRenderCentre < 0 || nearbyRenderCentre != selectedLoopIndex
                || previewBuildPending || stretching
                || juce::Time::getMillisecondCounterHiRes() - nearbyRenderRequestedMs < 300.0) return;
            const auto centre = nearbyRenderCentre;
            nearbyRenderCentre = -1;
            for (const int index : { centre + 1, centre - 1 })
            {
                if (index < 0 || index >= static_cast<int>(loopItems.size())) continue;
                const auto& item = loopItems[static_cast<size_t>(index)];
                const auto identity = getLoopCacheKey(item);
                const auto cached = gmailLoopCache.find(identity);
                auto file = getDownloadedLoopFile(item);
                if (!file.existsAsFile() && cached != gmailLoopCache.end()) file = cached->second;
                if (!file.existsAsFile())
                    file = GmailClient::getAttachmentCacheFile(item.messageId, item.attachmentId,
                        item.filename, getLoopCacheDirectory());
                if (!file.existsAsFile()) continue;
                auto request = makeRenderRequest(file, identity, item.bpm.value_or(0.0), effectivePitch(item.musicalKey, 0));
                if (request.sampleRate <= 0.0 || (request.host && (request.sourceBpm <= 0.0 || request.bpm <= 0.0))
                    || preparedPreviews.count(request.key) > 0) continue;
                // Speculative results populate the cache but cannot publish.
                request.generation = std::numeric_limits<uint64_t>::max();
                nearbyRenders.push_back(std::move(request));
            }
        }

        bool isHostConnected() const
        {
            return haveHostState && juce::Time::getMillisecondCounterHiRes()
                - hostStateReceivedMs < 2000.0;
        }

        void sendPreviewState()
        {
            for (const auto& message : {
                     juce::String("PREVIEW:") + (hostPreviewEnabled && hostPreviewReady ? "1" : "0"),
                     juce::String("GAIN:") + juce::String(previewVolume, 6) })
                sendSocket.write("127.0.0.1", 49153, message.toRawUTF8(),
                                 static_cast<int>(message.getNumBytesAsUTF8()));
        }

        void startPreview()
        {
            if (bridgeEnabled)
            {
                stopSoloPreview();
                hostPreviewEnabled = true;
                scheduleHostPreviewBuild(true);
                sendPreviewState();
                repaint();
            }
            else
                startSoloPreview();
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
            if (bridgeEnabled || !hasCurrentSource()) return;
            {
                const juce::ScopedLock lock(audioLock);
                soloPreviewIntent = true;
                soloPlaying = soloPreviewReady && soloBuffer != nullptr;
                if (soloBuffer && soloPlaybackPosition >= soloBuffer->getNumSamples()) soloPlaybackPosition = 0;
            }
            if (!soloPreviewReady) scheduleHostPreviewBuild(true);
            repaint();
        }

        void stopSoloPreview()
        {
            {
                const juce::ScopedLock lock(audioLock);
                soloPlaying = false;
                soloPreviewIntent = false;
                soloPlaybackPosition = 0;
            }
            playButton.setEnabled(sourceMusicalSamples > 0);
            stopButton.setEnabled(false);
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
            bool reconnecting = !isHostConnected();

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
                if (part.startsWith("SESSION:"))
                {
                    const auto session = part.substring(8);
                    reconnecting = reconnecting || session != hostSession;
                    hostSession = session;
                }
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
            if (reconnecting)
            {
                sendBridgeState();
                if (hostPreviewReady)
                    sendLoadCommand(activeHostPreviewFile);
                sendPreviewState();
            }

            if (playbackStarted
                || playbackJumped)
            {
                sendRephaseCommand(
                    newPpq);
            }

            const bool needsInitialPreview =
                bridgeEnabled && !hostPreviewReady && renderError.isEmpty()
                && !previewBuildPending
                && !previewBuildRunning;

            if (bridgeEnabled && sourceMusicalSamples > 0
                && (loadedBrowserLoopIndex < 0 || loadedBrowserLoopIndex == selectedLoopIndex)
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
            if (gmailClient.getState() == GmailClient::State::connected
                && !gmailClient.isLibrarySyncActive()
                && juce::Time::getMillisecondCounterHiRes() >= nextGmailSyncMs)
                startGmailSync();

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
            if (juce::Time::getMillisecondCounterHiRes() - previewStateSentMs >= 500.0)
            {
                sendBridgeState();
                sendPreviewState();
                previewStateSentMs = juce::Time::getMillisecondCounterHiRes();
            }
            if (pendingBrowserPreview.isNotEmpty())
            {
                playButton.setButtonText(pendingBrowserAutoPlay ? "Pause" : "Cancel");
                playButton.setEnabled(true);
                stopButton.setEnabled(true);
            }
            else if (bridgeEnabled)
            {
                playButton.setButtonText(hostPreviewEnabled ? "Pause" : "Play");
                playButton.setEnabled(sourceMusicalSamples > 0);
                stopButton.setEnabled(hostPreviewEnabled && sourceMusicalSamples > 0);
            }
            else
            {
                playButton.setButtonText(soloPreviewIntent ? "Pause" : "Play");
                playButton.setEnabled(sourceMusicalSamples > 0);
                stopButton.setEnabled(soloPreviewIntent);
            }

            if (pendingPrefetchCentre >= 0)
            {
                if (pendingPrefetchCentre != selectedLoopIndex
                    || gmailClient.getState() != GmailClient::State::connected)
                {
                    pendingPrefetchCentre = -1;
                }
                else if (juce::Time::getMillisecondCounterHiRes()
                             - prefetchRequestedMs >= 300.0)
                {
                    const int neighbour = pendingPrefetchCentre
                        + (pendingPrefetchStep == 0 ? 1 : -1);
                    if (++pendingPrefetchStep >= 2)
                        pendingPrefetchCentre = -1;
                    prefetchRequestedMs = juce::Time::getMillisecondCounterHiRes();
                    prefetchLoop(neighbour);
                }
            }

            prepareNearbyLoops();
            if (!previewBuildPending && !previewBuildRunning && !nearbyRenders.empty())
                startPendingPreviewBuild();

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

        double sourceBpm =
            100.0;

        double sourceSampleRate =
            0.0;

        std::atomic<double> deviceSampleRate { 0.0 };

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

        bool hostPreviewEnabled = true;
        juce::String hostSession;
        uint64_t sourceGeneration = 0;
        float previewVolume = 1.0f;
        juce::SmoothedValue<float> soloGain;
        double previewStateSentMs = 0.0;
        juce::Slider volumeSlider;

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

        juce::ComboBox projectKeySelector;
        juce::ToggleButton keySyncButton;
        juce::TextButton pitchMinus, pitchPlus;
        juce::Label pitchLabel, keyStatusLabel;
        std::unique_ptr<juce::PropertiesFile> settings;
        int projectKeyId = 1;
        int manualSemitones = 0;
        std::optional<MusicalKey> sourceKey;
        juce::File originalPreviewFile;
        juce::String originalPreviewIdentity;
        juce::String appliedRenderKey, renderError;
        juce::File activeHostPreviewFile;
        juce::File preparedDirectory = juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getChildFile("LoopBridge").getChildFile("prepared").getChildFile(juce::Uuid().toString());
        std::map<juce::String, CachedPreview> preparedPreviews;
        uint64_t previewCacheClock = 0;
        std::vector<PreviewRenderer::Request> nearbyRenders;
        int nearbyRenderCentre = -1;
        double nearbyRenderRequestedMs = 0.0;
        int sourceTotalSamples = 0;
        bool soloPreviewReady = false;
        bool soloPreviewIntent = false;

        std::future<
            HostPreviewBuildResult>
            previewBuildFuture;

        int sourceMusicalSamples =
            0;

        int soloPlaybackPosition =
            0;

        juce::AudioFormatManager
            formatManager;

        std::shared_ptr<const juce::AudioBuffer<float>> soloBuffer;
        std::shared_ptr<const juce::AudioBuffer<float>> hostPreviewBuffer;

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

        int dragCandidateIndex =
            -1;

        std::map<juce::String, juce::File>
            gmailLoopCache;

        juce::String libraryAccount;
        double nextGmailSyncMs = 0.0;

        int pendingPrefetchCentre = -1;
        int pendingPrefetchStep = 0;
        double prefetchRequestedMs = 0.0;

        std::set<juce::String>
            downloadsInFlight;

        std::map<juce::String, double>
            gmailLoopDurations;

        int loadedBrowserLoopIndex =
            -1;

        uint64_t previewRequestId =
            0;
        juce::String pendingBrowserPreview;
        bool pendingBrowserAutoPlay = false;
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
