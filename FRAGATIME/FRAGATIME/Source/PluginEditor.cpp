#include "PluginEditor.h"

// ------------------------------------------------------------ look and feel

MonoLook::MonoLook()
{
    using namespace Palette;
    setColour (juce::Slider::textBoxTextColourId, fg);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, dim);
    setColour (juce::ComboBox::backgroundColourId, panel);
    setColour (juce::ComboBox::textColourId, fg);
    setColour (juce::ComboBox::outlineColourId, track);
    setColour (juce::ComboBox::arrowColourId, fg);
    setColour (juce::PopupMenu::backgroundColourId, panel);
    setColour (juce::PopupMenu::textColourId, fg);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, fg);
    setColour (juce::PopupMenu::highlightedTextColourId, bg);
    setColour (juce::TextButton::buttonColourId, panel);
    setColour (juce::TextButton::textColourOffId, fg);
    setColour (juce::TextButton::textColourOnId, fg);
}

void MonoLook::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                                 float pos, float startAngle, float endAngle, juce::Slider&)
{
    using namespace Palette;
    auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (6.f);
    const float radius = juce::jmin (b.getWidth(), b.getHeight()) * 0.5f;
    const auto c = b.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);

    juce::Path trackPath;
    trackPath.addCentredArc (c.x, c.y, radius, radius, 0.f, startAngle, endAngle, true);
    g.setColour (track);
    g.strokePath (trackPath, juce::PathStrokeType (2.f));

    juce::Path valuePath;
    valuePath.addCentredArc (c.x, c.y, radius, radius, 0.f, startAngle, angle, true);
    g.setColour (fg);
    g.strokePath (valuePath, juce::PathStrokeType (3.f));

    const float kr = radius * 0.74f;
    g.setColour (panel.brighter (0.05f));
    g.fillEllipse (c.x - kr, c.y - kr, kr * 2.f, kr * 2.f);
    g.setColour (track);
    g.drawEllipse (c.x - kr, c.y - kr, kr * 2.f, kr * 2.f, 1.f);

    juce::Path pointer;
    pointer.addRectangle (-1.5f, -kr, 3.f, kr * 0.45f);
    g.setColour (fg);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (c.x, c.y));
}

// ------------------------------------------------------------------- knob

Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& id, const juce::String& text)
    : attachment (state, id, slider)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f,
                                juce::MathConstants<float>::pi * 2.8f, true);
    addAndMakeVisible (slider);

    label.setText (text, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (juce::Font (juce::FontOptions (12.f)));
    addAndMakeVisible (label);
}

void Knob::resized()
{
    auto r = getLocalBounds();
    label.setBounds (r.removeFromTop (18));
    slider.setBounds (r);
}

// ----------------------------------------------------------------- editor

FragatimeEditor::FragatimeEditor (FragatimeProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      fxAttach (p.apvts, "fx", fxBox)
{
    setLookAndFeel (&look);
    setSize (760, 660);

    const std::pair<const char*, const char*> defs[] = {
        { "intensity", "INTENSIDAD" }, { "density", "DENSITY" },
        { "size", "SIZE" },            { "pitch", "PITCH" },
        { "arp", "ARPEGIADOR" },       { "grain", "GRANULADO" },
        { "feedback", "FEEDBACK" },    { "position", "POSICION" },
        { "fxamount", "FX AMOUNT" }
    };
    for (auto& d : defs)
    {
        knobs.push_back (std::make_unique<Knob> (proc.apvts, d.first, d.second));
        addAndMakeVisible (*knobs.back());
    }

    fxBox.addItemList ({ "Ninguno", "Reverb", "Delay", "Bitcrusher", "Chorus" }, 1);
    addAndMakeVisible (fxBox);

    fxLabel.setText ("EFECTO BASE", juce::dontSendNotification);
    fxLabel.setJustificationType (juce::Justification::centred);
    fxLabel.setFont (juce::Font (juce::FontOptions (12.f)));
    addAndMakeVisible (fxLabel);

    loadBtn.onClick  = [this] { openChooser(); };
    clearBtn.onClick = [this] { proc.clearAudioFile(); };
    addAndMakeVisible (loadBtn);
    addAndMakeVisible (clearBtn);

    startTimerHz (30);
}

FragatimeEditor::~FragatimeEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void FragatimeEditor::openChooser()
{
    chooser = std::make_unique<juce::FileChooser> ("Selecciona un audio", juce::File(),
                                                   "*.wav;*.mp3;*.flac;*.aiff;*.aif;*.ogg");
    chooser->launchAsync (juce::FileBrowserComponent::openMode
                          | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (f.existsAsFile()) proc.loadAudioFile (f);
                              repaint();
                          });
}

