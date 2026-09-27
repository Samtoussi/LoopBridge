#pragma once

#include <JuceHeader.h>

#include <atomic>
#include <memory>

class LoopBridgeAudioProcessor
    : public juce::AudioProcessor,
      private juce::Timer
{
public:
    LoopBridgeAudioProcessor();
    ~LoopBridgeAudioProcessor() override;

    void prepareToPlay(
        double sampleRate,
        int samplesPerBlock) override;

    void releaseResources() override;

#ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported(
        const BusesLayout& layouts) const override;
#endif

    void processBlock(
        juce::AudioBuffer<float>&,
        juce::MidiBuffer&) override;

    juce::AudioProcessorEditor*
    createEditor() override;

    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;

    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;

    void setCurrentProgram(int index) override;

    const juce::String
    getProgramName(int index) override;

    void changeProgramName(
        int index,
        const juce::String& newName) override;

    void getStateInformation(
        juce::MemoryBlock& destData) override;

    void setStateInformation(
        const void* data,
        int sizeInBytes) override;

    double getHostBpm() const
    {
        return hostBpm.load();
    }

private:
    struct PreviewData
    {
        juce::AudioBuffer<float> buffer;
        double sampleRate = 0.0;
    };

    void timerCallback() override;

    void handleDesktopMessage(
        const juce::String& message);

    void loadPreviewFile(
        const juce::File& file);

    void sendHostState(
        double bpm,
        bool playing,
        double ppq);

    juce::DatagramSocket sendSocket;
    juce::DatagramSocket receiveSocket;

    juce::AudioFormatManager
        formatManager;

    std::shared_ptr<PreviewData>
        previewData;

    std::atomic<double>
        hostBpm { 0.0 };

    // Desktop Bridge ON/OFF state.
    // Starts enabled so existing behaviour is preserved.
    std::atomic<bool>
        bridgeEnabled { true };

    double currentSampleRate = 0.0;
    double lastSendTimeMs = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(
        LoopBridgeAudioProcessor)
};