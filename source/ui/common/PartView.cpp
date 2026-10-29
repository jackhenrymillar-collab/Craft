#include "PartView.h"

#include <cmath>

namespace foundry
{
using namespace theme;

namespace
{
constexpr float kTwoPi = juce::MathConstants<float>::twoPi;

void drawScrew (juce::Graphics& g, juce::Point<float> p, float r)
{
    g.setColour (juce::Colour (0xffb4b5ae));
    g.fillEllipse (p.x - r, p.y - r, 2 * r, 2 * r);
    g.setColour (juce::Colour (0xff2a2b27));
    g.drawEllipse (p.x - r, p.y - r, 2 * r, 2 * r, 1.0f);
    g.drawLine (p.x - r * 0.6f, p.y, p.x + r * 0.6f, p.y, 1.0f);
}
} // namespace

PartView::PartView (FoundryProcessor& p) : proc (p)
{
    setTitle ("Part");
    setDescription ("Top-down view of the pad's object. Click anywhere on it to tap-test it there.");
    startTimerHz (30);
}

void PartView::timerCallback()
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

PartView::Mapping PartView::mapping() const
{
    auto r = getLocalBounds().toFloat().reduced (18.0f).withTrimmedBottom (22.0f);
    Mapping m;
    m.centre = r.getCentre();
    const double hx = juce::jmax (1e-4, snap.halfExtents.x), hy = juce::jmax (1e-4, snap.round ? snap.halfExtents.x : snap.halfExtents.y);
    // Leave room for hoops, cords and mounts around the part.
    const float margin = snap.part == PartType::Membrane ? 1.12f : 1.08f;
    const float s = (float) juce::jmin ((double) r.getWidth() / (2.0 * hx * margin), (double) r.getHeight() / (2.0 * hy * margin));
    m.sx = s;
    m.sy = s;
    // Very thin parts (springs, slim bars) are drawn a little fatter so they can be tapped.
    const float minThickness = 16.0f;
    if (! snap.round && 2.0f * (float) hy * s < minThickness)
        m.sy = minThickness / (2.0f * (float) hy);
    return m;
}

juce::Path PartView::bodyPath (const Mapping& m) const
{
    juce::Path p;
    const auto c = m.centre;
    const float hx = (float) snap.halfExtents.x * m.sx;
    const float hy = (float) (snap.round ? snap.halfExtents.x * m.sx : snap.halfExtents.y * m.sy);
    if (snap.round)
        p.addEllipse (c.x - hx, c.y - hx, 2 * hx, 2 * hx);
    else if (snap.part == PartType::Tube || snap.part == PartType::Spring)
        p.addRoundedRectangle (c.x - hx, c.y - hy, 2 * hx, 2 * hy, hy);
    else
        p.addRoundedRectangle (c.x - hx, c.y - hy, 2 * hx, 2 * hy, snap.part == PartType::Bar ? 3.0f : 2.0f);
    return p;
}

void PartView::drawUnder (juce::Graphics& g, const Mapping& m, const juce::Path& body) const
{
    const auto c = m.centre;
    const auto b = body.getBounds();
    // Drop shadow onto the mat.
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillPath (body, juce::AffineTransform::translation (4.0f, 6.0f));

    switch (snap.part)
    {
        case PartType::Membrane:
        {
            // Hoop (chrome-ish rim) and lugs.
            const auto outer = b.expanded (b.getWidth() * 0.06f);
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillEllipse (outer.translated (4.0f, 6.0f));
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff8d8f88), outer.getX(), outer.getY(),
                                                     juce::Colour (0xff3b3c37), outer.getRight(), outer.getBottom(), false));
            g.fillEllipse (outer);
            for (int i = 0; i < 8; ++i)
            {
                const float a = kTwoPi * (float) i / 8.0f + 0.2f;
                const auto p = c.getPointOnCircumference (outer.getWidth() * 0.5f - 2.0f, a);
                juce::Path lug;
                lug.addRoundedRectangle (-6.0f, -11.0f, 12.0f, 22.0f, 3.0f);
                lug.applyTransform (juce::AffineTransform::rotation (a).translated (p));
                g.setColour (juce::Colour (0xffb4b5ae));
                g.fillPath (lug);
                g.setColour (juce::Colour (0xff2a2b27));
                g.strokePath (lug, juce::PathStrokeType (1.0f));
            }
            break;
        }
        case PartType::Shell:
            if (snap.mount == 0)
            {
                // Ring cushion under the base.
                const auto cushion = juce::Rectangle<float> (b.getWidth() * 0.62f, b.getWidth() * 0.62f).withCentre (c);
                g.setColour (juce::Colour (0xff6b2f3a));
                g.fillEllipse (cushion);
                g.setColour (juce::Colour (0xff8a4652));
                g.drawEllipse (cushion.reduced (6.0f), 2.0f);
            }
            break;
        case PartType::Plate:
            if (snap.mount == 1)
            {
                // Frame.
                g.setColour (juce::Colour (0xff4a4b45));
                g.fillRoundedRectangle (b.expanded (9.0f), 3.0f);
                g.setColour (juce::Colour (0xff2a2b27));
                g.drawRoundedRectangle (b.expanded (9.0f), 3.0f, 1.0f);
            }
            break;
        case PartType::Bar: case PartType::Tube: case PartType::Spring: case PartType::Box: case PartType::Count:
            break;
    }
}

