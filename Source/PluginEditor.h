#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class LoopBridgeAudioProcessorEditor
    : public juce::AudioProcessorEditor,
      private juce::Timer
{
public:
    explicit LoopBridgeAudioProcessorEditor(
        LoopBridgeAudioProcessor&
    );

    ~LoopBridgeAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    LoopBridgeAudioProcessor& audioProcessor;

    double displayedBpm { 0.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(
        LoopBridgeAudioProcessorEditor
    )
};