bool FragatimeEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
    {
        const auto ext = juce::File (f).getFileExtension().toLowerCase();
        if (ext == ".wav" || ext == ".mp3" || ext == ".flac" || ext == ".aiff"
            || ext == ".aif" || ext == ".ogg")
            return true;
    }
    return false;
}

void FragatimeEditor::filesDropped (const juce::StringArray& files, int, int)
{
    for (auto& f : files)
        if (proc.loadAudioFile (juce::File (f))) break;
    repaint();
}

void FragatimeEditor::timerCallback()
{
    const auto name = proc.getLoadedFileName();
    if (name != cachedName)
    {
        cachedName = name;
        cachedPeaks = proc.getPeaks();
    }
    repaint (waveArea);
}

void FragatimeEditor::resized()
{
    auto r = getLocalBounds().reduced (30, 0);

    auto header = r.removeFromTop (60);
    clearBtn.setBounds (header.removeFromRight (80).reduced (0, 16));
    header.removeFromRight (8);
    loadBtn.setBounds (header.removeFromRight (130).reduced (0, 16));

    waveArea = r.removeFromTop (130).withTrimmedTop (10);

    r.removeFromTop (14);
    const int cw = r.getWidth() / 4;
    const int rh = 150;

    for (int i = 0; i < 8; ++i)
        knobs[(size_t) i]->setBounds (r.getX() + (i % 4) * cw, r.getY() + (i / 4) * rh, cw, rh);

    auto bottom = juce::Rectangle<int> (r.getX(), r.getY() + 2 * rh, r.getWidth(), 120);
    auto fxArea = bottom.removeFromLeft (cw * 2).reduced (30, 20);
    fxLabel.setBounds (fxArea.removeFromTop (18));
    fxArea.removeFromTop (10);
    fxBox.setBounds (fxArea.removeFromTop (32));
    knobs[8]->setBounds (bottom.removeFromLeft (cw).withHeight (rh).withY (bottom.getY() - 10));
}

void FragatimeEditor::paint (juce::Graphics& g)
{
    using namespace Palette;
    g.fillAll (bg);

    g.setColour (fg);
    g.setFont (juce::Font (juce::FontOptions (26.f, juce::Font::bold)));
    g.drawText ("F R A G A T I M E", 30, 8, 360, 44, juce::Justification::centredLeft);

    // onda
    g.setColour (panel);
    g.fillRoundedRectangle (waveArea.toFloat(), 6.f);

    if (! cachedPeaks.empty())
    {
        const int w = waveArea.getWidth();
        const float cy = waveArea.getCentreY();
        const float hh = waveArea.getHeight() * 0.45f;
        g.setColour (fg.withAlpha (0.85f));
        for (int x = 0; x < w; ++x)
        {
            const size_t idx = juce::jmin (cachedPeaks.size() - 1,
                                           (size_t) x * cachedPeaks.size() / (size_t) w);
            const float h = juce::jmax (1.f, cachedPeaks[idx] * hh);
            g.fillRect ((float) (waveArea.getX() + x), cy - h, 1.f, h * 2.f);
        }
        const float px = waveArea.getX() + proc.getPlayhead() * (float) w;
        g.setColour (juce::Colours::white);
        g.drawVerticalLine ((int) px, (float) waveArea.getY(), (float) waveArea.getBottom());

        g.setColour (dim);
        g.setFont (12.f);
        g.drawText (cachedName, waveArea.reduced (8, 4), juce::Justification::topLeft);
    }
    else
    {
        g.setColour (dim);
        g.setFont (14.f);
        g.drawText ("Arrastra un .wav / .mp3 aqui   -   o usa la entrada en vivo del canal",
                    waveArea, juce::Justification::centred);
    }
}