void PartView::drawSurface (juce::Graphics& g, const Mapping& m, const juce::Path& body) const
{
    const auto b = body.getBounds();
    const auto c = m.centre;
    const juce::Colour base = materialColour (snap.material);

    switch (snap.part)
    {
        case PartType::Membrane:
            g.setGradientFill (juce::ColourGradient (base.brighter (0.06f), c.x - b.getWidth() * 0.15f, c.y - b.getHeight() * 0.2f,
                                                     base.darker (0.18f), b.getRight(), b.getBottom(), true));
            g.fillPath (body);
            break;
        case PartType::Shell:
        {
            // Concentric shading: the base (centre) sits lower and darker, the rim catches light.
            const float profile = (float) snap.shape[1];
            g.setGradientFill (juce::ColourGradient (base.darker (0.55f - 0.25f * profile), c.x, c.y,
                                                     base.brighter (0.25f), c.x + b.getWidth() * 0.5f, c.y, true));
            g.fillPath (body);
            g.setColour (juce::Colours::white.withAlpha (0.08f));
            for (float f : { 0.3f, 0.55f, 0.8f })
                g.drawEllipse (juce::Rectangle<float> (b.getWidth() * f, b.getHeight() * f).withCentre (c), 1.0f);
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            juce::Path rim;
            rim.addCentredArc (c.x, c.y, b.getWidth() * 0.47f, b.getHeight() * 0.47f, 0.0f, -2.3f, -0.4f, true);
            g.strokePath (rim, juce::PathStrokeType (2.0f));
            break;
        }
        case PartType::Tube:
        {
            // Cylinder: light along the top edge, dark underneath.
            g.setGradientFill (juce::ColourGradient (base.brighter (0.35f), c.x, b.getY(), base.darker (0.5f), c.x, b.getBottom(), false));
            g.fillPath (body);
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.fillRect (b.getX() + b.getHeight() * 0.5f, b.getY() + b.getHeight() * 0.22f, b.getWidth() - b.getHeight(), 1.5f);
            break;
        }
        case PartType::Spring:
            // The body is only the tap area; the coil is drawn over it.
            g.setColour (juce::Colours::black.withAlpha (0.12f));
            g.fillPath (body);
            break;
        case PartType::Box:
        {
            g.setGradientFill (juce::ColourGradient (base.brighter (0.1f), b.getX(), b.getY(), base.darker (0.25f), b.getRight(), b.getBottom(), false));
            g.fillPath (body);
            // Plywood grain.
            g.setColour (base.darker (0.35f).withAlpha (0.35f));
            for (int i = 0; i < 14; ++i)
            {
                juce::Path grain;
                const float y = b.getY() + b.getHeight() * ((float) i + 0.5f) / 14.0f;
                grain.startNewSubPath (b.getX(), y);
                for (float x = b.getX(); x <= b.getRight(); x += 12.0f)
                    grain.lineTo (x, y + 2.5f * std::sin (x * 0.03f + (float) i * 1.7f));
                g.strokePath (grain, juce::PathStrokeType (0.8f));
            }
            break;
        }
        case PartType::Bar:
        case PartType::Plate:
        case PartType::Count:
        {
            const bool wood = snap.material == MaterialId::Oak || snap.material == MaterialId::Spruce || snap.material == MaterialId::Bamboo;
            g.setGradientFill (juce::ColourGradient (base.brighter (0.18f), b.getX(), b.getY(), base.darker (0.3f), b.getRight(), b.getBottom(), false));
            g.fillPath (body);
            if (wood)
            {
                g.setColour (base.darker (0.4f).withAlpha (0.3f));
                for (int i = 0; i < 9; ++i)
                {
                    const float y = b.getY() + b.getHeight() * ((float) i + 0.5f) / 9.0f;
                    g.drawLine (b.getX() + 2.0f, y, b.getRight() - 2.0f, y + 1.5f * std::sin ((float) i), 0.8f);
                }
            }
            else
            {
                // Brushed metal streaks.
                g.setColour (juce::Colours::white.withAlpha (0.07f));
                for (int i = 0; i < 24; ++i)
                {
                    const float y = b.getY() + b.getHeight() * ((float) i + 0.5f) / 24.0f;
                    g.drawLine (b.getX() + 3.0f, y, b.getRight() - 3.0f, y, 0.6f);
                }
            }
            if (snap.part == PartType::Bar && snap.shape[2] > 0.01)
            {
                // Undercut arch: thinner (darker, seen through) in the middle 70 %.
                const float w = b.getWidth() * 0.7f;
                juce::ColourGradient arch (juce::Colours::transparentBlack, c.x - w * 0.5f, c.y, juce::Colours::black.withAlpha (0.35f * (float) snap.shape[2]), c.x, c.y, false);
                arch.addColour (1.0, juce::Colours::transparentBlack);
                arch.point2 = { c.x + w * 0.5f, c.y };
                arch.clearColours();
                arch.addColour (0.0, juce::Colours::transparentBlack);
                arch.addColour (0.5, juce::Colours::black.withAlpha (0.35f * (float) snap.shape[2]));
                arch.addColour (1.0, juce::Colours::transparentBlack);
                g.setGradientFill (arch);
                g.fillRect (juce::Rectangle<float> (w, b.getHeight()).withCentre (c));
            }
            break;
        }
    }
}

