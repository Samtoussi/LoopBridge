#include <JuceHeader.h>

#include "GmailClient.h"
#include "LoopItem.h"
#include "LibraryFilter.h"
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
        setResizable(true, false);
        setResizeLimits(600, 660, 1400, 1400);

        setContentOwned(
            new MainComponent(),
            true);

        centreWithSize(
            760,
            900);

        setVisible(true);
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::
            getInstance()
            ->systemRequestedQuit();
    }

private:
    class CompactLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        CompactLookAndFeel()
        {
            const auto surface = juce::Colour::fromRGB(29, 32, 38);
            const auto text = juce::Colour::fromRGB(226, 229, 234);
            const auto border = juce::Colour::fromRGB(49, 54, 63);
            const auto accent = juce::Colour::fromRGB(139, 179, 166);
            setColour(juce::TextButton::buttonColourId, surface);
            setColour(juce::TextButton::buttonOnColourId, juce::Colour::fromRGB(46, 62, 59));
            setColour(juce::TextButton::textColourOffId, text);
            setColour(juce::TextButton::textColourOnId, text);
            setColour(juce::ToggleButton::textColourId, text);
            setColour(juce::ToggleButton::tickColourId, accent);
            setColour(juce::ToggleButton::tickDisabledColourId, border);
            setColour(juce::ComboBox::backgroundColourId, surface);
            setColour(juce::ComboBox::textColourId, text);
            setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
            setColour(juce::ComboBox::arrowColourId, text.withAlpha(0.6f));
            setColour(juce::PopupMenu::backgroundColourId, surface);
            setColour(juce::PopupMenu::textColourId, text);
            setColour(juce::PopupMenu::highlightedBackgroundColourId, border);
            setColour(juce::PopupMenu::highlightedTextColourId, text);
            setColour(juce::Slider::backgroundColourId, border);
            setColour(juce::Slider::trackColourId, accent);
            setColour(juce::Slider::thumbColourId, accent);
            setColour(juce::Slider::textBoxTextColourId, text.withAlpha(0.7f));
            setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
            setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            setColour(juce::TextEditor::backgroundColourId, surface);
            setColour(juce::TextEditor::textColourId, text);
            setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
            setColour(juce::TextEditor::focusedOutlineColourId, accent);
            setColour(juce::ScrollBar::backgroundColourId, juce::Colours::transparentBlack);
            setColour(juce::ScrollBar::thumbColourId, border.brighter(0.2f));
            setColour(juce::TooltipWindow::backgroundColourId, surface);
            setColour(juce::TooltipWindow::textColourId, text);
            setColour(juce::TooltipWindow::outlineColourId, border);
        }

        juce::Font getTextButtonFont(juce::TextButton&, int) override
        {
            return juce::Font(juce::FontOptions(12.0f));
        }
        juce::Font getComboBoxFont(juce::ComboBox&) override
        {
            return juce::Font(juce::FontOptions(12.0f));
        }
        juce::Font getLabelFont(juce::Label&) override
        {
            return juce::Font(juce::FontOptions(12.0f));
        }
        bool areScrollbarButtonsVisible() override { return false; }
        void drawButtonText(juce::Graphics& g, juce::TextButton& button, bool over, bool down) override
        {
            if (button.getName() != "transport")
            {
                juce::LookAndFeel_V4::drawButtonText(g, button, over, down);
                return;
            }
            g.setColour(button.findColour(juce::TextButton::textColourOffId)
                .withAlpha(button.isEnabled() ? 1.0f : 0.35f));
            const float x = button.getWidth() * 0.5f, y = button.getHeight() * 0.5f;
            if (button.getButtonText() == "Pause")
            {
                g.fillRoundedRectangle(x - 6, y - 6, 4, 12, 1);
                g.fillRoundedRectangle(x + 2, y - 6, 4, 12, 1);
            }
            else if (button.getButtonText() == "Cancel")
            {
                g.drawLine(x - 5, y - 5, x + 5, y + 5, 1.5f);
                g.drawLine(x - 5, y + 5, x + 5, y - 5, 1.5f);
            }
            else
            {
                juce::Path triangle;
                triangle.addTriangle(x - 4, y - 7, x - 4, y + 7, x + 7, y);
                g.fillPath(triangle);
            }
        }
        void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                  const juce::Colour& colour, bool over, bool down) override
        {
            const auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
            if (button.getName() == "tab")
            {
                if (button.getToggleState())
                {
                    g.setColour(button.findColour(juce::TextButton::textColourOnId).withAlpha(0.65f));
                    g.fillRoundedRectangle(bounds.getX() + 10.0f, bounds.getBottom() - 2.0f,
                                           bounds.getWidth() - 20.0f, 2.0f, 1.0f);
                }
                return;
            }
            const bool quiet = button.getName() != "transport" && !button.getToggleState();
            auto fill = button.getToggleState() ? button.findColour(juce::TextButton::buttonOnColourId) : colour;
            if (over || down) fill = fill.brighter(down ? 0.15f : 0.08f);
            g.setColour(fill.withAlpha(button.isEnabled() ? (quiet && !over && !down ? 0.0f : 1.0f) : 0.35f));
            g.fillRoundedRectangle(bounds, 4.0f);
        }
    };

    class MainComponent
        : public juce::AudioAppComponent,
          public juce::TooltipClient,
          private juce::Timer,
          private juce::ScrollBar::Listener
    {
    public:
        MainComponent()
            : receiveSocket(false),
              sendSocket(false)
        {
            setLookAndFeel(&lookAndFeel);
            tooltips.setLookAndFeel(&lookAndFeel);
            listening =
                receiveSocket.bindToPort(
                    49152,
                    "127.0.0.1");

            formatManager
                .registerBasicFormats();

            setWantsKeyboardFocus(true);

            loadButton.setButtonText(
                "Open...");
            loadButton.setTooltip("Open a local 100 BPM / 4-bar WAV");
            bridgeButton.setName("service");
            gmailButton.setName("service");
            bridgeButton.setTooltip("Toggle Bridge ON/OFF: FL Studio preview or local solo preview");
            gmailButton.setTooltip("Connect or disconnect Gmail; known cached loops remain available offline");
            for (auto* button : { &loadButton, &playButton, &stopButton, &bridgeButton, &gmailButton })
                button->setWantsKeyboardFocus(false);

            allTab.setButtonText("All");
            allTab.setName("tab");
            allTab.setToggleState(true, juce::dontSendNotification);
            allTab.setWantsKeyboardFocus(false);
            favoritesTab.setButtonText("Favorites");
            favoritesTab.setName("tab");
            favoritesTab.setTooltip("Loops from favorite producers");
            searchField.setFont(juce::Font(juce::FontOptions(12.0f)));
            searchField.setTextToShowWhenEmpty("Search loops or producers...", juce::Colour::fromRGB(139, 145, 155));
            searchField.setTooltip("Search loop filenames and producers");
            searchField.onTextChange = [this] { rebuildVisibleLibrary(true); };
            searchField.onReturnKey = [this] { grabKeyboardFocus(); };
            allTab.onClick = [this] { favoritesOnly = false; rebuildVisibleLibrary(true); grabKeyboardFocus(); };
            favoritesTab.onClick = [this] { favoritesOnly = true; rebuildVisibleLibrary(true); grabKeyboardFocus(); };
            addAndMakeVisible(allTab);
            addAndMakeVisible(favoritesTab);
            addAndMakeVisible(searchField);
            detailsButton.setButtonText("...");
            detailsButton.setName("service");
            detailsButton.setWantsKeyboardFocus(false);
            detailsButton.setTooltip("Connection details and diagnostics");
            detailsButton.onClick = [this]
            {
                juce::PopupMenu menu;
                menu.addItem(1, "Open local audio...");
                menu.addItem(2, "Stop preview", stopButton.isEnabled());
                menu.addSeparator();
                menu.addItem(3, "Connection details and diagnostics...");
                const juce::Component::SafePointer<MainComponent> safe(this);
                menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&detailsButton),
                    [safe](int result)
                    {
                        if (safe == nullptr) return;
                        if (result == 1) safe->loadButton.triggerClick();
                        else if (result == 2) safe->stopButton.triggerClick();
                        else if (result == 3) safe->showDiagnostics();
                    });
            };
            addAndMakeVisible(detailsButton);
            browserScrollbar.setSingleStepSize(1.0);
            browserScrollbar.addListener(this);
            addAndMakeVisible(browserScrollbar);

            playButton.setButtonText(
                "Play");
            playButton.setName("transport");

            stopButton.setButtonText(
                "Stop");

            bridgeButton.setButtonText(
                "BRIDGE ON");
            bridgeButton.setToggleState(bridgeEnabled, juce::dontSendNotification);

            gmailButton.setButtonText(
                "Gmail");

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
            volumeSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
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
            const auto savedFavorites = juce::JSON::parse(settings->getValue("favoriteProducers"));
            if (const auto saved = savedFavorites.getArray())
                for (const auto& name : *saved)
                    favoriteProducers.insert(LibraryFilter::normalizeProducer(name.toString()));
            projectKeyId = juce::jlimit(1, 24, settings->getIntValue("projectKey", 1));
            const char* notes[] = { "C", "C#/Db", "D", "D#/Eb", "E", "F", "F#/Gb", "G", "G#/Ab", "A", "A#/Bb", "B" };
            for (int mode = 0; mode < 2; ++mode)
                for (int note = 0; note < 12; ++note)
                    projectKeySelector.addItem(juce::String(notes[note]) + (mode ? " minor" : " major"), mode * 12 + note + 1);
            projectKeySelector.setSelectedId(projectKeyId, juce::dontSendNotification);
            projectKeySelector.setTooltip("Project key");
            keySyncButton.setTooltip("Match the loop key to the project key");
            pitchMinus.setTooltip("Lower manual transpose by one semitone");
            pitchPlus.setTooltip("Raise manual transpose by one semitone");
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
            addChildComponent(keyStatusLabel);
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
                    bridgeButton.setToggleState(bridgeEnabled, juce::dontSendNotification);

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
                            "Gmail");

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
                                    "Gmail");

                                gmailStatus =
                                    "GMAIL: CONNECTED";

                                startGmailSync();
                            }
                            else if (state
                                     == GmailClient::State::authorizing)
                            {
                                gmailButton.setButtonText(
                                    "Gmail connecting");

                                gmailStatus =
                                    "GMAIL: "
                                    + message;
                            }
                            else if (state
                                     == GmailClient::State::error)
                            {
                                gmailButton.setButtonText(
                                    "Gmail");

                                gmailStatus =
                                    "GMAIL ERROR: "
                                    + message;
                            }
                            else
                            {
                                gmailButton.setButtonText(
                                    "Gmail");

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
            setLookAndFeel(nullptr);
            tooltips.setLookAndFeel(nullptr);
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
            if (visibleLoopIndices.empty()) return false;
            const int current = selectedVisibleRow();
            if (key == juce::KeyPress::downKey || key == juce::KeyPress::upKey)
            {
                const int row = current < 0 ? 0 : juce::jlimit(0, static_cast<int>(visibleLoopIndices.size()) - 1,
                    current + (key == juce::KeyPress::downKey ? 1 : -1));
                selectLoop(visibleItem(row));
                previewSelectedLoop();
                return true;
            }
            if (key == juce::KeyPress::homeKey || key == juce::KeyPress::endKey)
            {
                selectLoop(visibleItem(key == juce::KeyPress::homeKey ? 0 : static_cast<int>(visibleLoopIndices.size()) - 1));
                return true;
            }
            if (current < 0) return false;
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

            if (!getBrowserRowsBounds().contains(event.getPosition()))
            {
                grabKeyboardFocus();
                return;
            }

            const int visibleRow =
                (event.y - rowsTop)
                / browserRowHeight;

            const int itemIndex =
                visibleItem(browserScrollIndex + visibleRow);

            if (itemIndex >= 0
                && itemIndex
                       < static_cast<int>(
                           loopItems.size()))
            {
                const int favoriteLeft = getBrowserColumns().favorite;
                if (event.x >= favoriteLeft && event.x < favoriteLeft + 30)
                {
                    toggleProducerFavorite(itemIndex);
                    grabKeyboardFocus();
                    return;
                }
                selectLoop(itemIndex);

                const int downloadLeft =
                    getBrowserColumns().download;

                if (event.x >= downloadLeft
                    && event.x < downloadLeft + 30)
                {
                    downloadSelectedLoop();
                    grabKeyboardFocus();
                    return;
                }

                const int playButtonLeft =
                    getBrowserColumns().play;

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
                        visibleLoopIndices.size())
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

            if (!getBrowserRowsBounds().contains(event.getPosition()))
            {
                return;
            }

            const int visibleRow =
                (event.y - rowsTop)
                / browserRowHeight;

            const int itemIndex =
                visibleItem(browserScrollIndex + visibleRow);

            if (itemIndex < 0
                || itemIndex
                       >= static_cast<int>(
                           loopItems.size()))
            {
                return;
            }

            const int downloadLeft =
                getBrowserColumns().download;

            if (event.x >= downloadLeft
                && event.x < downloadLeft + 30)
                return;

            if (event.x >= getBrowserColumns().favorite && event.x < getBrowserColumns().favorite + 30) return;
            selectLoop(itemIndex);

            loadSelectedGmailLoop();

            grabKeyboardFocus();
        }

        void paint(juce::Graphics& g) override
        {
            const auto text = juce::Colour::fromRGB(226, 229, 234);
            const auto muted = juce::Colour::fromRGB(139, 145, 155);
            const auto accent = juce::Colour::fromRGB(139, 179, 166);
            const auto warning = juce::Colour::fromRGB(218, 169, 112);
            g.fillAll(juce::Colour::fromRGB(19, 21, 25));
            g.setColour(text);
            g.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
            g.drawText("LOOPBRIDGE", 16, 8, 140, 28, juce::Justification::centredLeft);

            const bool gmailAttention = gmailStatus.containsIgnoreCase("ERROR")
                || gmailStatus.containsIgnoreCase("PAUSED");
            g.setColour(gmailAttention ? warning : gmailClient.getState() == GmailClient::State::connected
                ? accent : muted);
            g.fillEllipse(static_cast<float>(gmailButton.getX() - 8), 19.0f, 5.0f, 5.0f);
            g.setColour(juce::Colour::fromRGB(39, 43, 50));
            // Surface spacing separates the header without a divider.

            drawLoopBrowser(g);

            const int playerTop = getHeight() - playerHeight;
            g.setColour(juce::Colour::fromRGB(24, 27, 32));
            g.fillRect(0, playerTop, getWidth(), playerHeight);
            g.setColour(juce::Colour::fromRGB(49, 54, 63));
            // Read-only progress uses the audio position or the existing host PPQ estimate.
            g.fillRect(0, playerTop, getWidth(), 2);
            double progress = -1.0;
            if (pendingBrowserPreview.isEmpty() && loadedFileName.isNotEmpty())
            {
                if (bridgeEnabled)
                {
                    if (isHostConnected() && hostPreviewReady && hostPreviewEnabled)
                    {
                        double phase = std::fmod(getEstimatedPpq(), loopBeats);
                        if (phase < 0.0) phase += loopBeats;
                        progress = phase / loopBeats;
                    }
                }
                else
                {
                    const juce::ScopedTryLock lock(audioLock);
                    if (lock.isLocked() && soloPreviewReady && soloBuffer && soloBuffer->getNumSamples() > 0)
                        progress = static_cast<double>(soloPlaybackPosition) / soloBuffer->getNumSamples();
                }
            }
            if (progress >= 0.0)
            {
                const float position = static_cast<float>(juce::jlimit(0.0, 1.0, progress) * getWidth());
                g.setColour(accent.withAlpha(0.7f));
                g.fillRect(0.0f, static_cast<float>(playerTop), position, 2.0f);
                g.fillEllipse(juce::jlimit(0.0f, static_cast<float>(getWidth() - 4), position - 2),
                              static_cast<float>(playerTop), 4.0f, 4.0f);
            }

            // Display the pending or loaded source, not an unrelated highlighted row.
            const LoopItem* playerItem = nullptr;
            const auto playerIdentity = pendingBrowserPreview.isNotEmpty() ? pendingBrowserPreview : originalPreviewIdentity;
            if (playerIdentity.isNotEmpty())
                for (const auto& item : loopItems)
                    if (getLoopCacheKey(item) == playerIdentity) { playerItem = &item; break; }
            const bool pending = pendingBrowserPreview.isNotEmpty();
            const bool hasSource = pending || loadedFileName.isNotEmpty();
            const auto title = playerItem != nullptr ? playerItem->filename
                : loadedFileName.isNotEmpty() ? loadedFileName : juce::String("Select a loop");
            const int metadataWidth = getWidth() - 248;
            g.setColour(text);
            g.setFont(juce::Font(juce::FontOptions(hasSource ? 14.0f : 13.0f, juce::Font::bold)));
            g.drawText(title, 100, playerTop + 8, metadataWidth, 22, juce::Justification::centredLeft);
            if (hasSource)
            {
                juce::String metadata = playerItem != nullptr ? getSenderDisplayName(*playerItem) : "Local file";
                const double bpm = pending && playerItem != nullptr ? playerItem->bpm.value_or(0.0) : sourceBpm;
                if (bpm > 0.0) metadata += juce::String::fromUTF8(" \xC2\xB7 ") + juce::String(bpm, 0) + " BPM";
                if (playerItem != nullptr && playerItem->key.isNotEmpty())
                    metadata += juce::String::fromUTF8(" \xC2\xB7 ") + playerItem->key;
                g.setColour(muted);
                g.setFont(juce::Font(juce::FontOptions(11.5f)));
                g.drawText(metadata, 100, playerTop + 30, metadataWidth, 18, juce::Justification::centredLeft);
            }
            // A speaker identifies volume without a caption or numeric control box.
            g.setColour(muted);
            juce::Path speaker;
            speaker.startNewSubPath(18.0f, static_cast<float>(playerTop + 61));
            speaker.lineTo(22.0f, static_cast<float>(playerTop + 61));
            speaker.lineTo(27.0f, static_cast<float>(playerTop + 57));
            speaker.lineTo(27.0f, static_cast<float>(playerTop + 71));
            speaker.lineTo(22.0f, static_cast<float>(playerTop + 67));
            speaker.lineTo(18.0f, static_cast<float>(playerTop + 67));
            speaker.closeSubPath();
            g.fillPath(speaker);
            g.drawLine(31.0f, static_cast<float>(playerTop + 60), 31.0f, static_cast<float>(playerTop + 68), 1.5f);
        }
        void resized() override
        {

            gmailButton.setBounds(getWidth() - 148, 8, 96, 28);
            detailsButton.setBounds(getWidth() - 44, 8, 28, 28);
            searchField.setBounds(16, 52, getWidth() - 32, 32);
            allTab.setBounds(16, 92, 44, 26);
            favoritesTab.setBounds(66, 92, 78, 26);
            const int playerTop = getHeight() - playerHeight;
            playButton.setBounds(16, playerTop + 10, 68, 32);
            bridgeButton.setBounds(getWidth() - 132, playerTop + 10, 116, 32);
            stopButton.setVisible(false);
            loadButton.setVisible(false);
            const int transposeLeft = getWidth() - 132;
            projectKeySelector.setBounds(transposeLeft - 142, playerTop + 50, 130, 28);
            keySyncButton.setBounds(projectKeySelector.getX() - 102, playerTop + 50, 90, 28);
            volumeSlider.setBounds(38, playerTop + 50, std::min(218, getWidth() - 426), 28);
            pitchMinus.setBounds(transposeLeft, playerTop + 50, 26, 28);
            pitchLabel.setBounds(transposeLeft + 28, playerTop + 50, 60, 28);
            pitchPlus.setBounds(transposeLeft + 90, playerTop + 50, 26, 28);
            browserVisibleRows = std::max(1, (playerTop - 26 - browserTop - browserHeaderHeight) / browserRowHeight);
            const int maxScroll = std::max(0, static_cast<int>(visibleLoopIndices.size()) - browserVisibleRows);
            // Keep the selected row visible when the window height shrinks.
            if (selectedVisibleRow() >= browserScrollIndex + browserVisibleRows)
                browserScrollIndex = selectedVisibleRow() - browserVisibleRows + 1;
            browserScrollIndex = juce::jlimit(0, maxScroll, browserScrollIndex);
            browserScrollbar.setBounds(getWidth() - browserLeft - 8, browserTop + browserHeaderHeight,
                                       8, browserVisibleRows * browserRowHeight);
        }
        juce::String getTooltip() override
        {
            const auto position = getMouseXYRelative();
            if (!getBrowserRowsBounds().contains(position)) return {};
            const int index = browserScrollIndex + (position.y - getBrowserRowsBounds().getY()) / browserRowHeight;
            if (index >= static_cast<int>(visibleLoopIndices.size())) return {};
            const int actionLeft = getBrowserColumns().download;
            if (position.x >= actionLeft && position.x < actionLeft + 30) return "Download";
            if (position.x >= getBrowserColumns().favorite && position.x < getBrowserColumns().favorite + 30) return "Favorite producer";
            return {};
        }
    private:
        CompactLookAndFeel lookAndFeel;
        juce::TooltipWindow tooltips { this, 650 };
        juce::TextEditor searchField;
        juce::TextButton allTab, favoritesTab, detailsButton;
        juce::ScrollBar browserScrollbar { true };
        int displayedScrollStart = -1, displayedScrollRows = -1;
        std::map<juce::String, bool> downloadIndicators;
        double downloadIndicatorsUpdatedMs = 0.0;
        std::vector<int> visibleLoopIndices;
        std::set<juce::String> favoriteProducers;
        bool favoritesOnly = false;

        int visibleItem(int row) const { return LibraryFilter::underlyingIndex(visibleLoopIndices, row); }
        int selectedVisibleRow() const
        {
            const auto found = std::find(visibleLoopIndices.begin(), visibleLoopIndices.end(), selectedLoopIndex);
            return found == visibleLoopIndices.end() ? -1 : static_cast<int>(found - visibleLoopIndices.begin());
        }
        void rebuildVisibleLibrary(bool resetScroll)
        {
            visibleLoopIndices = LibraryFilter::visibleItems(loopItems, favoriteProducers, favoritesOnly, searchField.getText());
            allTab.setToggleState(!favoritesOnly, juce::dontSendNotification);
            favoritesTab.setToggleState(favoritesOnly, juce::dontSendNotification);
            dragCandidateIndex = -1;
            if (resetScroll) browserScrollIndex = 0;
            browserScrollIndex = juce::jlimit(0, std::max(0, static_cast<int>(visibleLoopIndices.size()) - browserVisibleRows), browserScrollIndex);
            // Keep selection and pending/active playback identity untouched by filtering.
            repaint();
        }
        void toggleProducerFavorite(int index)
        {
            const auto name = LibraryFilter::normalizeProducer(LibraryFilter::producer(loopItems[static_cast<size_t>(index)]));
            if (name.isEmpty()) return;
            if (favoriteProducers.count(name)) favoriteProducers.erase(name);
            else favoriteProducers.insert(name);
            juce::Array<juce::var> names;
            for (const auto& favorite : favoriteProducers) names.add(favorite);
            settings->setValue("favoriteProducers", juce::JSON::toString(juce::var(names)));
            settings->saveIfNeeded();
            rebuildVisibleLibrary(false);
        }

        static constexpr int playerHeight = 86;
        static constexpr double loopBeats =
            16.0;

        static constexpr double
            previewDebounceMs = 150.0;

        static constexpr double
            ppqDiscontinuityThreshold = 0.25;

        static constexpr int
            browserTop = 128;

        static constexpr int
            browserLeft = 16;

        static constexpr int
            browserHeaderHeight = 0;

        static constexpr int
            browserRowHeight = 54;

        int browserVisibleRows = 1;

        struct BrowserColumns
        {
            int play, loop, bpm, key, favorite, download;
        };

        BrowserColumns getBrowserColumns() const
        {
            const int width = getWidth() - browserLeft * 2;
            BrowserColumns columns;
            columns.play = browserLeft + 10;
            columns.loop = browserLeft + 46;
            columns.download = browserLeft + width - 42;
            columns.favorite = columns.download - 34;
            columns.key = columns.favorite - 66;
            columns.bpm = columns.key - 52;
            return columns;
        }

        juce::Rectangle<int> getBrowserRowsBounds() const
        {
            return { browserLeft, browserTop + browserHeaderHeight,
                     getWidth() - browserLeft * 2, browserVisibleRows * browserRowHeight };
        }

        void scrollBarMoved(juce::ScrollBar*, double start) override
        {
            browserScrollIndex = juce::jlimit(0,
                std::max(0, static_cast<int>(visibleLoopIndices.size()) - browserVisibleRows),
                static_cast<int>(std::round(start)));
            repaint();
            grabKeyboardFocus();
        }

        void showDiagnostics()
        {
            juce::String details = "Bridge: " + juce::String(bridgeEnabled ? "ON" : "OFF")
                + " | " + (isHostConnected() ? "FL connected" : "Waiting for VST")
                + "\nListening: " + (listening ? "Yes" : "PORT ERROR")
                + "\nFL transport: " + (hostPlaying ? "Playing" : "Stopped")
                + "\nFL BPM: " + juce::String(hostBpm, 2)
                + " | PPQ: " + juce::String(getEstimatedPpq(), 3)
                + "\nSample rates (FL / file / device): " + juce::String(hostSampleRate, 0)
                + " / " + juce::String(sourceSampleRate, 0) + " / " + juce::String(deviceSampleRate.load(), 0)
                + "\nSource: " + juce::String(sourceBpm, 0) + " BPM / 4 bars"
                + "\nFile: " + (loadedFileName.isEmpty() ? "None" : loadedFileName)
                + "\nPreparing: " + (stretching ? "Yes" : "No")
                + " | Host preview: " + (hostPreviewReady ? "Ready" : "Not ready")
                + " | Enabled: " + (hostPreviewEnabled ? "Yes" : "No");
            if (sourceSampleRate > 0.0)
                details += "\nFile length: " + juce::String(sourceTotalSamples / sourceSampleRate, 3) + "s";
            if (sourceBpm > 0.0)
                details += " | Musical length: " + juce::String(loopBeats * 60.0 / sourceBpm, 3) + "s";
            if (hostPreviewBuffer && stretchedForHostSampleRate > 0.0)
                details += " | Prepared length: "
                    + juce::String(hostPreviewBuffer->getNumSamples() / stretchedForHostSampleRate, 3) + "s";
            details += "\n\n" + gmailStatus;
            if (gmailClient.getLastRequestError().message.isNotEmpty())
                details += "\n" + gmailClient.getLastRequestError().message;
            if (renderError.isNotEmpty()) details += "\n\nPreview: " + renderError;
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,
                                                 "LoopBridge details", details, "Close", this);
        }

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
            rebuildVisibleLibrary(false);
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

            const int row = selectedVisibleRow();
            if (row >= 0 && row < browserScrollIndex) browserScrollIndex = row;
            else if (row >= browserScrollIndex + browserVisibleRows) browserScrollIndex = row - browserVisibleRows + 1;
            browserScrollIndex = juce::jlimit(0, std::max(0, static_cast<int>(visibleLoopIndices.size()) - browserVisibleRows), browserScrollIndex);
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

        void drawLoopBrowser(juce::Graphics& g)
        {
            const auto rows = getBrowserRowsBounds();
            const auto bounds = rows.withTop(browserTop);
            const auto columns = getBrowserColumns();
            const auto text = juce::Colour::fromRGB(226, 229, 234);
            const auto muted = juce::Colour::fromRGB(139, 145, 155);
            const auto accent = juce::Colour::fromRGB(139, 179, 166);
            const auto warning = juce::Colour::fromRGB(218, 169, 112);
            g.setColour(juce::Colour::fromRGB(23, 26, 31));
            g.fillRoundedRectangle(bounds.toFloat(), 5.0f);
            browserScrollbar.setRangeLimits(0.0, std::max(browserVisibleRows, static_cast<int>(visibleLoopIndices.size())),
                                            juce::dontSendNotification);
            // Don't overwrite a user scroll while JUCE's asynchronous listener
            // notification is pending. Push only changes from the browser view.
            if (displayedScrollStart != browserScrollIndex || displayedScrollRows != browserVisibleRows)
            {
                browserScrollbar.setCurrentRange(browserScrollIndex, browserVisibleRows, juce::dontSendNotification);
                displayedScrollStart = browserScrollIndex;
                displayedScrollRows = browserVisibleRows;
            }
            // More visible rows must not multiply directory scans on every
            // bridge timer repaint. This only caches the displayed checkmarks;
            // actual Download and drag acquisition still check the real files.
            const auto now = juce::Time::getMillisecondCounterHiRes();
            if (now - downloadIndicatorsUpdatedMs >= 500.0)
            {
                downloadIndicators.clear();
                downloadIndicatorsUpdatedMs = now;
            }
            if (visibleLoopIndices.empty())
            {
                g.setColour(muted);
                g.setFont(juce::Font(juce::FontOptions(13.0f)));
                g.drawText(gmailClient.getState() == GmailClient::State::connected
                    ? "No matching loops" : loopItems.empty() ? "Connect Gmail to discover your loops" : "No matching loops",
                    rows, juce::Justification::centred);
            }
            const int endIndex = std::min(browserScrollIndex + browserVisibleRows, static_cast<int>(visibleLoopIndices.size()));
            for (int visibleRow = browserScrollIndex; visibleRow < endIndex; ++visibleRow)
            {
                const int itemIndex = visibleItem(visibleRow);
                const int rowY = rows.getY() + (visibleRow - browserScrollIndex) * browserRowHeight;
                const bool selected = itemIndex == selectedLoopIndex;
                const bool hovered = isMouseOver() && rows.contains(getMouseXYRelative())
                    && (getMouseXYRelative().y - rows.getY()) / browserRowHeight == visibleRow - browserScrollIndex;
                if (selected || hovered)
                {
                    g.setColour(selected ? juce::Colour::fromRGB(37, 45, 46) : juce::Colour::fromRGB(29, 33, 38));
                    g.fillRoundedRectangle(static_cast<float>(browserLeft + 4), static_cast<float>(rowY + 3),
                                           static_cast<float>(bounds.getWidth() - 16), static_cast<float>(browserRowHeight - 6), 6.0f);
                }
                const auto& item = loopItems[static_cast<size_t>(itemIndex)];
                const bool favorite = favoriteProducers.count(LibraryFilter::normalizeProducer(LibraryFilter::producer(item))) > 0;
                if (favorite || selected || hovered)
                {
                    const bool heartHovered = hovered && getMouseXYRelative().x >= columns.favorite
                        && getMouseXYRelative().x < columns.favorite + 30;
                    g.setColour(favorite ? juce::Colour::fromRGB(heartHovered ? 238 : 208, heartHovered ? 238 : 208, heartHovered ? 238 : 208)
                                         : juce::Colour::fromRGB(heartHovered ? 164 : 115, heartHovered ? 164 : 115, heartHovered ? 164 : 115));
                    // Draw a small monochrome heart rather than a platform-dependent text/emoji glyph.
                    juce::Path heart;
                    heart.startNewSubPath(6.0f, 10.5f);
                    heart.cubicTo(4.5f, 9.0f, 0.0f, 5.8f, 0.0f, 3.0f);
                    heart.cubicTo(0.0f, -0.5f, 4.4f, -0.8f, 6.0f, 2.0f);
                    heart.cubicTo(7.6f, -0.8f, 12.0f, -0.5f, 12.0f, 3.0f);
                    heart.cubicTo(12.0f, 5.8f, 7.5f, 9.0f, 6.0f, 10.5f);
                    heart.closeSubPath();
                    heart.applyTransform(juce::AffineTransform::translation(static_cast<float>(columns.favorite + 9),
                                         static_cast<float>(rowY) + (browserRowHeight - 11) * 0.5f));
                    if (favorite) g.fillPath(heart);
                    else g.strokePath(heart, juce::PathStrokeType(1.0f));
                }
                const bool thisLoopPlaying = itemIndex == loadedBrowserLoopIndex
                    && (bridgeEnabled ? isHostConnected() && hostPreviewEnabled && hostPreviewReady && hostPlaying : soloPlaying);
                g.setColour(thisLoopPlaying ? accent : muted.withAlpha(selected || hovered ? 0.85f : 0.45f));
                if (thisLoopPlaying)
                {
                    g.fillRect(columns.play + 10, rowY + (browserRowHeight - 8) / 2, 2, 8);
                    g.fillRect(columns.play + 15, rowY + (browserRowHeight - 8) / 2, 2, 8);
                }
                else
                {
                    juce::Path triangle;
                    triangle.addTriangle(static_cast<float>(columns.play + 11), static_cast<float>(rowY + (browserRowHeight - 8) / 2),
                                         static_cast<float>(columns.play + 11), static_cast<float>(rowY + (browserRowHeight + 8) / 2),
                                         static_cast<float>(columns.play + 18), static_cast<float>(rowY + browserRowHeight / 2));
                    g.fillPath(triangle);
                }
                g.setFont(juce::Font(juce::FontOptions(14.0f, juce::Font::bold)));
                g.setColour(text.withAlpha(selected ? 1.0f : 0.9f));
                g.drawText(item.filename, columns.loop, rowY + 5, columns.bpm - columns.loop - 16,
                           24, juce::Justification::centredLeft);
                g.setFont(juce::Font(juce::FontOptions(11.5f)));
                g.setColour(muted);
                const auto duration = gmailLoopDurations.find(getLoopCacheKey(item));
                const auto secondary = getSenderDisplayName(item)
                    + (duration != gmailLoopDurations.end() ? juce::String::fromUTF8(" \xC2\xB7 ") + formatDuration(duration->second) : juce::String{});
                g.drawText(secondary, columns.loop, rowY + 29, columns.bpm - columns.loop - 16,
                           18, juce::Justification::centredLeft);
                g.setFont(juce::Font(juce::FontOptions(12.0f)));
                g.drawText(item.bpm ? juce::String(*item.bpm, 0) : "--", columns.bpm, rowY,
                           44, browserRowHeight, juce::Justification::centredLeft);
                g.drawText(item.key.isNotEmpty() ? item.key : "--", columns.key, rowY,
                           58, browserRowHeight, juce::Justification::centredLeft);
                const auto identity = getLoopCacheKey(item);
                auto indicator = downloadIndicators.find(identity);
                if (indicator == downloadIndicators.end())
                    indicator = downloadIndicators.emplace(identity, getDownloadedLoopFile(item).existsAsFile()).first;
                const bool downloaded = indicator->second;
                const bool downloading = downloadsInFlight.count(getLoopCacheKey(item)) > 0;
                if (selected || hovered)
                {
                    g.setColour(juce::Colour::fromRGB(49, 57, 61));
                    g.fillRoundedRectangle(static_cast<float>(columns.download), static_cast<float>(rowY + (browserRowHeight - 30) / 2),
                                           30.0f, 30.0f, 6.0f);
                }
                g.setColour((downloaded ? accent : downloading ? warning : muted).withAlpha(selected || hovered || downloading ? 1.0f : 0.0f));
                g.setFont(juce::Font(juce::FontOptions(16.0f)));
                g.drawText(downloaded ? juce::String::fromUTF8("\xE2\x9C\x93") : downloading ? "..." : "+",
                           columns.download, rowY, 30, browserRowHeight, juce::Justification::centred);
            }

            juce::String status;
            bool attention = false;
            if (!listening) { status = "FL connection port unavailable. Open details."; attention = true; }
            else if (renderError.isNotEmpty()) { status = renderError; attention = true; }
            else if (gmailStatus.containsIgnoreCase("ERROR") || gmailStatus.containsIgnoreCase("PAUSED"))
                { status = gmailStatus; attention = true; }
            else if (pendingBrowserPreview.isNotEmpty() || stretching) status = "Preparing preview...";
            else if (gmailClient.isLibrarySyncActive()) status = "Syncing Gmail in the background...";
            else if (bridgeEnabled && hostPreviewEnabled && hostPreviewReady && !hostPlaying)
                status = "Ready. Start FL transport to preview.";
            g.setColour(attention ? warning : muted);
            g.setFont(juce::Font(juce::FontOptions(11.0f)));
            g.drawText(status, browserLeft, bounds.getBottom() + 3, bounds.getWidth() - 86,
                       20, juce::Justification::centredLeft);
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
            pitchLabel.setText((manualSemitones > 0 ? "+" : "") + juce::String(manualSemitones) + " st",
                               juce::dontSendNotification);
            pitchLabel.setJustificationType(juce::Justification::centred);
            pitchMinus.setEnabled(pitch > -12);
            pitchPlus.setEnabled(pitch < 12);
            keyStatusLabel.setText(!key ? "Source key unknown"
                : keySyncButton.getToggleState() && key->minor != projectKey().minor
                    ? "Mode mismatch: manual only" : "", juce::dontSendNotification);
            keyStatusLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.6f));
            pitchLabel.setTooltip("Manual: " + juce::String(manualSemitones) + " st | Effective: "
                + juce::String(pitch) + " st\n" + (!key ? "Unknown source key: manual pitch only"
                : keySyncButton.getToggleState() && key->minor != projectKey().minor
                    ? "Major/minor mismatch: manual pitch only" : "Includes Key Sync when enabled"));
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
            loadedFileName = identity.isEmpty() ? file.getFileName() : juce::String{};
            if (identity.isNotEmpty())
                for (const auto& item : loopItems)
                    if (getLoopCacheKey(item) == identity) { loadedFileName = item.filename; break; }
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

            playButton.setTooltip(playButton.getButtonText());

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
