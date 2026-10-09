#include "CharacterSelector.h"
#include "OceanLookAndFeel.h"

#include <array>

namespace amanita::ui
{
namespace
{
constexpr auto parameterId = "algorithm";

// The one table of the texts shown for the Characters, in the order of the
// choices of the host parameter.
constexpr std::array<CharacterDescription::Text, CharacterSelector::characterCount>
    characterTexts {{
        { "DEFAULT",
          "8-LINE FEEDBACK DELAY NETWORK",
          "Open water on a windless day. The sound settles into a clear, even space and "
          "fades without leaving a colour of its own. Evolution stirs the surface, slowly, "
          "from below." },
        { "BLOOM",
          "RISING TAPS, DOUBLE DIFFUSION",
          "The note goes under, and a moment later the water answers. A slow swell rises "
          "behind every sound and opens like something blooming in the dark. Evolution "
          "lets it rise longer and fuller." },
        { "DRIFT",
          "SPECTRAL FEEDBACK KERNELS",
          "A tail that never stays where it began. It leaves the surface as light and air "
          "and sinks, turn by turn, into warmth and weight. Evolution sends it further on "
          "its way." },
        { "VEIL",
          "ALL-PASS TRANSIENT DISPERSER",
          "Every attack dissolves before it lands, like a shape seen through moving water. "
          "Drums and plucked strings arrive as soft clouds, their edges gone. Evolution "
          "draws the veil closer." },
        { "CURRENT",
          "COHERENT FLOW FIELD",
          "One slow current carries the whole space with it. Colour, depth and direction "
          "turn together, the way a body of water turns: wide, liquid, never still. "
          "Evolution sets how hard it pulls." },
        { "FATHOM",
          "MODELLED 16-LINE TIDAL NETWORK",
          "Deep water with a tide of its own. The space opens and closes in long, slow "
          "breaths, dense and smooth as the dark below the light. Evolution brings the "
          "tide in; at zero the depth stands still." },
        { "UNDERTOW",
          "REVERSED VOICES IN TEMPO",
          "What you just played is pulled back under and returned in reverse: at pitch, an "
          "octave above, an octave below, in step with the tempo of the song. Evolution "
          "lets the three voices in, one by one." }
    }};

// Design measures of the description block. Its parts are set apart by the
// clear space between their ink: from a baseline down to the capitals or to
// the hairline under it.
namespace block
{
constexpr float nameFontHeight = 20.0f;
constexpr float nameTracking = 0.055f;
constexpr float nameToSubtitle = 15.0f;
constexpr float subtitleFontHeight = 8.5f;
constexpr float subtitleTracking = 0.095f;
constexpr float subtitleToRule = 18.0f;
constexpr float ruleThickness = 1.0f;
constexpr float ruleToParagraph = 16.0f;
constexpr float paragraphFontHeight = 10.5f;
constexpr float paragraphTracking = 0.012f;
constexpr float paragraphLineHeight = 14.0f;
// The lines of a paragraph may be narrowed by this share of their width to
// give a widow company.
constexpr float widowAllowance = 0.15f;
} // namespace block

[[nodiscard]] juce::Font blockFont(float height, bool bold, float tracking)
{
    return juce::Font { juce::FontOptions(juce::Font::getDefaultSansSerifFontName(),
                                          height,
                                          bold ? juce::Font::bold : juce::Font::plain)
                            .withKerningFactor(tracking) };
}

[[nodiscard]] juce::Font nameFont()
{
    return blockFont(block::nameFontHeight, true, block::nameTracking);
}

[[nodiscard]] juce::Font subtitleFont()
{
    return blockFont(block::subtitleFontHeight, true, block::subtitleTracking);
}

[[nodiscard]] juce::Font paragraphFont()
{
    return blockFont(block::paragraphFontHeight, false, block::paragraphTracking);
}

// How far the capitals of a font rise above its baseline.
[[nodiscard]] float capitalHeight(const juce::Font& font)
{
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(font, "H", 0.0f, 0.0f);
    juce::Path outline;
    glyphs.createPath(outline);
    return -outline.getBounds().getY();
}

// Draws one line on its baseline with its ink beginning at the left edge of
// the block, where the hairline and the paragraph begin.
void drawLineOfText(juce::Graphics& graphics,
                    const juce::Font& font,
                    const char* text,
                    float baseline,
                    juce::Colour colour)
{
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(font, text, 0.0f, baseline);
    juce::Path outline;
    glyphs.createPath(outline);
    glyphs.moveRangeOfGlyphs(0, -1, -outline.getBounds().getX(), 0.0f);
    graphics.setColour(colour);
    glyphs.draw(graphics);
}
} // namespace

CharacterSelector::CharacterSelector(juce::AudioProcessorValueTreeState& state)
{
    setComponentID("character-selector");
    setAccessible(true);
    setTitle("Reverb character");
    setDescription("Selects the reverb character. The arrow keys change it; "
                   "Return or Space opens the list.");

    auto* parameter = dynamic_cast<juce::AudioParameterChoice*>(
        state.getParameter(parameterId));
    jassert(parameter != nullptr && parameter->choices.size() == characterCount);
    if (parameter != nullptr)
        addItemList(parameter->choices, 1);

    attachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        state, parameterId, *this);
}

