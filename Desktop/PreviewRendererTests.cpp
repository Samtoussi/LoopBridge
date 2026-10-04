#include "PreviewRenderer.h"
#include <juce_cryptography/juce_cryptography.h>
#include <future>
#include <iostream>

int main()
{
    int failed = 0;
    const auto check = [&](bool ok, const char* name)
    {
        std::cout << (ok ? "[PASS] " : "[FAIL] ") << name << '\n';
        if (!ok) ++failed;
    };
    const auto directory = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("LoopBridgeRenderTests-" + juce::Uuid().toString());
    directory.createDirectory();
    const auto makeSource = [&](const juce::String& name, int samples)
    {
        auto file = directory.getChildFile(name);
        juce::AudioBuffer<float> buffer(2, samples);
        for (int i = 0; i < samples; ++i)
        {
            const auto sample = static_cast<float>(0.5 * std::sin(juce::MathConstants<double>::twoPi * 440.0 * i / 48000.0));
            buffer.setSample(0, i, sample);
            buffer.setSample(1, i, -0.5f * sample);
        }
        juce::WavAudioFormat wav;
        auto stream = file.createOutputStream();
        std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(stream.get(), 48000.0, 2, 32, {}, 0));
        if (!writer) return juce::File{};
        stream.release();
        writer->writeFromAudioSampleBuffer(buffer, 0, samples);
        return file;
    };
    const auto frequency = [](const juce::AudioBuffer<float>& buffer, double rate)
    {
        const int start = buffer.getNumSamples() / 4;
        const int end = buffer.getNumSamples() * 3 / 4;
        int crossings = 0;
        for (int i = start + 1; i < end; ++i)
            if (buffer.getSample(0, i - 1) <= 0.0f && buffer.getSample(0, i) > 0.0f) ++crossings;
        return crossings * rate / (end - start);
    };
    const auto source = makeSource("original.wav", 48000 * 8);
    const auto originalHash = juce::SHA256(source).toHexString();
    PreviewRenderer::Request request;
    request.source = source;
    request.sourceBpm = 120.0;
    request.sampleRate = 44100.0;
    request.semitones = 12;
    request.generation = 5;
    // Exercise the same immutable capture used by MainComponent's future.
    auto future = std::async(std::launch::async, [request] { return PreviewRenderer::render(request); });
    request.semitones = -12;
    auto solo = future.get();
    check(solo.buffer && solo.buffer->getNumSamples() == 44100 * 8, "Solo pitch preserves duration and converts sample rate");
    check(solo.request.semitones == 12 && solo.request.generation == 5, "Render captures settings independently of later UI changes");
    if (solo.buffer)
    {
        check(std::abs(frequency(*solo.buffer, 44100.0) - 880.0) < 3.0, "Solo octave-up frequency");
        float error = 0.0f;
        for (int i = 4096; i < solo.buffer->getNumSamples() - 4096; ++i)
            error = std::max(error, std::abs(solo.buffer->getSample(1, i) + 0.5f * solo.buffer->getSample(0, i)));
        check(error < 0.05f, "Stereo relationship preserved");
    }
    std::cout << "8-second stereo solo render: " << solo.milliseconds << " ms\n";
    request.host = true;
    request.bpm = 150.0;
    request.sampleRate = 48000.0;
    request.semitones = 7;
    request.hostFile = directory.getChildFile("host.wav");
    auto host = PreviewRenderer::render(request);
    check(host.buffer && host.buffer->getNumSamples() == 307200, "Host BPM ratio determines duration independently of pitch");
    if (host.buffer)
        check(std::abs(frequency(*host.buffer, 48000.0) - 440.0 * std::pow(2.0, 7.0 / 12.0)) < 3.0, "Combined host pitch and time stretch");
    check(request.hostFile.existsAsFile(), "Prepared host WAV written separately");
    std::cout << "8-second stereo host render including WAV write: " << host.milliseconds << " ms\n";
    request.host = false;
    request.sourceBpm = 0.0;
    request.bpm = 0.0;
    request.sampleRate = 48000.0;
    request.semitones = 0;
    auto unshifted = PreviewRenderer::render(request);
    check(unshifted.buffer && unshifted.buffer->getNumSamples() == 384000
        && std::abs(frequency(*unshifted.buffer, 48000.0) - 440.0) < 3.0, "Unknown BPM and zero pitch remain playable");
    std::cout << "Unshifted solo preparation: " << unshifted.milliseconds << " ms\n";
    request.semitones = -12;
    for (const int samples : { 1, 16, 127, 512 })
    {
        request.source = makeSource("short-" + juce::String(samples) + ".wav", samples);
        auto shortResult = PreviewRenderer::render(request);
        bool finite = shortResult.buffer && shortResult.buffer->getNumSamples() == samples;
        if (shortResult.buffer)
            for (int c = 0; c < shortResult.buffer->getNumChannels(); ++c)
                for (int i = 0; i < samples; ++i)
                    finite = finite && std::isfinite(shortResult.buffer->getSample(c, i));
        check(finite, ("Very short pitch render: " + juce::String(samples) + " samples").toRawUTF8());
    }
    request.source = source;
    const auto descriptor = PreviewRenderer::cacheDescriptor(request, "message|attachment");
    auto variant = request;
    variant.semitones = 0;
    check(descriptor != PreviewRenderer::cacheDescriptor(variant, "message|attachment"), "Cache separates pitches");
    variant = request; variant.host = true;
    check(descriptor != PreviewRenderer::cacheDescriptor(variant, "message|attachment"), "Cache separates routes");
    variant = request; variant.sampleRate = 44100;
    check(descriptor != PreviewRenderer::cacheDescriptor(variant, "message|attachment"), "Cache separates sample rates");
    variant = request; variant.bpm = 130;
    check(descriptor != PreviewRenderer::cacheDescriptor(variant, "message|attachment"), "Cache separates BPM settings");
    check(descriptor != PreviewRenderer::cacheDescriptor(request, "message|other-attachment"), "Cache separates same-named attachments");
    variant = request; variant.generation++;
    check(descriptor == PreviewRenderer::cacheDescriptor(variant, "message|attachment"), "Cache reusable across selection generations");
    check(originalHash == juce::SHA256(source).toHexString(), "Original audio unchanged");
    request.source = directory.getChildFile("missing.wav");
    check(!PreviewRenderer::render(request).buffer, "Missing audio fails safely");
    directory.deleteRecursively();
    std::cout << "Failed: " << failed << '\n';
    return failed == 0 ? 0 : 1;
}