void PartView::drawOver (juce::Graphics& g, const Mapping& m, const juce::Path& body) const
{
    const auto b = body.getBounds();
    const auto c = m.centre;
    const juce::Colour base = materialColour (snap.material);
    const juce::Colour cord (0xff8a7452);

    switch (snap.part)
    {
        case PartType::Bar:
            if (snap.mount == 0)
            {
                // Cords at the fundamental's nodes (0.224 L from each end).
                for (float f : { 0.224f, 0.776f })
                {
                    const float x = b.getX() + b.getWidth() * f;
                    g.setColour (cord);
                    g.drawLine (x, b.getY() - 14.0f, x, b.getBottom() + 14.0f, 2.0f);
                }
            }
            else
            {
                auto clamp = juce::Rectangle<float> (26.0f, b.getHeight() + 22.0f).withCentre ({ b.getX() + 8.0f, c.y });
                g.setColour (juce::Colour (0xff4a4b45));
                g.fillRoundedRectangle (clamp, 3.0f);
                drawScrew (g, clamp.getCentre(), 5.0f);
            }
            break;
        case PartType::Plate:
            if (snap.mount == 0)
            {
                // Hung from two holes near the top edge.
                for (float f : { 0.12f, 0.88f })
                {
                    const juce::Point<float> h (b.getX() + b.getWidth() * f, b.getY() + 10.0f);
                    g.setColour (cord);
                    g.drawLine (h.x, h.y, h.x + (f < 0.5f ? 14.0f : -14.0f), b.getY() - 22.0f, 1.5f);
                    g.setColour (colour::bg);
                    g.fillEllipse (h.x - 3.5f, h.y - 3.5f, 7.0f, 7.0f);
                }
            }
            else
                for (auto p : { b.getTopLeft(), b.getTopRight(), b.getBottomLeft(), b.getBottomRight() })
                    drawScrew (g, p + juce::Point<float> (p.x < c.x ? 0.0f : 0.0f, 0.0f), 4.0f);
            break;
        case PartType::Shell:
            if (snap.mount == 1)
            {
                // Crown loop for hanging.
                g.setColour (base.darker (0.5f));
                g.drawEllipse (juce::Rectangle<float> (16.0f, 10.0f).withCentre (c), 3.0f);
                g.setColour (cord);
                g.drawLine (c.x, c.y, c.x, b.getY() - 18.0f, 2.0f);
            }
            break;
        case PartType::Tube:
        {
            const float r = b.getHeight() * 0.5f;
            auto endEllipse = [&] (float x, bool open) {
                auto e = juce::Rectangle<float> (r * 0.7f, b.getHeight() * 0.92f).withCentre ({ x, c.y });
                g.setColour (open ? juce::Colour (0xff141513) : base.darker (0.15f));
                g.fillEllipse (e);
                g.setColour (base.brighter (0.3f));
                g.drawEllipse (e, 1.2f);
            };
            endEllipse (b.getX() + r * 0.4f, snap.mount == 0);
            endEllipse (b.getRight() - r * 0.4f, true);
            g.setColour (cord);
            g.drawLine (b.getX() + r * 0.4f, c.y - r, b.getX() + r * 0.4f, b.getY() - 22.0f, 1.5f);
            break;
        }
        case PartType::Spring:
        {
            const float len = b.getWidth(), h = b.getHeight();
            const int turns = juce::jlimit (8, 80, (int) (len / juce::jmax (4.0f, h * 0.45f)));
            const float pitch = len / (float) turns;
            juce::Path coil;
            for (int i = 0; i <= turns * 12; ++i)
            {
                const float t = (float) i / 12.0f;
                const float x = b.getX() + t * pitch;
                const float y = c.y + 0.5f * h * std::sin (kTwoPi * t);
                if (i == 0) coil.startNewSubPath (x, y); else coil.lineTo (x, y);
            }
            g.setColour (base.darker (0.5f));
            g.strokePath (coil, juce::PathStrokeType (2.6f));
            g.setColour (base.brighter (0.2f));
            g.strokePath (coil, juce::PathStrokeType (1.2f));
            // Hooks at the ends (one end only when it hangs).
            g.setColour (base);
            g.drawEllipse (b.getX() - 10.0f, c.y - 5.0f, 10.0f, 10.0f, 2.0f);
            if (snap.mount == 0)
                g.drawEllipse (b.getRight(), c.y - 5.0f, 10.0f, 10.0f, 2.0f);
            break;
        }
        case PartType::Box:
        {
            for (auto p : { b.getTopLeft(), b.getTopRight(), b.getBottomLeft(), b.getBottomRight() })
                drawScrew (g, p + juce::Point<float> (p.x < c.x ? 9.0f : -9.0f, p.y < c.y ? 9.0f : -9.0f), 3.5f);
            if (snap.shape[2] > 0.01)
            {
                // The port is in the back: dashed outline where it is.
                const float r = (float) (snap.shape[2] * 0.06) * m.sx;
                juce::Path port;
                port.addEllipse (juce::Rectangle<float> (2 * r, 2 * r).withCentre ({ c.x, c.y + b.getHeight() * 0.18f }));
                juce::Path dashed;
                const float dash[] = { 4.0f, 4.0f };
                juce::PathStrokeType (1.2f).createDashedStroke (dashed, port, dash, 2);
                g.setColour (juce::Colours::black.withAlpha (0.45f));
                g.fillPath (dashed);
                g.setFont (mono (9.0f));
                g.drawText ("PORT (BACK)", juce::Rectangle<float> (80.0f, 12.0f).withCentre ({ c.x, c.y + b.getHeight() * 0.18f + r + 9.0f }),
                            juce::Justification::centred);
            }
            break;
        }
        case PartType::Membrane:
        case PartType::Count:
            break;
    }
    juce::ignoreUnused (body);
}

