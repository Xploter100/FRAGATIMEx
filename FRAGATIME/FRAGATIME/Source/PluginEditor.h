#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

// Estilo monocromatico: negro / grises / blanco
namespace Palette
{
    const juce::Colour bg     { 0xff0d0d0d };
    const juce::Colour panel  { 0xff161616 };
    const juce::Colour track  { 0xff3a3a3a };
    const juce::Colour dim    { 0xff7a7a7a };
    const juce::Colour fg     { 0xfff2f2f2 };
}

class MonoLook : public juce::LookAndFeel_V4
{
public:
    MonoLook();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h,
                           float pos, float startAngle, float endAngle, juce::Slider&) override;
};

class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& text);
    void resized() override;

private:
    juce::Slider slider;
    juce::Label label;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment;
};

class FragatimeEditor : public juce::AudioProcessorEditor,
                        public juce::FileDragAndDropTarget,
                        private juce::Timer
{
public:
    explicit FragatimeEditor (FragatimeProcessor&);
    ~FragatimeEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    void timerCallback() override;
    void openChooser();

    MonoLook look;               // debe declararse primero (se destruye al final)
    FragatimeProcessor& proc;

    juce::TextButton loadBtn { "CARGAR AUDIO" }, clearBtn { "QUITAR" };
    juce::ComboBox fxBox;
    juce::Label fxLabel;
    juce::AudioProcessorValueTreeState::ComboBoxAttachment fxAttach;
    std::vector<std::unique_ptr<Knob>> knobs;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::Rectangle<int> waveArea;
    std::vector<float> cachedPeaks;
    juce::String cachedName;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FragatimeEditor)
};
