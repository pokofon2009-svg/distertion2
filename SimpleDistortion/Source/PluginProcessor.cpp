#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>

class DistortionProcessor : public juce::AudioProcessor
{
public:
    DistortionProcessor()
        : AudioProcessor(BusesProperties()
                             .withInput("Input", juce::AudioChannelSet::stereo(), true)
                             .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
          apvts(*this, nullptr, "PARAMS", createLayout())
    {
        driveParam = apvts.getRawParameterValue("drive");
        toneParam  = apvts.getRawParameterValue("tone");
        mixParam   = apvts.getRawParameterValue("mix");
    }

    //==========================================================================
    void prepareToPlay(double sampleRate, int) override
    {
        sr = sampleRate;
        driveSmooth.reset(sr, 0.03);
        toneSmooth.reset(sr, 0.03);
        mixSmooth.reset(sr, 0.03);
        driveSmooth.setCurrentAndTargetValue(driveParam->load());
        toneSmooth.setCurrentAndTargetValue(toneParam->load());
        mixSmooth.setCurrentAndTargetValue(mixParam->load());
        lpState[0] = lpState[1] = 0.0f;
    }

    void releaseResources() override {}

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override
    {
        const auto out = layouts.getMainOutputChannelSet();
        if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
            return false;
        return out == layouts.getMainInputChannelSet();
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals noDenormals;

        const int numCh      = juce::jmin(buffer.getNumChannels(), 2);
        const int numSamples = buffer.getNumSamples();

        for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
            buffer.clear(ch, 0, numSamples);

        driveSmooth.setTargetValue(driveParam->load());
        toneSmooth.setTargetValue(toneParam->load());
        mixSmooth.setTargetValue(mixParam->load());

        for (int i = 0; i < numSamples; ++i)
        {
            const float driveGain = juce::Decibels::decibelsToGain(driveSmooth.getNextValue());
            const float cutoff    = toneSmooth.getNextValue();
            const float mix       = mixSmooth.getNextValue();

            // one-pole low-pass coefficient
            const float a = 1.0f - std::exp(-juce::MathConstants<float>::twoPi
                                            * cutoff / static_cast<float>(sr));

            for (int ch = 0; ch < numCh; ++ch)
            {
                float* data = buffer.getWritePointer(ch);
                const float dry = data[i];

                float wet = std::tanh(dry * driveGain);
                lpState[ch] += a * (wet - lpState[ch]);
                wet = lpState[ch];

                data[i] = dry * (1.0f - mix) + wet * mix;
            }
        }
    }

    //==========================================================================
    juce::AudioProcessorEditor* createEditor() override
    {
        return new juce::GenericAudioProcessorEditor(*this);
    }
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Simple Distortion"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override
    {
        if (auto xml = apvts.copyState().createXml())
            copyXmlToBinary(*xml, destData);
    }

    void setStateInformation(const void* data, int size) override
    {
        if (auto xml = getXmlFromBinary(data, size))
            if (xml->hasTagName(apvts.state.getType()))
                apvts.replaceState(juce::ValueTree::fromXml(*xml));
    }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        using namespace juce;
        AudioProcessorValueTreeState::ParameterLayout layout;

        layout.add(std::make_unique<AudioParameterFloat>(
            ParameterID{"drive", 1}, "Drive",
            NormalisableRange<float>(0.0f, 40.0f, 0.1f), 12.0f,
            AudioParameterFloatAttributes().withLabel("dB")));

        layout.add(std::make_unique<AudioParameterFloat>(
            ParameterID{"tone", 1}, "Tone",
            NormalisableRange<float>(500.0f, 20000.0f, 1.0f, 0.3f), 8000.0f,
            AudioParameterFloatAttributes().withLabel("Hz")));

        layout.add(std::make_unique<AudioParameterFloat>(
            ParameterID{"mix", 1}, "Mix",
            NormalisableRange<float>(0.0f, 1.0f, 0.01f), 1.0f));

        return layout;
    }

    juce::AudioProcessorValueTreeState apvts;
    std::atomic<float>* driveParam = nullptr;
    std::atomic<float>* toneParam  = nullptr;
    std::atomic<float>* mixParam   = nullptr;

    juce::SmoothedValue<float> driveSmooth, toneSmooth, mixSmooth;
    float lpState[2] = {0.0f, 0.0f};
    double sr = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DistortionProcessor)
};

// Entry point required by JUCE
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DistortionProcessor();
}
