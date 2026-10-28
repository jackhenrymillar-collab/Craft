#include "Theme.h"

#include "BinaryData.h"

namespace foundry::theme
{
namespace
{
juce::Typeface::Ptr load (const char* data, int size)
{
    return juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
}

struct Faces
{
    juce::Typeface::Ptr sansRegular = load (FoundryFonts::IBMPlexSansRegular_ttf, FoundryFonts::IBMPlexSansRegular_ttfSize);
    juce::Typeface::Ptr sansSemi = load (FoundryFonts::IBMPlexSansSemiBold_ttf, FoundryFonts::IBMPlexSansSemiBold_ttfSize);
    juce::Typeface::Ptr monoRegular = load (FoundryFonts::IBMPlexMonoRegular_ttf, FoundryFonts::IBMPlexMonoRegular_ttfSize);
    juce::Typeface::Ptr monoMedium = load (FoundryFonts::IBMPlexMonoMedium_ttf, FoundryFonts::IBMPlexMonoMedium_ttfSize);
};

const Faces& faces()
{
    static const Faces f;
    return f;
}
} // namespace

juce::Font sans (float height, bool semibold)
{
    return juce::Font (juce::FontOptions (semibold ? faces().sansSemi : faces().sansRegular).withHeight (height));
}

juce::Font mono (float height, bool medium)
{
    return juce::Font (juce::FontOptions (medium ? faces().monoMedium : faces().monoRegular).withHeight (height));
}

void drawSectionLabel (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                       juce::Colour c, float height, juce::Justification just)
{
    auto f = mono (height, true);
    f.setExtraKerningFactor (0.12f);
    g.setFont (f);
    g.setColour (c);
    g.drawText (text.toUpperCase(), area, just, false);
}

void drawCuttingMat (juce::Graphics& g, juce::Rectangle<float> area, juce::Point<float> origin)
{
    g.setColour (colour::mat);
    g.fillRect (area);
    auto lines = [&] (float step, float alpha) {
        g.setColour (juce::Colours::white.withAlpha (alpha));
        const float x0 = area.getX() + std::fmod (origin.x, step);
        for (float x = x0; x < area.getRight(); x += step)
            g.fillRect (x, area.getY(), 1.0f, area.getHeight());
        const float y0 = area.getY() + std::fmod (origin.y, step);
        for (float y = y0; y < area.getBottom(); y += step)
            g.fillRect (area.getX(), y, area.getWidth(), 1.0f);
    };
    lines (20.0f, 0.04f);
    lines (100.0f, 0.10f);
}
} // namespace foundry::theme
