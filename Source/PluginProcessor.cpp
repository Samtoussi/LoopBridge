#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>
#include <limits>

LoopBridgeAudioProcessor::LoopBridgeAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
    : AudioProcessor(
          BusesProperties()
              .withInput(
                  "Input",
                  juce::AudioChannelSet::stereo(),
                  true)
              .withOutput(
                  "Output",
                  juce::AudioChannelSet::stereo(),
                  true)),
      sendSocket(false),
      receiveSocket(false)
#endif
{
    formatManager.registerBasicFormats();

    receiveSocket.bindToPort(
        49153,
        "127.0.0.1");

    startTimer(10);
}

LoopBridgeAudioProcessor::~LoopBridgeAudioProcessor()
{
    stopTimer();
}

const juce::String
LoopBridgeAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool
LoopBridgeAudioProcessor::acceptsMidi() const
{
    return false;
}

bool
LoopBridgeAudioProcessor::producesMidi() const
{
    return false;
}

bool
LoopBridgeAudioProcessor::isMidiEffect() const
{
    return false;
}

double
LoopBridgeAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int
LoopBridgeAudioProcessor::getNumPrograms()
{
    return 1;
}

int
LoopBridgeAudioProcessor::getCurrentProgram()
{
    return 0;
}

void
LoopBridgeAudioProcessor::setCurrentProgram(int)
{
}

const juce::String
LoopBridgeAudioProcessor::getProgramName(int)
{
    return {};
}

void
LoopBridgeAudioProcessor::changeProgramName(
    int,
    const juce::String&)
{
}

void
LoopBridgeAudioProcessor::prepareToPlay(
    double sampleRate,
    int)
{
    currentSampleRate = sampleRate;
}

void
LoopBridgeAudioProcessor::releaseResources()
{
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool
LoopBridgeAudioProcessor::isBusesLayoutSupported(
    const BusesLayout& layouts) const
{
    const auto mainOutput =
        layouts.getMainOutputChannelSet();

    if (mainOutput
            != juce::AudioChannelSet::mono()
        && mainOutput
            != juce::AudioChannelSet::stereo())
    {
        return false;
    }

    if (layouts.getMainInputChannelSet()
        != mainOutput)
    {
        return false;
    }

    return true;
}
#endif

void
LoopBridgeAudioProcessor::processBlock(
    juce::AudioBuffer<float>& buffer,
    juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    auto* playHead =
        getPlayHead();

    if (playHead == nullptr)
        return;

    const auto position =
        playHead->getPosition();

    if (!position.hasValue())
        return;

    double bpm = 0.0;
    double ppq = 0.0;
    bool playing = false;

    if (const auto bpmValue =
            position->getBpm())
    {
        bpm = *bpmValue;
        hostBpm.store(bpm);
    }

    if (const auto ppqValue =
            position->getPpqPosition())
    {
        ppq = *ppqValue;
    }

    playing =
        position->getIsPlaying();

    const auto preview =
        std::atomic_load(
            &previewData);

    // ---------------------------------------------------------
    // LOOPBRIDGE AUDIO OUTPUT
    //
    // bridgeEnabled is controlled by the desktop app.
    //
    // OFF:
    //   VST keeps the preview in RAM but renders nothing.
    //
    // ON:
    //   Playback resumes immediately at the correct FL PPQ
    //   position. No reload or re-stretch is required.
    // ---------------------------------------------------------

    if (bridgeEnabled.load()
        && playing
        && preview != nullptr
        && preview->buffer.getNumSamples() > 0
        && bpm > 0.0)
    {
        const int loopSamples =
            preview->buffer.getNumSamples();

        const int sourceChannels =
            preview->buffer.getNumChannels();

        const int outputChannels =
            buffer.getNumChannels();

        if (sourceChannels > 0
            && outputChannels > 0)
        {
            constexpr double loopBeats =
                16.0;

            double loopPositionBeats =
                std::fmod(
                    ppq,
                    loopBeats);

            if (loopPositionBeats < 0.0)
            {
                loopPositionBeats +=
                    loopBeats;
            }

            const double loopProgress =
                loopPositionBeats
                / loopBeats;

            int sourcePosition =
                static_cast<int>(
                    std::floor(
                        loopProgress
                        * static_cast<double>(
                            loopSamples)));

            sourcePosition =
                juce::jlimit(
                    0,
                    loopSamples - 1,
                    sourcePosition);

            int destinationPosition = 0;
            int remaining =
                buffer.getNumSamples();

            while (remaining > 0)
            {
                const int available =
                    loopSamples
                    - sourcePosition;

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

                    buffer.addFrom(
                        channel,
                        destinationPosition,
                        preview->buffer,
                        sourceChannel,
                        sourcePosition,
                        samplesToCopy);
                }

                destinationPosition +=
                    samplesToCopy;

                remaining -=
                    samplesToCopy;

                sourcePosition +=
                    samplesToCopy;

                if (sourcePosition
                    >= loopSamples)
                {
                    sourcePosition = 0;
                }
            }
        }
    }

    // Host transport information continues being sent
    // even while Bridge is OFF.
    //
    // That means the desktop app always knows the current
    // BPM / PPQ / sample rate and can prepare previews.
    const double nowMs =
        juce::Time::
            getMillisecondCounterHiRes();

    if (nowMs - lastSendTimeMs
        >= 10.0)
    {
        lastSendTimeMs =
            nowMs;

        sendHostState(
            bpm,
            playing,
            ppq);
    }
}

