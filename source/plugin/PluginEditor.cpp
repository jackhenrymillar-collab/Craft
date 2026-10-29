#include "PluginEditor.h"

namespace foundry
{
using namespace theme;

TopBar::TopBar (FoundryProcessor& p) : proc (p)
{
    kitSelector.addItem (juce::String::fromUTF8 ("M1 · Test Tom"), 1);
    kitSelector.setSelectedId (1, juce::dontSendNotification);
    kitSelector.setTooltip ("Kit presets arrive in M4");
    addAndMakeVisible (kitSelector);

    for (auto* b : { &kitButton, &benchButton })
    {
        b->setRadioGroupId (1001);
        b->setClickingTogglesState (true);
        addAndMakeVisible (b);
    }
    kitButton.setToggleState (true, juce::dontSendNotification);
    startTimerHz (30);
}

void TopBar::timerCallback()
{
    const float peak = proc.takePeak();
    meter = juce::jmax (peak, meter * 0.85f);
    if (proc.snapshotVersion() != seen)
    {
        seen = proc.snapshotVersion();
        snap = proc.getSnapshot();
    }
    repaint (meterArea);
    repaint (statusArea);
}

void TopBar::paint (juce::Graphics& g)
{
    g.setColour (colour::panel);
    g.fillRect (getLocalBounds());
    g.setColour (colour::divider);
    g.fillRect (getLocalBounds().removeFromBottom (1));

    // Logo: an anvil-ish glyph and the wordmark.
    auto r = getLocalBounds().reduced (16, 0);
    auto logo = r.removeFromLeft (150).toFloat();
    juce::Path anvil;
    const float lx = logo.getX(), ly = logo.getCentreY();
    anvil.startNewSubPath (lx, ly - 7);
    anvil.lineTo (lx + 22, ly - 7);
    anvil.quadraticTo (lx + 22, ly - 1, lx + 15, ly);
    anvil.lineTo (lx + 17, ly + 8);
    anvil.lineTo (lx + 5, ly + 8);
    anvil.lineTo (lx + 7, ly);
    anvil.quadraticTo (lx + 1, ly - 2, lx, ly - 7);
    anvil.closeSubPath();
    g.setColour (colour::accent);
    g.fillPath (anvil);
    auto word = mono (16.0f, true);
    word.setExtraKerningFactor (0.2f);
    g.setFont (word);
    g.setColour (colour::text);
    g.drawText ("FOUNDRY", logo.withTrimmedLeft (32.0f), juce::Justification::centredLeft);

    // Transport (sequencer arrives in M4).
    g.setFont (mono (12.0f));
    g.setColour (colour::textMuted);
    g.drawText (juce::String::fromUTF8 ("\xe2\x96\xb6  120 BPM  ·  SWING 0%"), transportArea, juce::Justification::centred);

    // Status chip: modes solved.
    auto chip = statusArea.toFloat().reduced (0.0f, 14.0f);
    g.setColour (colour::control);
    g.fillRoundedRectangle (chip, radiusSmall);
    g.setColour (proc.blowUpCount() > 0 ? colour::accent : colour::ok);
    g.fillEllipse (chip.getX() + 10.0f, chip.getCentreY() - 3.0f, 6.0f, 6.0f);
    g.setFont (mono (11.5f));
    g.setColour (colour::text);
    const auto status = proc.blowUpCount() > 0
                          ? juce::String (juce::String::fromUTF8 ("sim reset · check settings"))
                          : juce::String::fromUTF8 ("modes solved · ") + juce::String (snap.solveMs, 1) + juce::String::fromUTF8 (" ms · ") + juce::String (snap.modes) + " modes";
    g.drawText (status, chip.withTrimmedLeft (24.0f), juce::Justification::centredLeft);

    // Output meter.
    auto m = meterArea.toFloat().reduced (0.0f, 22.0f);
    g.setColour (colour::control);
    g.fillRoundedRectangle (m, 2.0f);
    const float db = juce::Decibels::gainToDecibels (meter, -60.0f);
    const float frac = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
    g.setColour (meter > 0.89f ? colour::accent : colour::ok);
    g.fillRoundedRectangle (m.withWidth (m.getWidth() * frac), 2.0f);
    drawSectionLabel (g, "Out", meterArea.toFloat().withHeight (20.0f).translated (0.0f, 2.0f), colour::textMuted, 9.0f);
}

void TopBar::resized()
{
    auto r = getLocalBounds().reduced (16, 0);
    r.removeFromLeft (150);
    kitSelector.setBounds (r.removeFromLeft (200).reduced (0, 12));
    r.removeFromLeft (24);
    auto sw = r.removeFromLeft (180).reduced (0, 12);
    kitButton.setBounds (sw.removeFromLeft (90));
    benchButton.setBounds (sw);
    meterArea = r.removeFromRight (120);
    r.removeFromRight (16);
    statusArea = r.removeFromRight (290);
    transportArea = r;
}

FoundryEditor::FoundryEditor (FoundryProcessor& p)
    : AudioProcessorEditor (p), proc (p), topBar (p), kit (p), bench (p)
{
    setLookAndFeel (&lnf);
    juce::LookAndFeel::setDefaultLookAndFeel (&lnf);
    addAndMakeVisible (content);
    content.addAndMakeVisible (topBar);
    content.addAndMakeVisible (kit);
    content.addChildComponent (bench);

    topBar.kitButton.onClick = [this] { showBench (false); };
    topBar.benchButton.onClick = [this] { showBench (true); };
    kit.onOpenBench = [this] { showBench (true); };

    setResizable (true, true);
    setResizeLimits (baseWidth / 2, baseHeight / 2, baseWidth * 2, baseHeight * 2);
    getConstrainer()->setFixedAspectRatio ((double) baseWidth / baseHeight);
    setSize (baseWidth, baseHeight);
}

FoundryEditor::~FoundryEditor()
{
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void FoundryEditor::showBench (bool b)
{
    kit.setVisible (! b);
    bench.setVisible (b);
    topBar.kitButton.setToggleState (! b, juce::dontSendNotification);
    topBar.benchButton.setToggleState (b, juce::dontSendNotification);
}

void FoundryEditor::paint (juce::Graphics& g)
{
    g.fillAll (colour::bg);
}

void FoundryEditor::resized()
{
    // Everything is laid out at the base size and scaled as one.
    content.setBounds (0, 0, baseWidth, baseHeight);
    const float scale = (float) getWidth() / (float) baseWidth;
    content.setTransform (juce::AffineTransform::scale (scale));

    auto r = content.getLocalBounds();
    topBar.setBounds (r.removeFromTop (64));
    kit.setBounds (r);
    bench.setBounds (r);
}
} // namespace foundry
