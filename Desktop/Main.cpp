#include <JuceHeader.h>
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
            620,
            580);

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

            loadButton.setButtonText(
                "Load 100 BPM WAV");

            playButton.setButtonText(
                "Play");

            stopButton.setButtonText(
                "Stop");

            bridgeButton.setButtonText(
                "BRIDGE ON");

            addAndMakeVisible(
                loadButton);

            addAndMakeVisible(
                playButton);

            addAndMakeVisible(
                stopButton);

            addAndMakeVisible(
                bridgeButton);

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
        }

        void resized() override
        {
            const int buttonWidth = 150;
            const int buttonHeight = 40;
            const int gap = 12;

            const int totalWidth =
                buttonWidth * 3
                + gap * 2;

            const int startX =
                (getWidth()
                 - totalWidth)
                / 2;

            const int y = 410;

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
                470,
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

        struct HostPreviewBuildResult
        {
            juce::AudioBuffer<float>
                buffer;

            double bpm = 0.0;
            double sampleRate = 0.0;

            bool success = false;
        };

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

                    loadAudioFile(file);
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

            hostPreviewReady = false;

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

                soloPlaybackPosition = 0;
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

            previewBuildPending = true;

            stretching = true;

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
                previewBuildPending = false;
                stretching = false;
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

            previewBuildPending = false;
            previewBuildRunning = true;

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

            previewBuildRunning = false;

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
                stretching = true;
            }
            else if (!stillCurrent)
            {
                scheduleHostPreviewBuild(
                    false);
            }
            else
            {
                stretching = false;
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

            // Re-send current Bridge state after
            // publishing a new preview.
            sendBridgeState();

            hostPreviewReady = true;
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

                soloPlaybackPosition = 0;
                soloPlaying = true;
            }

            playButton.setEnabled(false);
            stopButton.setEnabled(true);

            repaint();
        }

        void stopSoloPreview()
        {
            {
                const juce::ScopedLock lock(
                    audioLock);

                soloPlaying = false;
                soloPlaybackPosition = 0;
            }

            playButton.setEnabled(
                sourceMusicalSamples > 0);

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

            haveHostState = true;

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

        bool listening = false;

        bool bridgeEnabled = true;

        double hostBpm = 0.0;
        bool hostPlaying = false;
        double hostPpq = 0.0;

        double hostSampleRate = 0.0;
        double sourceSampleRate = 0.0;
        double deviceSampleRate = 0.0;

        bool haveHostState = false;

        double hostStateReceivedMs =
            0.0;

        bool stretching = false;
        bool hostPreviewReady = false;
        bool soloPlaying = false;

        double stretchedForHostBpm =
            0.0;

        double stretchedForHostSampleRate =
            0.0;

        bool previewBuildPending = false;
        bool previewBuildRunning = false;

        double previewBuildRequestedMs =
            0.0;

        double pendingPreviewBpm =
            0.0;

        double pendingPreviewSampleRate =
            0.0;

        std::future<
            HostPreviewBuildResult>
            previewBuildFuture;

        int sourceMusicalSamples = 0;
        int soloPlaybackPosition = 0;

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

        juce::TextButton loadButton;
        juce::TextButton playButton;
        juce::TextButton stopButton;
        juce::TextButton bridgeButton;

        juce::String loadedFileName;
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