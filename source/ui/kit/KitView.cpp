#include "KitView.h"

namespace foundry
{
using namespace theme;
namespace pid = params::id;

PadTile::PadTile (int n, juce::String nm, juce::String r, bool isLive)
    : number (n), name (std::move (nm)), recipe (std::move (r)), live (isLive)
{
    setTitle ("Pad " + juce::String (n));
    setDescription (live ? "Click to play, double-click to open on the bench" : "Empty pad");
}

void PadTile::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (4.0f);
    g.setColour (live ? colour::control : colour::panel);
    g.fillRoundedRectangle (r, radiusLarge);
    if (flashAlpha > 0.0f)
    {
        g.setColour (colour::accent.withAlpha (0.35f * flashAlpha));
        g.fillRoundedRectangle (r, radiusLarge);
    }
    g.setColour (selected ? colour::accent : colour::outline);
    g.drawRoundedRectangle (r, radiusLarge, selected ? 2.0f : 1.0f);

    auto inner = r.reduced (12.0f, 10.0f);
    drawSectionLabel (g, juce::String (number).paddedLeft ('0', 2), inner.removeFromTop (14.0f),
                      selected ? colour::accent : colour::textMuted);

    // Line glyph of the main object: a drum seen from above.
    auto glyph = inner.removeFromTop (inner.getHeight() * 0.5f);
    const float d = juce::jmin (glyph.getWidth(), glyph.getHeight()) * 0.8f;
    auto circle = juce::Rectangle<float> (d, d).withCentre (glyph.getCentre());
    g.setColour (live ? colour::text.withAlpha (0.85f) : colour::outline);
    g.drawEllipse (circle, 1.5f);
    g.drawEllipse (circle.reduced (d * 0.08f), 1.0f);
    for (int i = 0; i < 6; ++i)
    {
        const auto p = circle.getCentre().getPointOnCircumference (d * 0.5f, juce::MathConstants<float>::twoPi * (float) i / 6.0f);
        g.fillEllipse (p.x - 2.0f, p.y - 2.0f, 4.0f, 4.0f);
    }

    g.setFont (mono (11.0f));
    g.setColour (live ? colour::modeBar : colour::outline);
    g.drawFittedText (recipe, inner.removeFromTop (30.0f).toNearestInt(), juce::Justification::centredLeft, 2);
    g.setFont (sans (15.0f, true));
    g.setColour (live ? colour::text : colour::textMuted);
    g.drawText (name, inner, juce::Justification::bottomLeft, true);
}

void PadTile::mouseDown (const juce::MouseEvent& e)
{
    if (live && onHit)
    {
        // Lower on the tile = harder, so the pad doubles as a velocity tester.
        const float v = juce::jlimit (0.15f, 1.0f, 0.35f + 0.65f * e.position.y / (float) getHeight());
        onHit (v);
        flash();
    }
}

void PadTile::mouseDoubleClick (const juce::MouseEvent&)
{
    if (live && onOpenBench)
        onOpenBench();
}

