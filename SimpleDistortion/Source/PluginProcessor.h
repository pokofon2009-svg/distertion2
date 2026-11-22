#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <cmath>

//==============================================================================
// Waveshaper shared by the DSP and by the curve display in the editor.
// type: 0 = Soft (tanh), 1 = Hard clip, 2 = Tube (asymmetric), 3 = Fold
inline float shapeSample(float x, int type)
{
    switch (type)
    {
        case 0:  return std::tanh(x);
        case 1:  return juce::jlimit(-1.0f, 1.0f, x);
        case 2:  return x >= 0.0f ? std::tanh(x) : 0.75f * std::tanh(x / 0.75f);
        default: return std::sin(x);
    }
}

// Bias shifts the signal before shaping; subtracting shape(bias) keeps 0 -> 0.
inline float processShaper(float x, int type, float bias)
{
    return shapeSample(x + bias, type) - shapeSample(bias, type);
}

//==============================================================================
class DistortionProAudioProcessor : public juce::AudioProcessor
{
public:
    DistortionProAudioProcessor();
    ~DistortionProAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Distortion Pro"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    // Peak levels written by the audio thread, read (and reset) by the GUI.
    std::atomic<float> inLevel{0.0f};
    std::atomic<float> outLevel{0.0f};

private:
    std::atomic<float>* inGainP  = nullptr;
    std::atomic<float>* lowCutP  = nullptr;
    std::atomic<float>* driveP   = nullptr;
    std::atomic<float>* typeP    = nullptr;
    std::atomic<float>* biasP    = nullptr;
    std::atomic<float>* toneP    = nullptr;
    std::atomic<float>* outGainP = nullptr;
    std::atomic<float>* mixP     = nullptr;

    juce::SmoothedValue<float> inGainSm, driveSm, biasSm, mixSm, outGainSm;

    juce::dsp::StateVariableTPTFilter<float> lowCut, toneFilter, dcBlock;

    juce::dsp::Oversampling<float> oversampling{
        2, 1, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true};

    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> dryDelay{1024};
    juce::AudioBuffer<float> dryBuffer;

    double sr = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DistortionProAudioProcessor)
};
