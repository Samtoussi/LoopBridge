#include "PluginEditor.h"

LoopBridgeAudioProcessorEditor::LoopBridgeAudioProcessorEditor(
    LoopBridgeAudioProcessor& processor
)
    : AudioProcessorEditor(&processor),
      audioProcessor(processor)
{
    setSize(420, 220);

    startTimerHz(10);
}

LoopBridgeAudioProcessorEditor::~LoopBridgeAudioProcessorEditor()
{
    stopTimer();
}

void LoopBridgeAudioProcessorEditor::timerCallback()
{
    const auto newBpm = audioProcessor.getHostBpm();

    if (newBpm != displayedBpm)
    {
        displayedBpm = newBpm;
        repaint();
    }
}

void LoopBridgeAudioProcessorEditor::paint(
    juce::Graphics& g
)
{
    g.fillAll(
        juce::Colour::fromRGB(20, 20, 24)
    );

    g.setColour(juce::Colours::white);

    g.setFont(
        juce::Font(
            juce::FontOptions(28.0f, juce::Font::bold)
        )
    );

    g.drawFittedText(
        "LOOPBRIDGE",
        0,
        40,
        getWidth(),
        40,
        juce::Justification::centred,
        1
    );

    g.setColour(
        juce::Colours::white.withAlpha(0.65f)
    );

    g.setFont(
        juce::Font(
            juce::FontOptions(15.0f)
        )
    );

    g.drawFittedText(
        "HOST BPM",
        0,
        100,
        getWidth(),
        25,
        juce::Justification::centred,
        1
    );

    g.setColour(juce::Colours::white);

    g.setFont(
        juce::Font(
            juce::FontOptions(28.0f, juce::Font::bold)
        )
    );

    const auto bpmText =
        displayedBpm > 0.0
            ? juce::String(displayedBpm, 2)
            : "--";

    g.drawFittedText(
        bpmText,
        0,
        130,
        getWidth(),
        40,
        juce::Justification::centred,
        1
    );
}

void LoopBridgeAudioProcessorEditor::resized()
{
}