KitView::KitView (FoundryProcessor& p) : proc (p), head (p)
{
    auto& s = proc.state();
    const char* names[] = { "Tom", "Empty", "Empty", "Empty", "Empty", "Empty", "Empty", "Empty" };
    for (int i = 0; i < 8; ++i)
    {
        auto* t = pads.add (new PadTile (i + 1, names[i], i == 0 ? juce::String::fromUTF8 ("Mylar Membrane × Mallet") : "arrives in M4", i == 0));
        t->selected = i == 0;
        if (i == 0)
        {
            t->onHit = [this] (float v) { proc.queueHit (v); };
            t->onOpenBench = [this] { if (onOpenBench) onOpenBench(); };
        }
        addAndMakeVisible (t);
    }
    addAndMakeVisible (head);

    for (auto m : { MaterialId::Mylar, MaterialId::Hide, MaterialId::Paper })
    {
        auto* b = materialChips.add (new juce::TextButton (juce::String (getMaterial (m).name.data())));
        b->setClickingTogglesState (false);
        b->onClick = [this, m] { proc.setHeadMaterial (m); refreshMaterialChips(); };
        b->setTooltip ("Head material");
        addAndMakeVisible (b);
    }
    refreshMaterialChips();
    openBench.onClick = [this] { if (onOpenBench) onOpenBench(); };
    addAndMakeVisible (openBench);

    auto add = [&] (Group& gr, const char* id, const char* label, const char* tip) {
        addAndMakeVisible (gr.knobs.add (new Knob (s, id, label, tip)));
    };
    treatments[0].title = "Age";
    treatments[0].blurb = "How the material wears, per material";
    add (treatments[0], pid::ageAmount, "Amount", "How old: new to ancient (exaggerated)");
    add (treatments[0], pid::ageClimate, "Climate", "Dry ageing (drying, patina) to damp ageing (rust, rot, slack)");

    treatments[1].title = "Deform";
    treatments[1].blurb = "Dents and bumps in the surface";
    add (treatments[1], pid::deformIntensity, "Intensity", "How deep the dents are");
    add (treatments[1], pid::deformSize, "Size", "Small pings to broad warps");
    add (treatments[1], pid::deformDensity, "Density", "How many dents");

    treatments[2].title = "Twist";
    treatments[2].blurb = "Twisting stress in the material";
    add (treatments[2], pid::twistAmount, "Amount", "Direction and strength of the twist");
    add (treatments[2], pid::twistPivot, "Pivot", "Twist about the centre or a point near the rim");
    add (treatments[2], pid::twistFocus, "Focus", "Spread over the whole part or concentrated at the pivot");
    add (treatments[2], pid::twistWrinkle, "Wrinkle", "Fine wrinkles where the twist is strongest");

    treatments[3].title = "Puncture";
    treatments[3].blurb = "Holes through the material";
    add (treatments[3], pid::punctureCount, "Holes", "Number of holes (0 to 32)");
    add (treatments[3], pid::punctureSize, "Size", "Pin-prick to large");
    add (treatments[3], pid::puncturePlacement, "Place", "Near the centre or near the rim");
    add (treatments[3], pid::punctureScatter, "Scatter", "Neat ring to scattered at random");
    add (treatments[3], pid::punctureRagged, "Ragged", "Clean drilled edge to torn edge");

    const std::pair<const char*, const char*> macroDefs[] = {
        { pid::force, "Force" }, { pid::hardness, "Hardness" }, { pid::spread, "Spread" }, { pid::position, "Position" },
        { pid::tune, "Size/Tune" }, { pid::level, "Level" }, { pid::pan, "Pan" },
    };
    for (const auto& [id, label] : macroDefs)
        addAndMakeVisible (macros.add (new Knob (s, id, label)));

    startTimerHz (30);
}

void KitView::refreshMaterialChips()
{
    const auto current = proc.getHeadMaterial();
    for (auto* b : materialChips)
        b->setToggleState (b->getButtonText() == juce::String (getMaterial (current).name.data()), juce::dontSendNotification);
    pads[0]->setRecipe (juce::String (getMaterial (current).name.data()) + juce::String::fromUTF8 (" Membrane × Mallet"));
}

void KitView::timerCallback()
{
    for (auto* p : pads)
        p->decay();
}

