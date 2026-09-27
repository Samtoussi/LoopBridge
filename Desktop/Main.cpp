#include <JuceHeader.h>
#include <cmath>

class LoopBridgeWindow : public juce::DocumentWindow
{
public:
    LoopBridgeWindow()
        : DocumentWindow(
              "LoopBridge",
              juce::Colour::fromRGB(20, 20, 24),
              DocumentWindow::allButtons
          )
    {
        setUsingNativeTitleBar(true);
        setResizable(false, false);

        setContentOwned(new MainComponent(), true);

        centreWithSize(560, 470);
        setVisible(true);
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()
            ->systemRequestedQuit();
    }

private:
    class MainComponent
        : public juce::AudioAppComponent,
          private juce::Timer
    {
    public:
        MainComponent()
            : socket(false)
        {
            listening = socket.bindToPort(
                49152,
                "127.0.0.1"
            );

            formatManager.registerBasicFormats();

            loadButton.setButtonText("Load WAV");
            playButton.setButtonText("Play");
            stopButton.setButtonText("Stop");

            addAndMakeVisible(loadButton);
            addAndMakeVisible(playButton);
            addAndMakeVisible(stopButton);

            playButton.setEnabled(false);
            stopButton.setEnabled(false);

            loadButton.onClick = [this]
            {
                chooseAudioFile();
            };

            playButton.onClick = [this]
            {
                queuePreview();
            };

            stopButton.onClick = [this]
            {
                cancelQueuedPreview();

                transportSource.stop();
                transportSource.setPosition(0.0);

                previewPlaying = false;

                playButton.setEnabled(
                    readerSource != nullptr
                );

                stopButton.setEnabled(false);

                repaint();
            };

            setAudioChannels(0, 2);

            // Faster UI/control timer than before.
            // UDP itself still arrives at the VST's current rate.
            startTimer(5);
        }

        ~MainComponent() override
        {
            stopTimer();

            cancelQueuedPreview();

            transportSource.stop();
            transportSource.setSource(nullptr);

            shutdownAudio();
        }

        void prepareToPlay(
            int samplesPerBlockExpected,
            double sampleRate
        ) override
        {
            transportSource.prepareToPlay(
                samplesPerBlockExpected,
                sampleRate
            );
        }

        void getNextAudioBlock(
            const juce::AudioSourceChannelInfo&
                bufferToFill
        ) override
        {
            if (readerSource == nullptr)
            {
                bufferToFill.clearActiveBufferRegion();
                return;
            }

            transportSource.getNextAudioBlock(
                bufferToFill
            );
        }

        void releaseResources() override
        {
            transportSource.releaseResources();
        }

        void paint(juce::Graphics& g) override
        {
            g.fillAll(
                juce::Colour::fromRGB(20, 20, 24)
            );

            // Title
            g.setColour(juce::Colours::white);

            g.setFont(
                juce::Font(
                    juce::FontOptions(
                        32.0f,
                        juce::Font::bold
                    )
                )
            );

            g.drawFittedText(
                "LOOPBRIDGE",
                0,
                25,
                getWidth(),
                45,
                juce::Justification::centred,
                1
            );

            // Bridge status
            g.setFont(
                juce::Font(
                    juce::FontOptions(15.0f)
                )
            );

            g.setColour(
                listening
                    ? juce::Colours::lightgreen
                    : juce::Colours::red
            );

            g.drawFittedText(
                listening
                    ? "LISTENING"
                    : "PORT ERROR",
                0,
                75,
                getWidth(),
                25,
                juce::Justification::centred,
                1
            );

            // BPM
            g.setColour(juce::Colours::white);

            g.setFont(
                juce::Font(
                    juce::FontOptions(
                        38.0f,
                        juce::Font::bold
                    )
                )
            );

            const auto bpmText =
                hostBpm > 0.0
                    ? juce::String(hostBpm, 2)
                    : "--";

            g.drawFittedText(
                bpmText,
                0,
                105,
                getWidth(),
                50,
                juce::Justification::centred,
                1
            );

            g.setColour(
                juce::Colours::white.withAlpha(0.55f)
            );

            g.setFont(
                juce::Font(
                    juce::FontOptions(14.0f)
                )
            );

            g.drawFittedText(
                "HOST BPM",
                0,
                153,
                getWidth(),
                22,
                juce::Justification::centred,
                1
            );

            // FL transport
            g.setColour(
                hostPlaying
                    ? juce::Colours::lightgreen
                    : juce::Colours::white.withAlpha(0.55f)
            );

            g.setFont(
                juce::Font(
                    juce::FontOptions(
                        14.0f,
                        juce::Font::bold
                    )
                )
            );

            g.drawFittedText(
                hostPlaying
                    ? "FL PLAYING"
                    : "FL STOPPED",
                0,
                185,
                getWidth(),
                25,
                juce::Justification::centred,
                1
            );

            // PPQ
            g.setColour(
                juce::Colours::white.withAlpha(0.75f)
            );

            g.setFont(
                juce::Font(
                    juce::FontOptions(14.0f)
                )
            );

            g.drawFittedText(
                "PPQ: "
                    + juce::String(
                        getEstimatedPpq(),
                        3
                    ),
                0,
                215,
                getWidth(),
                25,
                juce::Justification::centred,
                1
            );

            // Preview status
            g.setFont(
                juce::Font(
                    juce::FontOptions(
                        13.0f,
                        juce::Font::bold
                    )
                )
            );

            if (previewQueued)
            {
                g.setColour(
                    juce::Colours::orange
                );

                g.drawFittedText(
                    "PREVIEW QUEUED -> BEAT "
                        + juce::String(
                            targetPpq,
                            0
                        ),
                    0,
                    242,
                    getWidth(),
                    22,
                    juce::Justification::centred,
                    1
                );
            }
            else if (previewPlaying)
            {
                g.setColour(
                    juce::Colours::lightgreen
                );

                g.drawFittedText(
                    "PREVIEW PLAYING",
                    0,
                    242,
                    getWidth(),
                    22,
                    juce::Justification::centred,
                    1
                );
            }

            // Loaded file
            g.setColour(
                juce::Colours::white.withAlpha(0.75f)
            );

            g.setFont(
                juce::Font(
                    juce::FontOptions(14.0f)
                )
            );

            const juce::String fileText =
                loadedFileName.isEmpty()
                    ? "No audio file loaded"
                    : loadedFileName;

            g.drawFittedText(
                fileText,
                30,
                275,
                getWidth() - 60,
                30,
                juce::Justification::centred,
                1
            );
        }

        void resized() override
        {
            const int buttonWidth = 120;
            const int buttonHeight = 38;
            const int gap = 12;

            const int totalWidth =
                buttonWidth * 3 + gap * 2;

            const int startX =
                (getWidth() - totalWidth) / 2;

            const int y = 335;

            loadButton.setBounds(
                startX,
                y,
                buttonWidth,
                buttonHeight
            );

            playButton.setBounds(
                startX + buttonWidth + gap,
                y,
                buttonWidth,
                buttonHeight
            );

            stopButton.setBounds(
                startX
                    + (buttonWidth + gap) * 2,
                y,
                buttonWidth,
                buttonHeight
            );
        }

    private:
        void chooseAudioFile()
        {
            fileChooser =
                std::make_unique<juce::FileChooser>(
                    "Choose an audio loop",
                    juce::File{},
                    "*.wav"
                );

            const auto flags =
                juce::FileBrowserComponent::openMode
                | juce::FileBrowserComponent::
                      canSelectFiles;

            fileChooser->launchAsync(
                flags,
                [this](
                    const juce::FileChooser& chooser
                )
                {
                    const auto file =
                        chooser.getResult();

                    if (!file.existsAsFile())
                        return;

                    loadAudioFile(file);
                }
            );
        }

        void loadAudioFile(
            const juce::File& file
        )
        {
            auto* reader =
                formatManager.createReaderFor(file);

            if (reader == nullptr)
                return;

            auto newSource =
                std::make_unique<
                    juce::AudioFormatReaderSource
                >(
                    reader,
                    true
                );

            cancelQueuedPreview();

            transportSource.stop();
            transportSource.setSource(nullptr);

            readerSource.reset();

            transportSource.setSource(
                newSource.get(),
                0,
                nullptr,
                reader->sampleRate
            );

            readerSource = std::move(newSource);

            loadedFileName =
                file.getFileName();

            previewPlaying = false;

            playButton.setEnabled(true);
            stopButton.setEnabled(false);

            repaint();
        }

        void queuePreview()
        {
            if (readerSource == nullptr)
                return;

            transportSource.stop();
            transportSource.setPosition(0.0);

            previewPlaying = false;

            // If FL isn't running, preserve the old behaviour:
            // play immediately.
            if (!hostPlaying || hostBpm <= 0.0)
            {
                startPreviewNow();
                return;
            }

            const double currentPpq =
                getEstimatedPpq();

            // Queue for the next whole beat.
            targetPpq =
                std::floor(currentPpq) + 1.0;

            // Avoid an almost-zero wait if the click happens
            // essentially on the beat.
            if ((targetPpq - currentPpq) < 0.01)
                targetPpq += 1.0;

            previewQueued = true;

            playButton.setEnabled(false);
            stopButton.setEnabled(true);

            repaint();
        }

        void startPreviewNow()
        {
            if (readerSource == nullptr)
                return;

            previewQueued = false;

            transportSource.setPosition(0.0);
            transportSource.start();

            previewPlaying = true;

            playButton.setEnabled(false);
            stopButton.setEnabled(true);

            repaint();
        }

        void cancelQueuedPreview()
        {
            previewQueued = false;
            targetPpq = 0.0;
        }

        double getEstimatedPpq() const
        {
            if (!haveHostState)
                return hostPpq;

            if (!hostPlaying || hostBpm <= 0.0)
                return hostPpq;

            const double nowMs =
                juce::Time::getMillisecondCounterHiRes();

            const double elapsedSeconds =
                (nowMs - hostStateReceivedMs)
                / 1000.0;

            const double beatsPerSecond =
                hostBpm / 60.0;

            return hostPpq
                + elapsedSeconds * beatsPerSecond;
        }

        void processBridgeMessage(
            const juce::String& message
        )
        {
            if (!message.startsWith("BPM:"))
                return;

            double newBpm = hostBpm;
            bool newPlaying = hostPlaying;
            double newPpq = hostPpq;

            juce::StringArray parts;
            parts.addTokens(message, ";", "");

            for (const auto& part : parts)
            {
                if (part.startsWith("BPM:"))
                {
                    newBpm =
                        part
                            .fromFirstOccurrenceOf(
                                "BPM:",
                                false,
                                false
                            )
                            .getDoubleValue();
                }
                else if (
                    part.startsWith("PLAYING:")
                )
                {
                    newPlaying =
                        part
                            .fromFirstOccurrenceOf(
                                "PLAYING:",
                                false,
                                false
                            )
                            .getIntValue()
                        == 1;
                }
                else if (
                    part.startsWith("PPQ:")
                )
                {
                    newPpq =
                        part
                            .fromFirstOccurrenceOf(
                                "PPQ:",
                                false,
                                false
                            )
                            .getDoubleValue();
                }
            }

            hostBpm = newBpm;
            hostPlaying = newPlaying;
            hostPpq = newPpq;

            hostStateReceivedMs =
                juce::Time::getMillisecondCounterHiRes();

            haveHostState = true;
        }

        void timerCallback() override
        {
            // Drain all currently available UDP packets.
            // The newest one becomes our reference point.
            while (true)
            {
                char buffer[256] {};

                const int bytesRead =
                    socket.read(
                        buffer,
                        sizeof(buffer) - 1,
                        false
                    );

                if (bytesRead <= 0)
                    break;

                buffer[bytesRead] = '\0';

                processBridgeMessage(
                    juce::String(buffer)
                );
            }

            // If a preview is queued, use our locally
            // extrapolated PPQ to decide when to start.
            if (previewQueued)
            {
                if (!hostPlaying)
                {
                    // FL stopped before the target beat.
                    cancelQueuedPreview();

                    playButton.setEnabled(
                        readerSource != nullptr
                    );

                    stopButton.setEnabled(false);
                }
                else
                {
                    const double currentPpq =
                        getEstimatedPpq();

                    if (currentPpq >= targetPpq)
                    {
                        startPreviewNow();
                    }
                }
            }

            // Detect natural end of the audio file.
            if (previewPlaying
                && !transportSource.isPlaying())
            {
                previewPlaying = false;

                playButton.setEnabled(
                    readerSource != nullptr
                );

                stopButton.setEnabled(false);
            }

            repaint();
        }

        juce::DatagramSocket socket;

        bool listening = false;

        double hostBpm = 0.0;
        bool hostPlaying = false;
        double hostPpq = 0.0;

        bool haveHostState = false;

        double hostStateReceivedMs = 0.0;

        bool previewQueued = false;
        bool previewPlaying = false;

        double targetPpq = 0.0;

        juce::AudioFormatManager formatManager;
        juce::AudioTransportSource transportSource;

        std::unique_ptr<
            juce::AudioFormatReaderSource
        > readerSource;

        std::unique_ptr<juce::FileChooser>
            fileChooser;

        juce::TextButton loadButton;
        juce::TextButton playButton;
        juce::TextButton stopButton;

        juce::String loadedFileName;
    };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(
        LoopBridgeWindow
    )
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
        const juce::String&
    ) override
    {
        mainWindow =
            std::make_unique<LoopBridgeWindow>();
    }

    void shutdown() override
    {
        mainWindow.reset();
    }

private:
    std::unique_ptr<LoopBridgeWindow>
        mainWindow;
};

START_JUCE_APPLICATION(
    LoopBridgeApplication
)