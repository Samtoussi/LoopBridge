#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <signalsmith-stretch/signalsmith-stretch.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

// One immutable job, used by the existing desktop background-render future.
namespace PreviewRenderer
{
struct Request
{
    juce::File source;
    juce::File hostFile;
    juce::String key;
    uint64_t generation = 0;
    double sourceBpm = 0.0;
    double bpm = 0.0;
    double sampleRate = 0.0;
    int semitones = 0;
    bool host = false;
};

struct Result
{
    Request request;
    std::shared_ptr<const juce::AudioBuffer<float>> buffer;
    juce::String error;
    double milliseconds = 0.0;
};

inline juce::String cacheDescriptor(const Request& request, const juce::String& identity)
{
    return identity + "|" + juce::String(request.source.getSize()) + "|"
        + juce::String(request.source.getLastModificationTime().toMilliseconds()) + "|"
        + juce::String(request.semitones) + "|" + (request.host ? "host" : "solo") + "|"
        + juce::String(request.sourceBpm, 8) + "|" + juce::String(request.bpm, 8) + "|"
        + juce::String(request.sampleRate, 8);
}

inline Result render(const Request& request)
{
    Result result;
    result.request = request;
    const auto started = juce::Time::getMillisecondCounterHiRes();
    try
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(request.source));
        if (!reader || reader->sampleRate <= 0.0 || reader->numChannels == 0
            || reader->lengthInSamples <= 0 || request.sampleRate <= 0.0
            || (request.host && (request.sourceBpm <= 0.0 || request.bpm <= 0.0)))
        {
            result.error = "Cannot read preview audio or invalid render settings";
            return result;
        }
        const double sourceRate = reader->sampleRate;
        const auto musicalSamples = request.sourceBpm > 0.0
            ? std::min<int64_t>(reader->lengthInSamples,
                static_cast<int64_t>(std::llround(16.0 * 60.0 / request.sourceBpm * sourceRate)))
            : reader->lengthInSamples;
        if (musicalSamples <= 0 || musicalSamples > std::numeric_limits<int>::max())
        {
            result.error = "Preview audio is too large";
            return result;
        }
        const int samples = static_cast<int>(musicalSamples);
        const int channels = static_cast<int>(reader->numChannels);
        juce::AudioBuffer<float> original(channels, samples);
        if (!reader->read(&original, 0, samples, 0, true, true))
        {
            result.error = "Cannot decode preview audio";
            return result;
        }
        const double lengthRatio = request.host ? request.sourceBpm / request.bpm : 1.0;
        const double requestedSamples = std::round(samples * lengthRatio);
        if (requestedSamples > std::numeric_limits<int>::max() / 2)
        {
            result.error = "Preview duration is too large";
            return result;
        }
        const int stretchedSamples = std::max(1, static_cast<int>(requestedSamples));
        juce::AudioBuffer<float> stretched;
        const juce::AudioBuffer<float>* processed = &original;
        if (request.host || request.semitones != 0)
        {
            signalsmith::stretch::SignalsmithStretch<float> stretch;
            stretch.presetDefault(channels, sourceRate);
            stretch.setTransposeSemitones(static_cast<float>(request.semitones));

            // exact() rejects inputs shorter than its seek window. Repeat a
            // short loop and extract a middle cycle, keeping pitch and duration.
            const int minimum = std::max(stretch.blockSamples() * 4,
                stretch.outputSeekLength(static_cast<float>(1.0 / lengthRatio)) + 1);
            const int repeats = samples < minimum ? (minimum + samples - 1) / samples + 2 : 1;
            if (static_cast<int64_t>(samples) * repeats > std::numeric_limits<int>::max()
                || static_cast<int64_t>(stretchedSamples) * repeats > std::numeric_limits<int>::max())
            {
                result.error = "Preview duration is too large";
                return result;
            }
            juce::AudioBuffer<float> repeated;
            const auto* input = &original;
            if (repeats > 1)
            {
                repeated.setSize(channels, samples * repeats);
                for (int c = 0; c < channels; ++c)
                    for (int r = 0; r < repeats; ++r)
                        repeated.copyFrom(c, r * samples, original, c, 0, samples);
                input = &repeated;
            }
            juce::AudioBuffer<float> complete(channels, stretchedSamples * repeats);
            std::vector<const float*> inputs(static_cast<size_t>(channels));
            std::vector<float*> outputs(static_cast<size_t>(channels));
            for (int c = 0; c < channels; ++c)
            {
                inputs[static_cast<size_t>(c)] = input->getReadPointer(c);
                outputs[static_cast<size_t>(c)] = complete.getWritePointer(c);
            }
            if (!stretch.exact(inputs.data(), input->getNumSamples(), outputs.data(), complete.getNumSamples()))
            {
                result.error = "Audio is too short to prepare a preview";
                return result;
            }
            stretched.setSize(channels, stretchedSamples);
            for (int c = 0; c < channels; ++c)
                stretched.copyFrom(c, 0, complete, c, (repeats / 2) * stretchedSamples, stretchedSamples);
            processed = &stretched;
        }
        const double outputLength = std::round(processed->getNumSamples() * request.sampleRate / sourceRate);
        if (outputLength > std::numeric_limits<int>::max())
        {
            result.error = "Preview output is too large";
            return result;
        }
        const int outputSamples = std::max(1, static_cast<int>(outputLength));
        auto output = std::make_shared<juce::AudioBuffer<float>>(channels, outputSamples);
        for (int c = 0; c < channels; ++c)
        {
            juce::LagrangeInterpolator interpolator;
            // The bounded-input overload wraps at the loop boundary and avoids
            // reading past short buffers or the interpolator's final lookahead.
            interpolator.process(sourceRate / request.sampleRate, processed->getReadPointer(c),
                output->getWritePointer(c), outputSamples, processed->getNumSamples(), processed->getNumSamples());
        }
        if (request.host)
        {
            request.hostFile.getParentDirectory().createDirectory();
            request.hostFile.deleteFile();
            juce::WavAudioFormat wav;
            auto stream = request.hostFile.createOutputStream();
            if (!stream)
            {
                result.error = "Cannot create prepared preview";
                return result;
            }
            std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(
                stream.get(), request.sampleRate, static_cast<unsigned int>(channels), 32, {}, 0));
            if (!writer)
            {
                result.error = "Cannot write prepared preview";
                return result;
            }
            stream.release();
            if (!writer->writeFromAudioSampleBuffer(*output, 0, outputSamples))
            {
                result.error = "Cannot write prepared preview samples";
                return result;
            }
            writer.reset();
        }
        result.buffer = std::move(output);
    }
    catch (const std::exception& error)
    {
        result.error = juce::String("Preview render failed: ") + error.what();
    }
    result.milliseconds = juce::Time::getMillisecondCounterHiRes() - started;
    return result;
}
}
