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

    // Line glyph of the main object.
    auto glyphArea = inner.removeFromTop (inner.getHeight() * 0.5f).reduced (4.0f);
    if (live)
        PartView::drawGlyph (g, glyph, glyphArea, colour::text.withAlpha (0.85f));
    else
    {
        const float d = juce::jmin (glyphArea.getWidth(), glyphArea.getHeight()) * 0.7f;
        g.setColour (colour::outline);
        g.drawEllipse (juce::Rectangle<float> (d, d).withCentre (glyphArea.getCentre()), 1.0f);
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

MaterialSwatch::MaterialSwatch (MaterialId m)
    : juce::Button (juce::String (getMaterial (m).name.data())), material (m)
{
    setTooltip (juce::String (getMaterial (m).name.data()));
    setTitle (getTooltip());
}

void MaterialSwatch::paintButton (juce::Graphics& g, bool highlighted, bool)
{
    auto r = getLocalBounds().toFloat().reduced (2.5f);
    const auto c = materialColour (material);
    g.setGradientFill (juce::ColourGradient (c.brighter (0.25f), r.getTopLeft(), c.darker (0.35f), r.getBottomRight(), false));
    g.fillRoundedRectangle (r, radiusSmall);
    if (getToggleState())
    {
        g.setColour (colour::accent);
        g.drawRoundedRectangle (r.expanded (1.5f), radiusSmall + 1.0f, 2.0f);
    }
    else
    {
        g.setColour (highlighted ? colour::text.withAlpha (0.6f) : colour::bg.withAlpha (0.6f));
        g.drawRoundedRectangle (r, radiusSmall, 1.0f);
    }
}

namespace
{
// Short chip labels; the recipe text uses the full part names.
const char* chipLabel (PartType t)
{
    switch (t)
    {
        case PartType::Membrane: return "Head";
        case PartType::Bar:      return "Bar";
        case PartType::Plate:    return "Plate";
        case PartType::Shell:    return "Shell";
        case PartType::Tube:     return "Tube";
        case PartType::Spring:   return "Spring";
        case PartType::Box:      return "Box";
        case PartType::Count:    break;
    }
    return "";
}

const char* partTip (PartType t)
{
    switch (t)
    {
        case PartType::Membrane: return "Drum head: a tensioned skin on a hoop";
        case PartType::Bar:      return "Bar: a marimba / glockenspiel-style bar";
        case PartType::Plate:    return "Plate: a thin sheet, hung or framed";
        case PartType::Shell:    return "Shell: a singing bowl that morphs into a bell";
        case PartType::Tube:     return "Tube: a chime with an air column inside";
        case PartType::Spring:   return "Spring: a coiled spring with a chirpy, dispersive ring";
        case PartType::Box:      return "Box: a wooden box (cajon-like) with a port";
        case PartType::Count:    break;
    }
    return "";
}

juce::String str (std::string_view v) { return juce::String (v.data(), v.size()); }
} // namespace

KitView::KitView (FoundryProcessor& p) : proc (p), part (p)
{
    auto& s = proc.state();
    const char* names[] = { "Pad 1", "Empty", "Empty", "Empty", "Empty", "Empty", "Empty", "Empty" };
    for (int i = 0; i < 8; ++i)
    {
        auto* t = pads.add (new PadTile (i + 1, names[i], i == 0 ? juce::String() : "arrives in M4", i == 0));
        t->selected = i == 0;
        if (i == 0)
        {
            t->onHit = [this] (float v) { proc.queueHit (v); };
            t->onOpenBench = [this] { if (onOpenBench) onOpenBench(); };
        }
        addAndMakeVisible (t);
    }
    addAndMakeVisible (part);

    for (int t = 0; t < kNumPartTypes; ++t)
    {
        auto* b = objectChips.add (new juce::TextButton (chipLabel ((PartType) t)));
        b->setTooltip (partTip ((PartType) t));
        b->onClick = [this, t] { proc.setPartType ((PartType) t); refreshRecipe(); };
        addAndMakeVisible (b);
    }
    for (int m = 0; m < (int) kNumMaterials; ++m)
    {
        auto* b = swatches.add (new MaterialSwatch ((MaterialId) m));
        b->onClick = [this, m] { proc.setMaterial ((MaterialId) m); refreshRecipe(); };
        addChildComponent (b);
    }
    for (int t = 0; t < kNumStrikerTypes; ++t)
    {
        const auto& info = strikerInfo ((StrikerType) t);
        auto* b = strikerChips.add (new juce::TextButton (str (info.name)));
        b->setTooltip ("Hardness goes from " + str (info.soft).toLowerCase() + " to " + str (info.hard).toLowerCase());
        b->onClick = [this, t] { proc.setStriker ((StrikerType) t); refreshRecipe(); };
        addAndMakeVisible (b);
    }
    for (int m = 0; m < 2; ++m)
    {
        auto* b = mountChips.add (new juce::TextButton());
        b->setTooltip ("How the part is held");
        b->onClick = [this, m] { proc.setMount (m); refreshRecipe(); };
        addChildComponent (b);
    }
    for (int i = 0; i < 3; ++i)
    {
        auto* k = shapeKnobs.add (new Knob (s, params::id::shape[i], "Shape"));
        k->valueText = [this, i] (float u) {
            const auto& c = partInfo (proc.getPartType()).controls[(size_t) i];
            const double v = c.value (u) * c.display;
            const int decimals = std::abs (v) < 10.0 && c.unit != "%" ? 1 : 0;
            return juce::String (v, decimals) + (c.unit.empty() ? "" : " " + str (c.unit));
        };
        addAndMakeVisible (k);
    }
    // Add-ons.
    attachBox.addItem ("Nothing joined", 1);
    for (int t = 0; t < kNumPartTypes; ++t)
        attachBox.addItem (str (partInfo ((PartType) t).name), t + 2);
    attachBox.setTooltip ("A second part bolted or rubber-mounted to this one (a head on a box, a bar on a plate)");
    attachBox.setTitle ("Joined part");
    attachBox.onChange = [this] {
        const int id = attachBox.getSelectedId();
        proc.setAttachPart (id <= 1 ? std::nullopt : std::optional<PartType> ((PartType) (id - 2)));
        refreshRecipe();
    };
    addAndMakeVisible (attachBox);
    attachMaterialBox.setTooltip ("The joined part's material");
    attachMaterialBox.setTitle ("Joined part material");
    attachMaterialBox.onChange = [this] {
        if (const int id = attachMaterialBox.getSelectedId(); id > 0)
            proc.setAttachMaterial ((MaterialId) (id - 1));
        refreshRecipe();
    };
    addAndMakeVisible (attachMaterialBox);
    for (int t = 0; t < kNumJointTypes; ++t)
    {
        auto* b = jointChips.add (new juce::TextButton (str (jointInfo ((JointType) t).name)));
        b->setTooltip (str (jointInfo ((JointType) t).description));
        b->onClick = [this, t] { proc.setJoint ((JointType) t); refreshRecipe(); };
        addAndMakeVisible (b);
    }
    for (int t = 0; t < kNumRattleTypes; ++t)
    {
        auto* b = rattleChips.add (new juce::TextButton (str (rattleInfo ((RattleType) t).name)));
        b->setTooltip (str (rattleInfo ((RattleType) t).description));
        b->onClick = [this, t] { proc.setRattleType ((RattleType) t); refreshRecipe(); };
        addAndMakeVisible (b);
    }
    for (int t = 0; t < kNumLooseTypes; ++t)
    {
        auto* b = looseChips.add (new juce::TextButton (str (looseInfo ((LooseType) t).name)));
        b->setTooltip (str (looseInfo ((LooseType) t).description));
        b->onClick = [this, t] { proc.setLooseType ((LooseType) t); refreshRecipe(); };
        addAndMakeVisible (b);
    }
    oversampleButton.setTooltip ("Run this pad at twice the sample rate: cleaner buzz and rattles, about twice the CPU");
    oversampleButton.setClickingTogglesState (false);
    oversampleButton.onClick = [this] { proc.setOversample (! proc.getOversample()); refreshRecipe(); };
    addAndMakeVisible (oversampleButton);
    joinKnobs.add (new Knob (s, pid::attachSize, "Size", "The joined part's size"));
    joinKnobs.add (new Knob (s, pid::jointStiffness, "Stiffness", "How firmly the joint holds"));
    joinKnobs.add (new Knob (s, pid::attachPlace, "Place", "Where the joint sits on this part: centre to edge"));
    joinKnobs[0]->valueText = [this] (float u) {
        const auto t = proc.getAttachPart().value_or (PartType::Box);
        const auto& c = partInfo (t).controls[0];
        const double v = c.value (u) * c.display;
        return juce::String (v, std::abs (v) < 10.0 && c.unit != "%" ? 1 : 0) + (c.unit.empty() ? "" : " " + str (c.unit));
    };
    rattleKnobs.add (new Knob (s, pid::rattle, "Rattle", "Snare wires / rivets: pressed tight to hanging loose"));
    rattleKnobs.add (new Knob (s, pid::looseAmount, "Amount", "How much rice, how many beads"));
    for (auto* k : joinKnobs) addAndMakeVisible (k);
    for (auto* k : rattleKnobs) addAndMakeVisible (k);

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
    add (treatments[2], pid::twistPivot, "Pivot", "Twist about the centre or a point near the edge");
    add (treatments[2], pid::twistFocus, "Focus", "Spread over the whole part or concentrated at the pivot");
    add (treatments[2], pid::twistWrinkle, "Wrinkle", "Fine wrinkles where the twist is strongest");

    treatments[3].title = "Puncture";
    treatments[3].blurb = "Holes through the material";
    add (treatments[3], pid::punctureCount, "Holes", "Number of holes (0 to 32)");
    add (treatments[3], pid::punctureSize, "Size", "Pin-prick to large");
    add (treatments[3], pid::puncturePlacement, "Place", "Near the centre or near the edge");
    add (treatments[3], pid::punctureScatter, "Scatter", "Neat ring to scattered at random");
    add (treatments[3], pid::punctureRagged, "Ragged", "Clean drilled edge to torn edge");

    const std::tuple<const char*, const char*, const char*> macroDefs[] = {
        { pid::force, "Force", "How hard the pad is hit (velocity scales it)" },
        { pid::hardness, "Hardness", "Striker hardness: soft to hard (depends on the striker)" },
        { pid::spread, "Spread", "Random scatter of the strike point" },
        { pid::position, "Position", "Strike point: centre to edge" },
        { pid::tune, "Size/Tune", "Scales the whole object" },
        { pid::airMix, "Air mix", "Contact mic (on the part) to air mic (in the room)" },
        { pid::micDistance, "Mic dist", "How far the air mic is from the part" },
        { pid::level, "Level", "" },
        { pid::pan, "Pan", "" },
    };
    for (const auto& [id, label, tip] : macroDefs)
        addAndMakeVisible (macros.add (new Knob (s, id, label, tip)));

    refreshRecipe();
    startTimerHz (30);
}

KitView::Shown KitView::current() const
{
    return { proc.getPartType(), proc.getMaterial(), proc.getStriker(), proc.getMount(), proc.getAttachPart(),
             proc.getAttachMaterial(), proc.getJoint(), proc.getRattleType(), proc.getLooseType(), proc.getOversample() };
}

void KitView::refreshRecipe()
{
    const Shown now = current();
    const bool partChanged = ! shown || shown->part != now.part;
    shown = now;
    const auto& info = partInfo (now.part);

    for (int t = 0; t < objectChips.size(); ++t)
        objectChips[t]->setToggleState (t == (int) now.part, juce::dontSendNotification);
    for (auto* sw : swatches)
    {
        sw->setVisible (materialAllowed (now.part, sw->material));
        sw->setToggleState (sw->material == now.material, juce::dontSendNotification);
    }
    for (int t = 0; t < strikerChips.size(); ++t)
        strikerChips[t]->setToggleState (t == (int) now.striker, juce::dontSendNotification);
    for (int m = 0; m < mountChips.size(); ++m)
    {
        auto* b = mountChips[m];
        b->setVisible (m < info.mountCount);
        b->setButtonText (str (info.mounts[(size_t) m]));
        b->setToggleState (m == now.mount, juce::dontSendNotification);
    }
    if (partChanged)
        for (int i = 0; i < shapeKnobs.size(); ++i)
        {
            const auto& c = info.controls[(size_t) i];
            shapeKnobs[i]->setLabel (str (c.name), str (info.name) + " " + str (c.name).toLowerCase());
        }

    // Add-ons.
    attachBox.setSelectedId (now.attach ? (int) *now.attach + 2 : 1, juce::dontSendNotification);
    attachMaterialBox.clear (juce::dontSendNotification);
    if (now.attach)
        for (int m = 0; m < (int) kNumMaterials; ++m)
            if (materialAllowed (*now.attach, (MaterialId) m))
                attachMaterialBox.addItem (str (getMaterial ((MaterialId) m).name), m + 1);
    attachMaterialBox.setSelectedId ((int) now.attachMaterial + 1, juce::dontSendNotification);
    attachMaterialBox.setEnabled (now.attach.has_value());
    for (int t = 0; t < jointChips.size(); ++t)
    {
        jointChips[t]->setToggleState (t == (int) now.joint, juce::dontSendNotification);
        jointChips[t]->setEnabled (now.attach.has_value());
    }
    auto enable = [] (juce::Component& c, bool on) { c.setEnabled (on); c.setAlpha (on ? 1.0f : 0.4f); };
    for (auto* k : joinKnobs)
        enable (*k, now.attach.has_value());
    for (auto* b : jointChips)
        enable (*b, now.attach.has_value());
    enable (attachMaterialBox, now.attach.has_value());
    for (int t = 0; t < rattleChips.size(); ++t)
        rattleChips[t]->setToggleState (t == (int) now.rattle, juce::dontSendNotification);
    for (int t = 0; t < looseChips.size(); ++t)
        looseChips[t]->setToggleState (t == (int) now.loose, juce::dontSendNotification);
    enable (*rattleKnobs[0], now.rattle != RattleType::None);
    enable (*rattleKnobs[1], now.loose != LooseType::None);
    oversampleButton.setToggleState (now.oversample, juce::dontSendNotification);

    // Recipe text, e.g. "Mylar Membrane + Box (bolt) + Snare + Rice x Mallet".
    juce::String recipe = str (getMaterial (now.material).name) + " " + str (info.name);
    if (now.attach)
        recipe << " + " << str (partInfo (*now.attach).name);
    if (now.rattle != RattleType::None)
        recipe << " + " << str (rattleInfo (now.rattle).name);
    if (now.loose != LooseType::None)
        recipe << " + " << str (looseInfo (now.loose).name);
    recipe << juce::String::fromUTF8 (" × ") << str (strikerInfo (now.striker).name);
    pads[0]->setRecipe (recipe, now.part);
    resized(); // the swatch row changes with the part
    repaint();
}

void KitView::timerCallback()
{
    for (auto* p : pads)
        p->decay();
    if (shown && ! (*shown == current()))
        refreshRecipe();
}

void KitView::paint (juce::Graphics& g)
{
    g.setColour (colour::panel);
    g.fillRoundedRectangle (recipeArea.toFloat(), radiusLarge);
    g.fillRoundedRectangle (addOnArea.toFloat(), radiusLarge);
    g.fillRoundedRectangle (treatArea.toFloat(), radiusLarge);
    g.fillRoundedRectangle (macroArea.toFloat(), radiusLarge);

    drawSectionLabel (g, juce::String::fromUTF8 ("Add-ons · Pad 01"), addOnArea.toFloat().reduced (14.0f, 10.0f).removeFromTop (26.0f));
    drawSectionLabel (g, "Joined to", joinTitle.toFloat(), colour::accent, 10.0f);
    drawSectionLabel (g, "Rattles", rattleTitle.toFloat(), colour::accent, 10.0f);
    drawSectionLabel (g, "Loose on top", looseTitle.toFloat(), colour::accent, 10.0f);

    drawSectionLabel (g, "Pads", padArea.toFloat().removeFromTop (20.0f).translated (6.0f, 0.0f));
    auto rec = recipeArea.toFloat().reduced (14.0f, 10.0f);
    drawSectionLabel (g, juce::String::fromUTF8 ("Recipe · Pad 01"), rec.removeFromTop (26.0f));

    auto rowLabel = [&] (const juce::Rectangle<int>& row, const char* text, juce::String detail) {
        auto a = row.toFloat().withWidth (72.0f);
        if (detail.isEmpty())
            drawSectionLabel (g, text, a, colour::textMuted, 10.0f);
        else
        {
            drawSectionLabel (g, text, a.removeFromTop (a.getHeight() * 0.5f), colour::textMuted, 10.0f);
            g.setFont (mono (10.5f));
            g.setColour (colour::text);
            g.drawText (detail, a, juce::Justification::centredLeft, true);
        }
    };
    if (shown)
    {
        rowLabel (objectRow, "Object", {});
        rowLabel (materialRow, "Material", str (getMaterial (shown->material).name));
        rowLabel (strikerRow, "Striker", {});
        rowLabel (mountRow, "Mount", {});
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

    recipeArea = r.removeFromRight (460);
    r.removeFromRight (10);
    addOnArea = r.removeFromRight (290);
    r.removeFromRight (10);
    padArea = r;

    {
        auto a = addOnArea.reduced (14, 10);
        oversampleButton.setBounds (a.removeFromTop (26).removeFromRight (44));
        a.removeFromTop (4);
        auto chipRow = [] (juce::Rectangle<int> row, auto& buttons) {
            const int w = row.getWidth() / juce::jmax (1, buttons.size());
            for (auto* b : buttons)
                b->setBounds (row.removeFromLeft (w).reduced (2, 0));
        };
        joinTitle = a.removeFromTop (16);
        auto row = a.removeFromTop (28);
        attachBox.setBounds (row.removeFromLeft (row.getWidth() / 2).reduced (2, 0));
        attachMaterialBox.setBounds (row.reduced (2, 0));
        a.removeFromTop (4);
        chipRow (a.removeFromTop (26), jointChips);
        a.removeFromTop (2);
        auto kr = a.removeFromTop (juce::jmin (78, a.getHeight() / 3));
        const int kw = kr.getWidth() / 3;
        for (auto* k : joinKnobs)
            k->setBounds (kr.removeFromLeft (kw).reduced (2, 0));
        a.removeFromTop (6);
        rattleTitle = a.removeFromTop (16);
        chipRow (a.removeFromTop (26), rattleChips);
        a.removeFromTop (6);
        looseTitle = a.removeFromTop (16);
        chipRow (a.removeFromTop (26), looseChips);
        a.removeFromTop (2);
        auto k2 = a.removeFromTop (juce::jmin (78, a.getHeight()));
        const int kw2 = k2.getWidth() / 3;
        rattleKnobs[0]->setBounds (k2.removeFromLeft (kw2).reduced (2, 0));
        rattleKnobs[1]->setBounds (k2.removeFromLeft (kw2).reduced (2, 0));
    }

    auto grid = padArea.withTrimmedTop (22);
    const int cw = grid.getWidth() / 4, ch = grid.getHeight() / 2;
    for (int i = 0; i < 8; ++i)
        pads[i]->setBounds (grid.getX() + (i % 4) * cw, grid.getY() + (i / 4) * ch, cw, ch);

    auto rec = recipeArea.reduced (14, 10);
    openBench.setBounds (rec.removeFromTop (26).removeFromRight (130));
    rec.removeFromTop (6);

    auto layRow = [] (juce::Rectangle<int> row, auto& buttons, int maxWidth) {
        row.removeFromLeft (76);
        int n = 0;
        for (auto* b : buttons)
            n += b->isVisible() ? 1 : 0;
        if (n == 0)
            return;
        const int w = juce::jmin (maxWidth, row.getWidth() / n);
        for (auto* b : buttons)
            if (b->isVisible())
                b->setBounds (row.removeFromLeft (w).reduced (2, 0));
    };
    objectRow = rec.removeFromTop (30);
    layRow (objectRow, objectChips, 80);
    rec.removeFromTop (6);
    materialRow = rec.removeFromTop (28);
    layRow (materialRow, swatches, 30);
    rec.removeFromTop (6);
    strikerRow = rec.removeFromTop (30);
    layRow (strikerRow, strikerChips, 92);
    rec.removeFromTop (6);
    mountRow = rec.removeFromTop (30);
    layRow (mountRow, mountChips, 110);
    rec.removeFromTop (8);

    auto knobs = rec.removeFromRight (84);
    const int kh = knobs.getHeight() / juce::jmax (1, shapeKnobs.size());
    for (auto* k : shapeKnobs)
        k->setBounds (knobs.removeFromTop (kh).reduced (2, 2));
    rec.removeFromRight (6);
    part.setBounds (rec);

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
