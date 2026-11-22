#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
namespace
{
    using ToText   = std::function<juce::String(float, int)>;
    using FromText = std::function<float(const juce::String&)>;

    std::unique_ptr<juce::AudioParameterFloat> makeFloat(const juce::String& id,
                                                         const juce::String& name,
                                                         juce::NormalisableRange<float> range,
                                                         float defaultValue,
                                                         ToText toText,
                                                         FromText fromText)
    {
        return std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{id, 1}, name, range, defaultValue,
            juce::AudioParameterFloatAttributes()
                .withStringFromValueFunction(std::move(toText))
                .withValueFromStringFunction(std::move(fromText)));
    }

    const ToText dbText  = [](float v, int) { return juce::String(v, 1) + " dB"; };
    const ToText hzText  = [](float v, int)
    {
        return v >= 1000.0f ? juce::String(v / 1000.0f, 1) + " kHz"
                            : juce::String(juce::roundToInt(v)) + " Hz";
    };
    const ToText pctText = [](float v, int) { return juce::String(juce::roundToInt(v * 100.0f)) + " %"; };
    const ToText numText = [](float v, int) { return juce::String(v, 2); };

    const FromText plainFrom = [](const juce::String& t) { return t.getFloatValue(); };
    const FromText hzFrom    = [](const juce::String& t)
    {
        const float v = t.getFloatValue();
        return t.containsIgnoreCase("k") ? v * 1000.0f : v;
    };
    const FromText pctFrom   = [](const juce::String& t) { return t.getFloatValue() / 100.0f; };

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        layout.add(makeFloat("inGain", "Input", {-24.0f, 24.0f, 0.1f}, 0.0f, dbText, plainFrom));
        layout.add(makeFloat("lowCut", "Low Cut", {20.0f, 1000.0f, 1.0f, 0.4f}, 20.0f, hzText, hzFrom));
        layout.add(makeFloat("drive", "Drive", {0.0f, 48.0f, 0.1f}, 12.0f, dbText, plainFrom));
        layout.add(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID{"type", 1}, "Type",
            juce::StringArray{"Soft", "Hard", "Tube", "Fold"}, 0));
        layout.add(makeFloat("bias", "Bias", {-1.0f, 1.0f, 0.01f}, 0.0f, numText, plainFrom));
        layout.add(makeFloat("tone", "Tone", {1000.0f, 20000.0f, 1.0f, 0.3f}, 12000.0f, hzText, hzFrom));
        layout.add(makeFloat("outGain", "Output", {-24.0f, 12.0f, 0.1f}, 0.0f, dbText, plainFrom));
        layout.add(makeFloat("mix", "Mix", {0.0f, 1.0f, 0.01f}, 1.0f, pctText, pctFrom));

        return layout;
    }
}

//==============================================================================
DistortionProAudioProcessor::DistortionProAudioProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMS", createLayout())
{
    inGainP  = apvts.getRawParameterValue("inGain");
    lowCutP  = apvts.getRawParameterValue("lowCut");
    driveP   = apvts.getRawParameterValue("drive");
    typeP    = apvts.getRawParameterValue("type");
    biasP    = apvts.getRawParameterValue("bias");
    toneP    = apvts.getRawParameterValue("tone");
    outGainP = apvts.getRawParameterValue("outGain");
    mixP     = apvts.getRawParameterValue("mix");
}

bool DistortionProAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return out == layouts.getMainInputChannelSet();
}

//==============================================================================
void DistortionProAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;

    juce::dsp::ProcessSpec spec{sampleRate, static_cast<juce::uint32>(samplesPerBlock), 2};

    lowCut.prepare(spec);
    lowCut.setType(juce::dsp::StateVariableTPTFilterType::highpass);
    toneFilter.prepare(spec);
    toneFilter.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
    dcBlock.prepare(spec);
    dcBlock.setType(juce::dsp::StateVariableTPTFilterType::highpass);
    dcBlock.setCutoffFrequency(10.0f);

    oversampling.initProcessing(static_cast<size_t>(samplesPerBlock));
    oversampling.reset();
    const int latency = static_cast<int>(std::round(oversampling.getLatencyInSamples()));
    setLatencySamples(latency);

    dryDelay.prepare(spec);
    dryDelay.setDelay(static_cast<float>(latency));
    dryDelay.reset();

    dryBuffer.setSize(2, samplesPerBlock);
    dryBuffer.clear();

    inGainSm.reset(sampleRate, 0.03);
    mixSm.reset(sampleRate, 0.03);
    outGainSm.reset(sampleRate, 0.03);
    driveSm.reset(sampleRate * 2.0, 0.03);
    biasSm.reset(sampleRate * 2.0, 0.03);

    inGainSm.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(inGainP->load()));
    outGainSm.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(outGainP->load()));
    driveSm.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(driveP->load()));
    biasSm.setCurrentAndTargetValue(biasP->load());
    mixSm.setCurrentAndTargetValue(mixP->load());

    inLevel.store(0.0f);
    outLevel.store(0.0f);
}