void KitView::paint (juce::Graphics& g)
{
    g.setColour (colour::panel);
    g.fillRoundedRectangle (recipeArea.toFloat(), radiusLarge);
    g.fillRoundedRectangle (treatArea.toFloat(), radiusLarge);
    g.fillRoundedRectangle (macroArea.toFloat(), radiusLarge);

    drawSectionLabel (g, "Pads", padArea.toFloat().removeFromTop (20.0f).translated (6.0f, 0.0f));
    auto rec = recipeArea.toFloat().reduced (14.0f, 10.0f);
    drawSectionLabel (g, juce::String::fromUTF8 ("Recipe · Pad 01"), rec.removeFromTop (18.0f));
    drawSectionLabel (g, "Object", rec.removeFromTop (18.0f).translated (0.0f, 2.0f), colour::textMuted, 10.0f);

    g.setFont (sans (13.0f));
    g.setColour (colour::text);
    {
        auto chip = recipeArea.toFloat().reduced (14.0f, 10.0f).withTrimmedTop (38.0f).removeFromTop (26.0f).removeFromLeft (110.0f);
        g.setColour (colour::accent);
        g.fillRoundedRectangle (chip, radiusSmall);
        g.setColour (colour::bg);
        g.setFont (sans (13.0f, true));
        g.drawText ("Membrane", chip, juce::Justification::centred);
    }

    for (auto& gr : treatments)
    {
        auto a = gr.area.toFloat().reduced (10.0f, 6.0f);
        drawSectionLabel (g, gr.title, a.removeFromTop (16.0f), colour::accent);
        g.setFont (sans (11.5f));
        g.setColour (colour::textMuted);
        g.drawText (gr.blurb, a.removeFromTop (16.0f), juce::Justification::centredLeft, true);
    }
    // Dividers between groups.
    g.setColour (colour::divider);
    for (size_t i = 1; i < treatments.size(); ++i)
        g.fillRect (treatments[i].area.getX(), treatArea.getY() + 10, 1, treatArea.getHeight() - 20);

    drawSectionLabel (g, juce::String::fromUTF8 ("Treatments · Pad 01"), treatArea.toFloat().reduced (14.0f, 8.0f).removeFromTop (16.0f));
    drawSectionLabel (g, juce::String::fromUTF8 ("Macros · Pad 01"), macroArea.toFloat().reduced (14.0f, 8.0f).removeFromTop (16.0f));
}

void KitView::resized()
{
    auto r = getLocalBounds().reduced (12);
    macroArea = r.removeFromBottom (118);
    r.removeFromBottom (10);
    treatArea = r.removeFromBottom (182);
    r.removeFromBottom (10);

    recipeArea = r.removeFromRight (440);
    r.removeFromRight (10);
    padArea = r;

    auto grid = padArea.withTrimmedTop (22);
    const int cw = grid.getWidth() / 4, ch = grid.getHeight() / 2;
    for (int i = 0; i < 8; ++i)
        pads[i]->setBounds (grid.getX() + (i % 4) * cw, grid.getY() + (i / 4) * ch, cw, ch);

    auto rec = recipeArea.reduced (14, 10);
    rec.removeFromTop (38);
    rec.removeFromTop (26); // object chip row (painted)
    rec.removeFromTop (10);
    auto matRow = rec.removeFromTop (34);
    for (auto* b : materialChips)
    {
        b->setBounds (matRow.removeFromLeft (92).reduced (0, 0));
        matRow.removeFromLeft (8);
    }
    auto bottom = rec.removeFromBottom (44);
    openBench.setBounds (bottom.removeFromRight (160));
    rec.removeFromBottom (6);
    head.setBounds (rec);

    auto t = treatArea.reduced (8, 0).withTrimmedTop (24);
    const int weights[] = { 2, 3, 4, 5 };
    const int total = 14;
    const int unit = t.getWidth() / total;
    for (size_t i = 0; i < treatments.size(); ++i)
    {
        auto& gr = treatments[i];
        gr.area = t.removeFromLeft (unit * weights[i]);
        auto k = gr.area.reduced (6, 0).withTrimmedTop (38).withTrimmedBottom (6);
        const int kw = k.getWidth() / gr.knobs.size();
        for (auto* kn : gr.knobs)
            kn->setBounds (k.removeFromLeft (kw).reduced (2, 0));
    }

    auto m = macroArea.reduced (14, 8).withTrimmedTop (18);
    const int mw = juce::jmin (110, m.getWidth() / macros.size());
    for (auto* kn : macros)
        kn->setBounds (m.removeFromLeft (mw).reduced (4, 0));
}
} // namespace foundry