bool CharacterSelector::keyPressed(const juce::KeyPress& key)
{
    // Only the bare keys are the drop-down's: a key pressed with a modifier
    // compares unequal to its code and is passed on.
    const auto step = key == juce::KeyPress::upKey || key == juce::KeyPress::leftKey ? -1
                    : key == juce::KeyPress::downKey || key == juce::KeyPress::rightKey ? 1
                    : 0;
    if (step != 0)
    {
        // An open list hands Left and Right on to its drop-down. The keys are
        // the list's then, and the Character stays until an item is chosen.
        if (! isPopupActive())
            moveSelection(step);
        return true;
    }

    // The list opens on Return in the base class; Space opens it as well.
    if (key == juce::KeyPress::spaceKey)
        return juce::ComboBox::keyPressed(juce::KeyPress(juce::KeyPress::returnKey));

    return juce::ComboBox::keyPressed(key);
}

void CharacterSelector::moveSelection(int offset)
{
    const auto count = getNumItems();
    if (count == 0)
        return;

    const auto selectedIndex = juce::jlimit(0, count - 1, getSelectedItemIndex());
    setSelectedItemIndex((selectedIndex + offset + count) % count, juce::sendNotificationSync);
}

CharacterDescription::CharacterDescription()
{
    setComponentID("character-description");
    setAccessible(true);
    setInterceptsMouseClicks(false, false);
    setCharacter(0);
}

void CharacterDescription::paint(juce::Graphics& graphics)
{
    const auto scale = designScale();
    const auto& content = text(characterIndex_);

    // The hairline keeps to whole pixels and to the full width of the block,
    // whose right end mirrors the ring of the Evolution knob.
    graphics.setColour(OceanLookAndFeel::hairline().withAlpha(0.75f));
    graphics.fillRect(0,
                      juce::roundToInt(textCentre_ + rows_.rule * scale),
                      getWidth(),
                      juce::jmax(1, juce::roundToInt(block::ruleThickness * scale)));

    juce::Graphics::ScopedSaveState saveState(graphics);
    graphics.addTransform(juce::AffineTransform::scale(scale).translated(0.0f, textCentre_));

    drawLineOfText(graphics, nameFont(), content.name, rows_.nameBaseline,
                   OceanLookAndFeel::primaryText());
    drawLineOfText(graphics, subtitleFont(), content.subtitle, rows_.subtitleBaseline,
                   OceanLookAndFeel::secondaryText());
    paragraph_.draw(graphics,
                    juce::Rectangle<float>(0.0f, rows_.paragraph,
                                           static_cast<float>(designWidth),
                                           paragraph_.getHeight()));
}

std::unique_ptr<juce::AccessibilityHandler> CharacterDescription::createAccessibilityHandler()
{
    return std::make_unique<juce::AccessibilityHandler>(*this,
                                                        juce::AccessibilityRole::staticText);
}

