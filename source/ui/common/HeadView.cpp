#include "HeadView.h"

#include <cmath>

namespace foundry
{
using namespace theme;

HeadView::HeadView (FoundryProcessor& p) : proc (p)
{
    setTitle ("Drum head");
    setDescription ("Top-down view of the drum head. Click to tap-test it at that point.");
    startTimerHz (30);
}

void HeadView::timerCallback()
{
    bool dirty = false;
    if (proc.snapshotVersion() != seen)
    {
        seen = proc.snapshotVersion();
        snap = proc.getSnapshot();
        dirty = true;
    }
    if (rippleAge < 1.0f)
    {
        rippleAge = juce::jmin (1.0f, rippleAge + 1.0f / 18.0f);
        dirty = true;
    }
    if (dirty)
        repaint();
}

juce::Rectangle<float> HeadView::headBounds() const
{
    auto r = getLocalBounds().toFloat().reduced (16.0f);
    const float d = juce::jmin (r.getWidth(), r.getHeight());
    return juce::Rectangle<float> (d, d).withCentre (r.getCentre());
}

void HeadView::mouseDown (const juce::MouseEvent& e)
{
    const auto hb = headBounds().reduced (headBounds().getWidth() * 0.06f);
    const auto c = hb.getCentre();
    const float R = hb.getWidth() * 0.5f;
    const auto d = e.position - c;
    const float r = d.getDistanceFromOrigin() / R;
    if (r > 1.0f)
        return;
    // Screen y grows downwards; the model's y grows upwards.
    proc.queueHit (0.8f, juce::jmin (r, 0.96f), std::atan2 (-d.y, d.x));
    lastTap = e.position;
    rippleAge = 0.0f;
}

void HeadView::paint (juce::Graphics& g)
{
    if (drawMat)
        drawCuttingMat (g, getLocalBounds().toFloat());

    const auto outer = headBounds();
    const auto head = outer.reduced (outer.getWidth() * 0.06f);
    const auto c = head.getCentre();
    const float R = head.getWidth() * 0.5f;
    const float metresToPx = R / (float) juce::jmax (1e-6, snap.radius);
    auto toScreen = [&] (Vec2 p) { return juce::Point<float> (c.x + (float) p.x * metresToPx, c.y - (float) p.y * metresToPx); };

    // Drop shadow onto the mat.
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillEllipse (outer.translated (4.0f, 6.0f));

    // Hoop (chrome-ish rim) and lugs.
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff8d8f88), outer.getX(), outer.getY(),
                                             juce::Colour (0xff3b3c37), outer.getRight(), outer.getBottom(), false));
    g.fillEllipse (outer);
    const int lugs = 8;
    for (int i = 0; i < lugs; ++i)
    {
        const float a = juce::MathConstants<float>::twoPi * (float) i / lugs + 0.2f;
        const auto p = c.getPointOnCircumference (outer.getWidth() * 0.5f - 2.0f, a);
        juce::Path lug;
        lug.addRoundedRectangle (-6.0f, -11.0f, 12.0f, 22.0f, 3.0f);
        lug.applyTransform (juce::AffineTransform::rotation (a).translated (p));
        g.setColour (juce::Colour (0xffb4b5ae));
        g.fillPath (lug);
        g.setColour (juce::Colour (0xff2a2b27));
        g.strokePath (lug, juce::PathStrokeType (1.0f));
    }

    // The skin.
    juce::Colour skin = snap.material == MaterialId::Hide ? juce::Colour (0xffd9c39a)
                      : snap.material == MaterialId::Paper ? juce::Colour (0xffe8dcc0)
                                                           : juce::Colour (0xffe9e6dc);
    g.setGradientFill (juce::ColourGradient (skin.brighter (0.06f), c.x - R * 0.3f, c.y - R * 0.4f,
                                             skin.darker (0.18f), c.x + R, c.y + R, true));
    g.fillEllipse (head);

    juce::Graphics::ScopedSaveState clip (g);
    juce::Path skinPath;
    skinPath.addEllipse (head);
    g.reduceClipRegion (skinPath);

    // Twist: spiral strain lines around the pivot.
    for (const auto& f : snap.features)
        if (f.kind == TreatmentFeature::Kind::TwistPivot)
        {
            const auto p = toScreen (f.p);
            const float r0 = (float) f.radius * metresToPx;
            const float amt = (float) f.strength;
            g.setColour (juce::Colours::black.withAlpha (0.10f + 0.12f * std::abs (amt)));
            for (int arm = 0; arm < 10; ++arm)
            {
                juce::Path spiral;
                const float base = juce::MathConstants<float>::twoPi * (float) arm / 10.0f;
                for (int s = 0; s <= 40; ++s)
                {
                    const float u = (float) s / 40.0f;
                    const float rr = u * R * 2.0f;
                    const float ang = base + amt * 1.6f * (r0 * r0) / (rr * rr + r0 * r0) * 3.0f + amt * u;
                    const auto q = p + juce::Point<float> (std::cos (ang) * rr, std::sin (ang) * rr);
                    if (s == 0) spiral.startNewSubPath (q); else spiral.lineTo (q);
                }
                g.strokePath (spiral, juce::PathStrokeType (1.0f));
            }
        }

    for (const auto& f : snap.features)
    {
        const auto p = toScreen (f.p);
        const float r = juce::jmax (1.5f, (float) f.radius * metresToPx);
        switch (f.kind)
        {
            case TreatmentFeature::Kind::Dent:
            {
                const float s = (float) f.strength;
                juce::ColourGradient grad (juce::Colours::black.withAlpha (0.28f * std::abs (s)), p.x + r * 0.2f, p.y + r * 0.2f,
                                           juce::Colours::transparentBlack, p.x + r, p.y + r, true);
                if (s < 0) grad = juce::ColourGradient (juce::Colours::white.withAlpha (0.35f * std::abs (s)), p.x - r * 0.2f, p.y - r * 0.2f,
                                                        juce::Colours::transparentWhite, p.x + r, p.y + r, true);
                g.setGradientFill (grad);
                g.fillEllipse (p.x - r, p.y - r, 2 * r, 2 * r);
                break;
            }
            case TreatmentFeature::Kind::Pit:
                g.setColour (juce::Colour (0xff8a4a22).withAlpha (0.25f + 0.5f * (float) f.strength));
                g.fillEllipse (p.x - r, p.y - r, 2 * r, 2 * r);
                break;
            case TreatmentFeature::Kind::Crack:
            {
                g.setColour (juce::Colour (0xff3a2a1a).withAlpha (0.7f));
                const float len = (float) f.radius * metresToPx;
                g.fillEllipse (p.x - 1.2f, p.y - 1.2f, 2.4f, 2.4f);
                juce::ignoreUnused (len);
                break;
            }
            case TreatmentFeature::Kind::Hole:
            {
                // See the mat (or background) through the hole, with a torn lip.
                g.setColour (drawMat ? colour::mat.darker (0.3f) : colour::bg);
                g.fillEllipse (p.x - r, p.y - r, 2 * r, 2 * r);
                g.setColour (juce::Colours::black.withAlpha (0.25f + 0.4f * (float) f.strength));
                g.drawEllipse (p.x - r, p.y - r, 2 * r, 2 * r, 1.0f + 2.0f * (float) f.strength);
                break;
            }
            case TreatmentFeature::Kind::TwistPivot:
                break;
        }
    }

    // Cracks as connected lines.
    {
        juce::Path cracks;
        bool open = false;
        juce::Point<float> prev;
        for (const auto& f : snap.features)
        {
            if (f.kind != TreatmentFeature::Kind::Crack) { open = false; continue; }
            const auto p = toScreen (f.p);
            if (! open || p.getDistanceFrom (prev) > (float) f.radius * metresToPx * 1.6f + 2.0f)
                cracks.startNewSubPath (p);
            else
                cracks.lineTo (p);
            prev = p;
            open = true;
        }
        g.setColour (juce::Colour (0xff3a2a1a).withAlpha (0.75f));
        g.strokePath (cracks, juce::PathStrokeType (1.4f));
    }

    // Contact mic: a small puck stuck to the head.
    {
        const auto m = toScreen (MembraneBasis::polarToLocal (snap.contactMic.r, snap.contactMic.theta, snap.radius));
        g.setColour (colour::control);
        g.fillRoundedRectangle (m.x - 7, m.y - 5, 14, 10, 2.0f);
        g.setColour (colour::ok);
        g.fillEllipse (m.x - 2, m.y - 2, 4, 4);
    }

    // Readout tag.
    {
        g.restoreState();
        g.saveState();
        const auto txt = "f0 " + juce::String (snap.fundamentalHz, 1) + juce::String::fromUTF8 (" Hz  \xc2\xb7  ") + juce::String (snap.exactModes) + " + "
                         + juce::String (snap.modes - snap.exactModes) + " modes";
        auto f = mono (11.0f);
        const float w = juce::GlyphArrangement::getStringWidth (f, txt) + 16.0f;
        auto tag = juce::Rectangle<float> (w, 20.0f).withPosition (8.0f, (float) getHeight() - 28.0f);
        g.setColour (colour::bg.withAlpha (0.85f));
        g.fillRoundedRectangle (tag, radiusSmall);
        g.setFont (f);
        g.setColour (colour::text);
        g.drawText (txt, tag, juce::Justification::centred);
    }

    // Last tap: accent dot with ripple rings.
    if (rippleAge < 1.0f)
    {
        g.setColour (colour::accent);
        g.fillEllipse (lastTap.x - 4, lastTap.y - 4, 8, 8);
        for (int i = 0; i < 3; ++i)
        {
            const float a = rippleAge + (float) i * 0.12f;
            if (a > 1.0f) continue;
            const float rr = 6.0f + a * R * 0.5f;
            g.setColour (colour::accent.withAlpha (0.6f * (1.0f - a)));
            g.drawEllipse (lastTap.x - rr, lastTap.y - rr, rr * 2, rr * 2, 1.5f);
        }
    }
}
} // namespace foundry
