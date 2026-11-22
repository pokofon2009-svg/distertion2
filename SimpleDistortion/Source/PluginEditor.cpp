#include "PluginEditor.h"

//==============================================================================
DistortionProEditor::DistortionProEditor(DistortionProAudioProcessor& p)
    : AudioProcessorEditor(&p), proc(p), curve(p)
{
    setLookAndFeel(&laf);
    setSize(660, 440);

    addAndMakeVisible(curve);
    addAndMakeVisible(meterIn);
    addAndMakeVisible(meterOut);

    setupKnob(drive,   "drive",   "DRIVE");
    setupKnob(inGain,  "inGain",  "INPUT");
    setupKnob(lowCut,  "lowCut",  "LOW CUT");
    setupKnob(bias,    "bias",    "BIAS");
    setupKnob(tone,    "tone",    "TONE");
    setupKnob(outGain, "outGain", "OUTPUT");
    setupKnob(mix,     "mix",     "MIX");

    // Distortion type buttons
    const char* names[4] = {"SOFT", "HARD", "TUBE", "FOLD"};
    for (int i = 0; i < 4; ++i)
    {
        addAndMakeVisible(typeButtons[i]);
        typeButtons[i].setButtonText(names[i]);
        typeButtons[i].setClickingTogglesState(false);
        typeButtons[i].onClick = [this, i]
        {
            if (typeAttachment != nullptr)
                typeAttachment->setValueAsCompleteGesture(static_cast<float>(i));
        };
    }

    typeAttachment = std::make_unique<juce::ParameterAttachment>(
        *proc.apvts.getParameter("type"),
        [this](float v) { updateTypeButtons(juce::roundToInt(v)); });
    typeAttachment->sendInitialUpdate();

    startTimerHz(30);
}

DistortionProEditor::~DistortionProEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void DistortionProEditor::setupKnob(Knob& k, const juce::String& paramID, const juce::String& text)
{
    k.slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 90, 18);
    addAndMakeVisible(k.slider);

    k.label.setText(text, juce::dontSendNotification);
    k.label.setJustificationType(juce::Justification::centred);
    k.label.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
    k.label.setColour(juce::Label::textColourId, pro::dim);
    addAndMakeVisible(k.label);

    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        proc.apvts, paramID, k.slider);
}

void DistortionProEditor::updateTypeButtons(int index)
{
    for (int i = 0; i < 4; ++i)
        typeButtons[i].setToggleState(i == index, juce::dontSendNotification);
    curve.refresh();
}

void DistortionProEditor::timerCallback()
{
    meterIn.push(proc.inLevel.exchange(0.0f));
    meterOut.push(proc.outLevel.exchange(0.0f));
    curve.refresh();
}

//==============================================================================
void DistortionProEditor::paint(juce::Graphics& g)
{
    g.fillAll(pro::bg);

    // header
    g.setColour(pro::panel);
    g.fillRect(0, 0, getWidth(), 50);
    g.setColour(pro::accent);
    g.fillRect(0, 50, getWidth(), 2);

    g.setColour(pro::text);
    g.setFont(juce::Font(juce::FontOptions(24.0f, juce::Font::bold)));
    g.drawText("DISTORTION PRO", 20, 0, 320, 50, juce::Justification::centredLeft);

    g.setColour(pro::dim);
    g.setFont(juce::Font(juce::FontOptions(12.0f)));
    g.drawText("multi-mode saturator", getWidth() - 220, 0, 200, 50, juce::Justification::centredRight);

    // panel behind the big drive knob
    g.setColour(pro::panel);
    g.fillRoundedRectangle(428.0f, 108.0f, 212.0f, 212.0f, 8.0f);
}

void DistortionProEditor::resized()
{
    const int margin = 20;

    // type buttons
    const int btnW = (getWidth() - 2 * margin - 3 * 8) / 4;
    for (int i = 0; i < 4; ++i)
        typeButtons[i].setBounds(margin + i * (btnW + 8), 62, btnW, 32);

    // middle row
    curve.setBounds(20, 108, 300, 212);
    meterIn.setBounds(334, 108, 34, 212);
    meterOut.setBounds(378, 108, 34, 212);

    drive.label.setBounds(428, 114, 212, 16);
    drive.slider.setBounds(438, 132, 192, 184);

    // bottom row of knobs
    Knob* row[6] = {&inGain, &lowCut, &bias, &tone, &outGain, &mix};
    const int kw = (getWidth() - 2 * margin) / 6;
    for (int k = 0; k < 6; ++k)
    {
        row[k]->label.setBounds(margin + k * kw, 330, kw, 16);
        row[k]->slider.setBounds(margin + k * kw + 4, 346, kw - 8, 90);
    }
}