void CharacterDescription::setCharacter(int characterIndex)
{
    characterIndex_ = juce::jlimit(0, CharacterSelector::characterCount - 1, characterIndex);
    const auto& content = text(characterIndex_);
    paragraph_ = paragraphLayout(content.paragraph, static_cast<float>(designWidth));

    // The text is as tall as its paragraph has lines; its rows are measured
    // from its middle.
    const auto nameCapitals = capitalHeight(nameFont());
    const auto subtitleCapitals = capitalHeight(subtitleFont());
    const auto paragraphCapitals = capitalHeight(paragraphFont());
    const auto lineCount = paragraph_.getNumLines();
    const auto firstBaseline = lineCount > 0 ? paragraph_.getLine(0).lineOrigin.y : 0.0f;
    const auto lastBaseline = lineCount > 0 ? paragraph_.getLine(lineCount - 1).lineOrigin.y
                                            : 0.0f;
    const auto textHeight = nameCapitals + block::nameToSubtitle
                          + subtitleCapitals + block::subtitleToRule
                          + block::ruleThickness + block::ruleToParagraph
                          + paragraphCapitals + lastBaseline - firstBaseline;
    rows_.nameBaseline = nameCapitals - 0.5f * textHeight;
    rows_.subtitleBaseline = rows_.nameBaseline + block::nameToSubtitle + subtitleCapitals;
    rows_.rule = rows_.subtitleBaseline + block::subtitleToRule;
    rows_.paragraph = rows_.rule + block::ruleThickness + block::ruleToParagraph
                    + paragraphCapitals - firstBaseline;
    rows_.lastBaseline = rows_.paragraph + lastBaseline;

    setTitle(content.name);
    setDescription(juce::String(content.subtitle) + ". " + content.paragraph);
    repaint();
}

void CharacterDescription::setTextCentre(float centreY)
{
    textCentre_ = centreY;
    repaint();
}

const CharacterDescription::Text& CharacterDescription::text(int characterIndex) noexcept
{
    return characterTexts[static_cast<std::size_t>(
        juce::jlimit(0, CharacterSelector::characterCount - 1, characterIndex))];
}

juce::TextLayout CharacterDescription::paragraphLayout(const char* text, float width)
{
    juce::AttributedString paragraph;
    paragraph.setJustification(juce::Justification::topLeft);
    paragraph.setWordWrap(juce::AttributedString::byWord);
    paragraph.setLineSpacing(block::paragraphLineHeight - block::paragraphFontHeight);
    paragraph.append(text,
                     paragraphFont(),
                     OceanLookAndFeel::primaryText().interpolatedWith(
                         OceanLookAndFeel::secondaryText(), 0.45f));

    const auto hasWidow = [width](const juce::TextLayout& layout)
    {
        return layout.getNumLines() > 1
            && layout.getLine(layout.getNumLines() - 1).getLineBoundsX().getLength()
                   < widowShare * width;
    };

    juce::TextLayout layout;
    layout.createLayout(paragraph, width);
    if (! hasWidow(layout))
        return layout;

    const auto narrowestWidth = (1.0f - block::widowAllowance) * width;
    for (auto narrowedWidth = width - 1.0f; narrowedWidth >= narrowestWidth;
         narrowedWidth -= 1.0f)
    {
        juce::TextLayout narrowed;
        narrowed.createLayout(paragraph, narrowedWidth);
        if (narrowed.getNumLines() == layout.getNumLines() && ! hasWidow(narrowed))
            return narrowed;
    }

    return layout;
}

bool CharacterDescription::textFits() const
{
    const auto& content = text(characterIndex_);
    const auto width = static_cast<float>(designWidth);
    const auto scale = designScale();
    const auto fitsOnOneLine = [width](const juce::Font& font, const char* line)
    {
        return juce::GlyphArrangement::getStringWidth(font, line) <= width;
    };

    return fitsOnOneLine(nameFont(), content.name)
        && fitsOnOneLine(subtitleFont(), content.subtitle)
        && paragraph_.getNumLines() <= maximumParagraphLines
        && paragraph_.getWidth() <= width
        && textCentre_ + scale * (rows_.nameBaseline - capitalHeight(nameFont())) >= 0.0f
        && textCentre_ + scale * (rows_.lastBaseline + paragraphFont().getDescent())
               <= static_cast<float>(getHeight());
}

float CharacterDescription::designScale() const noexcept
{
    return juce::jmin(static_cast<float>(getWidth()) / designWidth,
                      static_cast<float>(getHeight()) / designHeight);
}
} // namespace amanita::ui