void
LoopBridgeAudioProcessor::sendHostState(
    double bpm,
    bool playing,
    double ppq)
{
    const juce::String message =
        "BPM:"
        + juce::String(bpm, 6)
        + ";PLAYING:"
        + juce::String(
            playing ? 1 : 0)
        + ";PPQ:"
        + juce::String(ppq, 9)
        + ";SR:"
        + juce::String(
            currentSampleRate,
            1);

    sendSocket.write(
        "127.0.0.1",
        49152,
        message.toRawUTF8(),
        static_cast<int>(
            message.getNumBytesAsUTF8()));
}

void
LoopBridgeAudioProcessor::timerCallback()
{
    while (true)
    {
        char buffer[2048] {};

        const int bytesRead =
            receiveSocket.read(
                buffer,
                sizeof(buffer) - 1,
                false);

        if (bytesRead <= 0)
            break;

        buffer[bytesRead] =
            '\0';

        handleDesktopMessage(
            juce::String::fromUTF8(
                buffer,
                bytesRead));
    }
}

void
LoopBridgeAudioProcessor::handleDesktopMessage(
    const juce::String& message)
{
    if (message.startsWith(
            "BRIDGE:"))
    {
        const auto value =
            message
                .fromFirstOccurrenceOf(
                    "BRIDGE:",
                    false,
                    false)
                .trim();

        bridgeEnabled.store(
            value.getIntValue() != 0);

        return;
    }

    if (message.startsWith(
            "LOAD:"))
    {
        const auto path =
            message
                .fromFirstOccurrenceOf(
                    "LOAD:",
                    false,
                    false)
                .trim();

        if (path.isEmpty())
            return;

        loadPreviewFile(
            juce::File(path));
    }
}

void
LoopBridgeAudioProcessor::loadPreviewFile(
    const juce::File& file)
{
    if (!file.existsAsFile())
        return;

    std::unique_ptr<
        juce::AudioFormatReader>
        reader(
            formatManager
                .createReaderFor(file));

    if (reader == nullptr)
        return;

    if (reader->lengthInSamples <= 0
        || reader->lengthInSamples
            > std::numeric_limits<int>::max())
    {
        return;
    }

    auto newPreview =
        std::make_shared<
            PreviewData>();

    newPreview->sampleRate =
        reader->sampleRate;

    const int channels =
        static_cast<int>(
            reader->numChannels);

    const int samples =
        static_cast<int>(
            reader->lengthInSamples);

    newPreview->buffer.setSize(
        channels,
        samples);

    reader->read(
        &newPreview->buffer,
        0,
        samples,
        0,
        true,
        true);

    if (currentSampleRate > 0.0
        && std::abs(
               newPreview->sampleRate
               - currentSampleRate)
            > 0.5)
    {
        return;
    }

    std::atomic_store(
        &previewData,
        std::move(newPreview));
}

bool
LoopBridgeAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor*
LoopBridgeAudioProcessor::createEditor()
{
    return new LoopBridgeAudioProcessorEditor(
        *this);
}

void
LoopBridgeAudioProcessor::getStateInformation(
    juce::MemoryBlock&)
{
}

void
LoopBridgeAudioProcessor::setStateInformation(
    const void*,
    int)
{
}

juce::AudioProcessor*
JUCE_CALLTYPE createPluginFilter()
{
    return new LoopBridgeAudioProcessor();
}