#pragma once

#include "PluginProcessor.h"

//==============================================================================
namespace pro
{
    inline const juce::Colour bg     {0xff0e1116};
    inline const juce::Colour panel  {0xff171b23};
    inline const juce::Colour knob   {0xff1f242e};
    inline const juce::Colour track  {0xff2b313d};
    inline const juce::Colour grid   {0xff2b313d};
    inline const juce::Colour accent {0xffff6b35};
    inline const juce::Colour text   {0xffd9dde5};
    inline const juce::Colour dim    {0xff7b8393};
}

//==============================================================================
class ProLookAndFeel : public juce::LookAndFeel_V4
{
public:
    ProLookAndFeel()
    {
        setColour(juce::Slider::textBoxTextColourId, pro::text);
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour(juce::TextButton::buttonColourId, pro::panel);
        setColour(juce::TextButton::buttonOnColourId, pro::accent);
        setColour(juce::TextButton::textColourOffId, pro::dim);
        setColour(juce::TextButton::textColourOnId, juce::Colours::black);
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float pos, float startAngle, float endAngle, juce::Slider&) override
    {
        auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(4.0f);
        const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre  = bounds.getCentre();
        const float angle  = startAngle + pos * (endAngle - startAngle);

        const float trackW = juce::jmax(3.0f, radius * 0.10f);
        const float arcR   = radius - trackW * 0.5f;

        juce::Path trackPath;
        trackPath.addCentredArc(centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour(pro::track);
        g.strokePath(trackPath, juce::PathStrokeType(trackW, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));

        juce::Path valuePath;
        valuePath.addCentredArc(centre.x, centre.y, arcR, arcR, 0.0f, startAngle, angle, true);
        g.setColour(pro::accent);
        g.strokePath(valuePath, juce::PathStrokeType(trackW, juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded));

        const float bodyR = radius - trackW * 2.2f;
        g.setColour(pro::knob);
        g.fillEllipse(centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);
        g.setColour(pro::grid);
        g.drawEllipse(centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f, 1.0f);

        juce::Path pointer;
        pointer.addRoundedRectangle(-1.5f, -bodyR + 4.0f, 3.0f, bodyR * 0.4f, 1.5f);
        g.setColour(pro::text);
        g.fillPath(pointer, juce::AffineTransform::rotation(angle).translated(centre.x, centre.y));
    }
};

//==============================================================================
// Draws the current waveshaper transfer curve (input vs. output).
class TransferCurve : public juce::Component
{
public:
    explicit TransferCurve(DistortionProAudioProcessor& p)
    {
        driveP = p.apvts.getRawParameterValue("drive");
        typeP  = p.apvts.getRawParameterValue("type");
        biasP  = p.apvts.getRawParameterValue("bias");
    }

    void refresh()
    {
        const float d = driveP->load(), t = typeP->load(), b = biasP->load();
        if (d != lastD || t != lastT || b != lastB)
        {
            lastD = d; lastT = t; lastB = b;
            repaint();
        }
    }