//==============================================================================
void DistortionProAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numCh      = juce::jmin(buffer.getNumChannels(), 2);

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear(ch, 0, numSamples);

    if (numCh == 0 || numSamples == 0)
        return;

    float* chan[2] = {buffer.getWritePointer(0), numCh > 1 ? buffer.getWritePointer(1) : nullptr};

    // ---- 1. Input gain + input meter -----------------------------------------
    inGainSm.setTargetValue(juce::Decibels::decibelsToGain(inGainP->load()));
    float inPeak = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        const float g = inGainSm.getNextValue();
        for (int ch = 0; ch < numCh; ++ch)
        {
            chan[ch][i] *= g;
            inPeak = juce::jmax(inPeak, std::abs(chan[ch][i]));
        }
    }

    // ---- 2. Keep a delayed dry copy (compensates oversampling latency) --------
    for (int ch = 0; ch < numCh; ++ch)
        dryBuffer.copyFrom(ch, 0, buffer, ch, 0, numSamples);

    juce::dsp::AudioBlock<float> dryAll(dryBuffer);
    auto dryBlock = dryAll.getSubBlock(0, static_cast<size_t>(numSamples))
                          .getSubsetChannelBlock(0, static_cast<size_t>(numCh));
    dryDelay.process(juce::dsp::ProcessContextReplacing<float>(dryBlock));

    // ---- 3. Low cut before the distortion ------------------------------------
    juce::dsp::AudioBlock<float> all(buffer);
    auto wetBlock = all.getSubsetChannelBlock(0, static_cast<size_t>(numCh));

    lowCut.setCutoffFrequency(lowCutP->load());
    lowCut.process(juce::dsp::ProcessContextReplacing<float>(wetBlock));

    // ---- 4. Oversampled waveshaper -------------------------------------------
    const int type = juce::roundToInt(typeP->load());
    driveSm.setTargetValue(juce::Decibels::decibelsToGain(driveP->load()));
    biasSm.setTargetValue(biasP->load());

    auto up = oversampling.processSamplesUp(wetBlock);
    const size_t upN = up.getNumSamples();

    for (size_t i = 0; i < upN; ++i)
    {
        const float drive = driveSm.getNextValue();
        const float bias  = biasSm.getNextValue();

        for (size_t ch = 0; ch < up.getNumChannels(); ++ch)
        {
            float* d = up.getChannelPointer(ch);
            d[i] = processShaper(d[i] * drive, type, bias);
        }
    }

    oversampling.processSamplesDown(wetBlock);

    // ---- 5. Tone (low-pass) and DC blocker -----------------------------------
    toneFilter.setCutoffFrequency(juce::jmin(toneP->load(), static_cast<float>(sr * 0.45)));
    toneFilter.process(juce::dsp::ProcessContextReplacing<float>(wetBlock));
    dcBlock.process(juce::dsp::ProcessContextReplacing<float>(wetBlock));

    // ---- 6. Mix, output gain, output meter -----------------------------------
    mixSm.setTargetValue(mixP->load());
    outGainSm.setTargetValue(juce::Decibels::decibelsToGain(outGainP->load()));
    float outPeak = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        const float mix = mixSm.getNextValue();
        const float og  = outGainSm.getNextValue();

        for (int ch = 0; ch < numCh; ++ch)
        {
            const float dry = dryBuffer.getReadPointer(ch)[i];
            const float out = (dry * (1.0f - mix) + chan[ch][i] * mix) * og;
            chan[ch][i] = out;
            outPeak = juce::jmax(outPeak, std::abs(out));
        }
    }

    inLevel.store(juce::jmax(inLevel.load(), inPeak));
    outLevel.store(juce::jmax(outLevel.load(), outPeak));
}

//==============================================================================
juce::AudioProcessorEditor* DistortionProAudioProcessor::createEditor()
{
    return new DistortionProEditor(*this);
}

void DistortionProAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void DistortionProAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

// Entry point required by JUCE
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DistortionProAudioProcessor();
}
