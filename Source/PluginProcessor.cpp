#include "PluginProcessor.h"
#include "PluginEditor.h"

LoopBridgeAudioProcessor::LoopBridgeAudioProcessor()
    : AudioProcessor(
          BusesProperties()
              .withInput(
                  "Input",
                  juce::AudioChannelSet::stereo(),
                  true
              )
              .withOutput(
                  "Output",
                  juce::AudioChannelSet::stereo(),
                  true
              )
      )
{
}

LoopBridgeAudioProcessor::~LoopBridgeAudioProcessor()
{
}

const juce::String LoopBridgeAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool LoopBridgeAudioProcessor::acceptsMidi() const
{
    return false;
}

bool LoopBridgeAudioProcessor::producesMidi() const
{
    return false;
}

bool LoopBridgeAudioProcessor::isMidiEffect() const
{
    return false;
}

double LoopBridgeAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int LoopBridgeAudioProcessor::getNumPrograms()
{
    return 1;
}

int LoopBridgeAudioProcessor::getCurrentProgram()
{
    return 0;
}

void LoopBridgeAudioProcessor::setCurrentProgram(int)
{
}

const juce::String LoopBridgeAudioProcessor::getProgramName(int)
{
    return {};
}

void LoopBridgeAudioProcessor::changeProgramName(
    int,
    const juce::String&
)
{
}

void LoopBridgeAudioProcessor::prepareToPlay(
    double,
    int
)
{
}

void LoopBridgeAudioProcessor::releaseResources()
{
}

bool LoopBridgeAudioProcessor::isBusesLayoutSupported(
    const BusesLayout& layouts
) const
{
    const auto& input = layouts.getMainInputChannelSet();
    const auto& output = layouts.getMainOutputChannelSet();

    if (input != juce::AudioChannelSet::mono()
        && input != juce::AudioChannelSet::stereo())
    {
        return false;
    }

    return input == output;
}

void LoopBridgeAudioProcessor::processBlock(
    juce::AudioBuffer<float>&,
    juce::MidiBuffer&
)
{
    if (auto* playHead = getPlayHead())
    {
        if (auto position = playHead->getPosition())
        {
            if (auto bpm = position->getBpm())
            {
                hostBpm.store(*bpm);
            }
        }
    }

    // V0 feasibility build:
    // Audio passes through unchanged.
}

double LoopBridgeAudioProcessor::getHostBpm() const
{
    return hostBpm.load();
}

bool LoopBridgeAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor*
LoopBridgeAudioProcessor::createEditor()
{
    return new LoopBridgeAudioProcessorEditor(*this);
}

void LoopBridgeAudioProcessor::getStateInformation(
    juce::MemoryBlock&
)
{
}

void LoopBridgeAudioProcessor::setStateInformation(
    const void*,
    int
)
{
}

juce::AudioProcessor*
JUCE_CALLTYPE createPluginFilter()
{
    return new LoopBridgeAudioProcessor();
}