void PartView::drawFeatures (juce::Graphics& g, const Mapping& m) const
{
    const float s = m.sx;
    // Twist: spiral strain lines around the pivot.
    for (const auto& f : snap.features)
        if (f.kind == TreatmentFeature::Kind::TwistPivot)
        {
            const auto p = m.toScreen (f.p);
            const float r0 = (float) f.radius * s;
            const float amt = (float) f.strength;
            const float R = (float) juce::jmax (snap.halfExtents.x, snap.halfExtents.y) * s;
            g.setColour (juce::Colours::black.withAlpha (0.10f + 0.12f * std::abs (amt)));
            for (int arm = 0; arm < 10; ++arm)
            {
                juce::Path spiral;
                const float base = kTwoPi * (float) arm / 10.0f;
                for (int k = 0; k <= 40; ++k)
                {
                    const float u = (float) k / 40.0f;
                    const float rr = u * R * 2.0f;
                    const float ang = base + amt * 1.6f * (r0 * r0) / (rr * rr + r0 * r0) * 3.0f + amt * u;
                    const auto q = p + juce::Point<float> (std::cos (ang) * rr, std::sin (ang) * rr);
                    if (k == 0) spiral.startNewSubPath (q); else spiral.lineTo (q);
                }
                g.strokePath (spiral, juce::PathStrokeType (1.0f));
            }
        }

    for (const auto& f : snap.features)
    {
        const auto p = m.toScreen (f.p);
        const float r = juce::jmax (1.5f, (float) f.radius * s);
        switch (f.kind)
        {
            case TreatmentFeature::Kind::Dent:
            {
                const float st = (float) f.strength;
                juce::ColourGradient grad (juce::Colours::black.withAlpha (0.28f * std::abs (st)), p.x + r * 0.2f, p.y + r * 0.2f,
                                           juce::Colours::transparentBlack, p.x + r, p.y + r, true);
                if (st < 0) grad = juce::ColourGradient (juce::Colours::white.withAlpha (0.35f * std::abs (st)), p.x - r * 0.2f, p.y - r * 0.2f,
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
                g.setColour (juce::Colour (0xff3a2a1a).withAlpha (0.7f));
                g.fillEllipse (p.x - 1.2f, p.y - 1.2f, 2.4f, 2.4f);
                break;
            case TreatmentFeature::Kind::Hole:
                // See the mat (or background) through the hole, with a torn lip.
                g.setColour (drawMat ? colour::mat.darker (0.3f) : colour::bg);
                g.fillEllipse (p.x - r, p.y - r, 2 * r, 2 * r);
                g.setColour (juce::Colours::black.withAlpha (0.25f + 0.4f * (float) f.strength));
                g.drawEllipse (p.x - r, p.y - r, 2 * r, 2 * r, 1.0f + 2.0f * (float) f.strength);
                break;
            case TreatmentFeature::Kind::TwistPivot:
                break;
        }
    }

    // Cracks as connected lines.
    juce::Path cracks;
    bool open = false;
    juce::Point<float> prev;
    for (const auto& f : snap.features)
    {
        if (f.kind != TreatmentFeature::Kind::Crack) { open = false; continue; }
        const auto p = m.toScreen (f.p);
        if (! open || p.getDistanceFrom (prev) > (float) f.radius * s * 1.6f + 2.0f)
            cracks.startNewSubPath (p);
        else
            cracks.lineTo (p);
        prev = p;
        open = true;
    }
    g.setColour (juce::Colour (0xff3a2a1a).withAlpha (0.75f));
    g.strokePath (cracks, juce::PathStrokeType (1.4f));
}

void PartView::mouseDown (const juce::MouseEvent& e)
{
    const auto m = mapping();
    Vec2 p = m.toLocal (e.position);
    const double hx = snap.halfExtents.x, hy = snap.round ? snap.halfExtents.x : snap.halfExtents.y;
    // Forgive taps a few pixels off the edge (thin parts): pull them just inside.
    const double tolX = 10.0 / m.sx, tolY = 10.0 / m.sy;
    if (snap.round)
    {
        const double r = std::hypot (p.x, p.y);
        if (r > hx + tolX) return;
        if (r > 0.97 * hx) { p.x *= 0.97 * hx / r; p.y *= 0.97 * hx / r; }
    }
    else
    {
        if (std::abs (p.x) > hx + tolX || std::abs (p.y) > hy + tolY) return;
        p.x = juce::jlimit (-0.98 * hx, 0.98 * hx, p.x);
        p.y = juce::jlimit (-0.98 * hy, 0.98 * hy, p.y);
    }
    proc.queueTap (0.8f, p);
    lastTap = e.position;
    rippleAge = 0.0f;
}

void PartView::paint (juce::Graphics& g)
{
    if (drawMat)
        drawCuttingMat (g, getLocalBounds().toFloat());

    const auto m = mapping();
    const auto body = bodyPath (m);
    drawUnder (g, m, body);
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (body);
        drawSurface (g, m, body);
        drawFeatures (g, m);
    }
    drawOver (g, m, body);

    // Contact mic: a small puck stuck to the part.
    {
        const auto p = m.toScreen (snap.contactMic);
        g.setColour (colour::control);
        g.fillRoundedRectangle (p.x - 7, p.y - 5, 14, 10, 2.0f);
        g.setColour (colour::ok);
        g.fillEllipse (p.x - 2, p.y - 2, 4, 4);
    }

    // Readout tag.
    {
        const auto txt = "f0 " + juce::String (snap.fundamentalHz, 1) + juce::String::fromUTF8 (" Hz  \xc2\xb7  ") + juce::String (snap.exactModes)
                         + (snap.modes > snap.exactModes ? " + " + juce::String (snap.modes - snap.exactModes) : juce::String()) + " modes";
        auto f = mono (11.0f);
        const float w = juce::GlyphArrangement::getStringWidth (f, txt) + 16.0f;
        auto tag = juce::Rectangle<float> (w, 20.0f).withPosition (8.0f, (float) getHeight() - 26.0f);
        g.setColour (colour::bg.withAlpha (0.85f));
        g.fillRoundedRectangle (tag, radiusSmall);
        g.setFont (f);
        g.setColour (colour::text);
        g.drawText (txt, tag, juce::Justification::centred);
    }

    // Last tap: accent dot with ripple rings.
    if (rippleAge < 1.0f)
    {
        const float R = body.getBounds().getWidth() * 0.5f;
        g.setColour (colour::accent);
        g.fillEllipse (lastTap.x - 4, lastTap.y - 4, 8, 8);
        for (int i = 0; i < 3; ++i)
        {
            const float a = rippleAge + (float) i * 0.12f;
            if (a > 1.0f) continue;
            const float rr = 6.0f + a * juce::jmin (R, 120.0f) * 0.5f;
            g.setColour (colour::accent.withAlpha (0.6f * (1.0f - a)));
            g.drawEllipse (lastTap.x - rr, lastTap.y - rr, rr * 2, rr * 2, 1.5f);
        }
    }
}

void PartView::drawGlyph (juce::Graphics& g, PartType type, juce::Rectangle<float> area, juce::Colour c)
{
    const auto ctr = area.getCentre();
    const float d = juce::jmin (area.getWidth(), area.getHeight()) * 0.8f;
    g.setColour (c);
    switch (type)
    {
        case PartType::Membrane:
        {
            auto circle = juce::Rectangle<float> (d, d).withCentre (ctr);
            g.drawEllipse (circle, 1.5f);
            g.drawEllipse (circle.reduced (d * 0.08f), 1.0f);
            for (int i = 0; i < 6; ++i)
            {
                const auto p = ctr.getPointOnCircumference (d * 0.5f, kTwoPi * (float) i / 6.0f);
                g.fillEllipse (p.x - 2.0f, p.y - 2.0f, 4.0f, 4.0f);
            }
            break;
        }
        case PartType::Bar:
        {
            auto bar = juce::Rectangle<float> (d * 1.4f, d * 0.22f).withCentre (ctr);
            g.drawRoundedRectangle (bar, 2.0f, 1.5f);
            for (float f : { 0.224f, 0.776f })
                g.drawLine (bar.getX() + bar.getWidth() * f, bar.getY() - 5.0f, bar.getX() + bar.getWidth() * f, bar.getBottom() + 5.0f, 1.0f);
            break;
        }
        case PartType::Plate:
        {
            auto plate = juce::Rectangle<float> (d * 1.1f, d * 0.78f).withCentre (ctr);
            g.drawRect (plate, 1.5f);
            g.drawLine (plate.getX() + 6.0f, plate.getY(), plate.getX() + 12.0f, plate.getY() - 7.0f, 1.0f);
            g.drawLine (plate.getRight() - 6.0f, plate.getY(), plate.getRight() - 12.0f, plate.getY() - 7.0f, 1.0f);
            break;
        }
        case PartType::Shell:
        {
            auto bowl = juce::Rectangle<float> (d, d).withCentre (ctr);
            g.drawEllipse (bowl, 1.5f);
            g.drawEllipse (bowl.reduced (d * 0.3f), 1.0f);
            break;
        }
        case PartType::Tube:
        {
            auto tube = juce::Rectangle<float> (d * 1.4f, d * 0.26f).withCentre (ctr);
            g.drawRoundedRectangle (tube, tube.getHeight() * 0.5f, 1.5f);
            g.drawEllipse (juce::Rectangle<float> (tube.getHeight() * 0.5f, tube.getHeight() * 0.8f).withCentre ({ tube.getRight() - tube.getHeight() * 0.3f, ctr.y }), 1.0f);
            break;
        }
        case PartType::Spring:
        {
            juce::Path coil;
            const float w = d * 1.4f, h = d * 0.28f;
            for (int i = 0; i <= 8 * 12; ++i)
            {
                const float t = (float) i / 12.0f;
                const float x = ctr.x - w * 0.5f + t * w / 8.0f;
                const float y = ctr.y + 0.5f * h * std::sin (kTwoPi * t);
                if (i == 0) coil.startNewSubPath (x, y); else coil.lineTo (x, y);
            }
            g.strokePath (coil, juce::PathStrokeType (1.3f));
            break;
        }
        case PartType::Box:
        {
            auto box = juce::Rectangle<float> (d * 0.7f, d * 1.0f).withCentre (ctr);
            g.drawRect (box, 1.5f);
            g.drawEllipse (juce::Rectangle<float> (d * 0.22f, d * 0.22f).withCentre ({ ctr.x, ctr.y + d * 0.15f }), 1.0f);
            break;
        }
        case PartType::Count: break;
    }
}
} // namespace foundry