    void paint(juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour(pro::panel);
        g.fillRoundedRectangle(b, 8.0f);

        auto plot = b.reduced(14.0f);

        // grid
        for (int i = 0; i <= 4; ++i)
        {
            const float gx = plot.getX() + plot.getWidth() * (float) i / 4.0f;
            const float gy = plot.getY() + plot.getHeight() * (float) i / 4.0f;
            g.setColour(i == 2 ? pro::dim.withAlpha(0.6f) : pro::grid);
            g.drawLine(gx, plot.getY(), gx, plot.getBottom(), 1.0f);
            g.drawLine(plot.getX(), gy, plot.getRight(), gy, 1.0f);
        }

        // unity reference line
        g.setColour(pro::dim.withAlpha(0.5f));
        g.drawLine(plot.getX(), plot.getBottom(), plot.getRight(), plot.getY(), 1.0f);

        // curve
        const float driveGain = juce::Decibels::decibelsToGain(driveP->load());
        const int type        = juce::roundToInt(typeP->load());
        const float bias      = biasP->load();

        juce::Path curve;
        const int steps = juce::jmax(2, (int) plot.getWidth() * 2);
        for (int i = 0; i <= steps; ++i)
        {
            const float xn = juce::jmap((float) i, 0.0f, (float) steps, -1.0f, 1.0f);
            const float yn = juce::jlimit(-1.0f, 1.0f, processShaper(xn * driveGain, type, bias));
            const float px = plot.getX() + (xn + 1.0f) * 0.5f * plot.getWidth();
            const float py = plot.getBottom() - (yn + 1.0f) * 0.5f * plot.getHeight();
            if (i == 0) curve.startNewSubPath(px, py);
            else        curve.lineTo(px, py);
        }

        g.setColour(pro::accent.withAlpha(0.25f));
        g.strokePath(curve, juce::PathStrokeType(7.0f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
        g.setColour(pro::accent);
        g.strokePath(curve, juce::PathStrokeType(2.0f));

        g.setColour(pro::dim);
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.drawText("TRANSFER CURVE", getLocalBounds().reduced(10, 4), juce::Justification::topLeft);
    }

private:
    std::atomic<float>* driveP = nullptr;
    std::atomic<float>* typeP  = nullptr;
    std::atomic<float>* biasP  = nullptr;
    float lastD = -999.0f, lastT = -999.0f, lastB = -999.0f;
};

//==============================================================================
class LevelMeter : public juce::Component
{
public:
    explicit LevelMeter(const juce::String& labelText) : label(labelText) {}

    void push(float gain)
    {
        const float db = juce::Decibels::gainToDecibels(gain, -60.0f);
        const float n  = juce::jlimit(0.0f, 1.0f, juce::jmap(db, -60.0f, 6.0f, 0.0f, 1.0f));
        shown = juce::jmax(n, shown - 0.04f);
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour(pro::panel);
        g.fillRoundedRectangle(b, 8.0f);

        auto bar = b.reduced(7.0f).withTrimmedBottom(18.0f);
        g.setColour(pro::bg);
        g.fillRoundedRectangle(bar, 3.0f);

        const float h = bar.getHeight() * shown;
        if (h > 0.5f)
        {
            auto fill = bar.withTop(bar.getBottom() - h);
            g.setGradientFill(juce::ColourGradient(juce::Colour(0xffff3b30), bar.getX(), bar.getY(),
                                                   juce::Colour(0xff34c759), bar.getX(), bar.getBottom(), false));
            g.fillRoundedRectangle(fill, 3.0f);
        }

        // 0 dB marker
        const float zeroY = bar.getBottom() - bar.getHeight() * (60.0f / 66.0f);
        g.setColour(pro::text.withAlpha(0.7f));
        g.drawLine(bar.getX(), zeroY, bar.getRight(), zeroY, 1.0f);

        g.setColour(pro::dim);
        g.setFont(juce::Font(juce::FontOptions(11.0f, juce::Font::bold)));
        g.drawText(label, getLocalBounds().removeFromBottom(18), juce::Justification::centred);
    }

private:
    juce::String label;
    float shown = 0.0f;
};

//==============================================================================
class DistortionProEditor : public juce::AudioProcessorEditor,
                            private juce::Timer
{
public:
    explicit DistortionProEditor(DistortionProAudioProcessor&);
    ~DistortionProEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void setupKnob(Knob& k, const juce::String& paramID, const juce::String& text);
    void updateTypeButtons(int index);
    void timerCallback() override;

    DistortionProAudioProcessor& proc;
    ProLookAndFeel laf;

    TransferCurve curve;
    LevelMeter meterIn{"IN"}, meterOut{"OUT"};

    Knob drive, inGain, lowCut, bias, tone, outGain, mix;

    juce::TextButton typeButtons[4];
    std::unique_ptr<juce::ParameterAttachment> typeAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DistortionProEditor)
};
