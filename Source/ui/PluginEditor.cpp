#include "PluginEditor.h"
#include "CharacterPalette.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
[[nodiscard]] juce::Font uiFont(float height,
                                int style = juce::Font::plain,
                                float kerning = 0.0f)
{
    return juce::Font { juce::FontOptions(juce::Font::getDefaultSansSerifFontName(),
                                          height,
                                          style)
                            .withKerningFactor(kerning) };
}

[[nodiscard]] juce::String percentValue(double value)
{
    return juce::String(juce::roundToInt(value)) + " %";
}

[[nodiscard]] juce::String decimalPercentValue(double value)
{
    return juce::String(value, 1) + " %";
}

[[nodiscard]] juce::String secondsValue(double value)
{
    return juce::String(value, value < 10.0 ? 2 : 1) + " s";
}

[[nodiscard]] juce::String millisecondsValue(double value)
{
    return juce::String(value, 1) + " ms";
}

[[nodiscard]] juce::String hertzValue(double value)
{
    return juce::String(juce::roundToInt(value)) + " Hz";
}

[[nodiscard]] float fittedToggleWidth(const juce::String& text,
                                      float height,
                                      float minimumWidth,
                                      float maximumWidth)
{
    const auto paintedHeight = std::max(1.0f, height - 1.4f);
    const auto controlScale = juce::jlimit(0.65f, 1.75f, paintedHeight / 38.6f);
    const auto font = uiFont(11.0f * controlScale, juce::Font::bold, 0.075f);
    const auto contentWidth = 42.0f * controlScale
        + juce::GlyphArrangement::getStringWidth(font, text.toUpperCase());
    const auto gridWidth = 4.0f * std::ceil(contentWidth / 4.0f);
    return juce::jlimit(minimumWidth, maximumWidth, gridWidth);
}

} // namespace

AmanitaOceanAudioProcessorEditor::AmanitaOceanAudioProcessorEditor(
    AmanitaOceanAudioProcessor& processorToUse)
    : AudioProcessorEditor(processorToUse),
      processor_(processorToUse),
      characterSelector_(processorToUse.getParameterState()),
      evolutionKnob_(processorToUse.getParameterState(), "evolution", "Evolution",
                     percentValue, true),
      preDelayKnob_(processorToUse.getParameterState(), "preDelay", "Pre-delay",
                    millisecondsValue),
      sizeKnob_(processorToUse.getParameterState(), "size", "Size", decimalPercentValue),
      decayKnob_(processorToUse.getParameterState(), "decay", "Decay", secondsValue),
      lowCutKnob_(processorToUse.getParameterState(), "lowCut", "Low Cut", hertzValue),
      dampingKnob_(processorToUse.getParameterState(), "highDamping", "Damping", hertzValue),
      widthKnob_(processorToUse.getParameterState(), "width", "Width", decimalPercentValue),
      focusKnob_(processorToUse.getParameterState(), "focus", "Focus",
                 decimalPercentValue),
      mixKnob_(processorToUse.getParameterState(), "mix", "Mix", decimalPercentValue),
      currentAccent_(amanita::ui::characterAccent(characterSelector_.getSelectedIndex())),
      targetAccent_(currentAccent_),
      visualCharacter_(characterSelector_.getSelectedIndex())
{
    setComponentID("amanita-ocean-editor");
    setName("Amanita Ocean");
    setTitle("Amanita Ocean reverb editor");
    setDescription("Custom editor for the Amanita Ocean evolving reverb");
    setOpaque(true);
    setLookAndFeel(&lookAndFeel_);

    lookAndFeel_.setAccentColour(currentAccent_);
    characterSelector_.onSelectionChanged = [this](int index)
    {
        updateCharacterVisuals(index);
    };

    for (auto* component : std::array<juce::Component*, 10> {
             &characterSelector_, &evolutionKnob_, &preDelayKnob_, &sizeKnob_,
             &decayKnob_, &lowCutKnob_, &dampingKnob_, &widthKnob_, &focusKnob_,
             &mixKnob_
         })
        addAndMakeVisible(*component);

    freezeButton_.setComponentID("freeze");
    freezeButton_.setAccessible(true);
    freezeButton_.setButtonText("FREEZE");
    freezeButton_.setName("Freeze");
    freezeButton_.setTitle("Freeze");
    freezeButton_.setDescription("Hold the current reverb tail");
    freezeButton_.setHelpText("Press Space to hold or release the current reverb tail.");
    freezeButton_.setTooltip("Freeze the current tail");
    freezeButton_.setClickingTogglesState(true);
    freezeButton_.setWantsKeyboardFocus(true);
    freezeButton_.setExplicitFocusOrder(14);
    addAndMakeVisible(freezeButton_);
    freezeAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor_.getParameterState(), "freeze", freezeButton_);

    characterSelector_.setExplicitFocusOrder(1);
    evolutionKnob_.setFocusOrder(5);
    preDelayKnob_.setFocusOrder(6);
    sizeKnob_.setFocusOrder(7);
    decayKnob_.setFocusOrder(8);
    lowCutKnob_.setFocusOrder(9);
    dampingKnob_.setFocusOrder(10);
    widthKnob_.setFocusOrder(11);
    focusKnob_.setFocusOrder(12);
    mixKnob_.setFocusOrder(13);

    deepCurrent_.reset(visualCharacter_,
                       static_cast<float>(evolutionKnob_.getSlider().getValue() * 0.01),
                       freezeButton_.getToggleState());

    setResizable(true, true);
    setResizeLimits(minimumWidth, minimumHeight, maximumWidth, maximumHeight);
    if (auto* constrainer = getConstrainer())
        constrainer->setFixedAspectRatio(static_cast<double>(defaultWidth) / defaultHeight);
    setSize(defaultWidth, defaultHeight);
    startTimerHz(30);
}

AmanitaOceanAudioProcessorEditor::~AmanitaOceanAudioProcessorEditor()
{
    stopTimer();
    freezeAttachment_.reset();
    setLookAndFeel(nullptr);
}

void AmanitaOceanAudioProcessorEditor::paint(juce::Graphics& graphics)
{
    const auto bounds = getLocalBounds().toFloat();
    juce::ColourGradient background(amanita::ui::OceanLookAndFeel::backgroundTop(),
                                    bounds.getTopLeft(),
                                    amanita::ui::OceanLookAndFeel::backgroundBottom(),
                                    bounds.getBottomLeft(),
                                    false);
    background.addColour(0.58, juce::Colour::fromRGB(9, 23, 26));
    graphics.setGradientFill(background);
    graphics.fillRect(bounds);

    deepCurrent_.paint(graphics, bounds);

    const auto heroField = scaledBounds(32.0f, 126.0f, 896.0f, 300.0f).toFloat();
    const auto evolution = deepCurrent_.getEvolution();

    drawBathymetricField(graphics, heroField, evolution);

    const auto sx = static_cast<float>(getWidth()) / defaultWidth;
    const auto sy = static_cast<float>(getHeight()) / defaultHeight;
    const auto scale = juce::jmin(sx, sy);

    graphics.setColour(amanita::ui::OceanLookAndFeel::primaryText());
    graphics.setFont(uiFont(18.0f * scale, juce::Font::bold, 0.11f));
    graphics.drawText("AMANITA OCEAN", scaledBounds(32.0f, 18.0f, 300.0f, 25.0f),
                      juce::Justification::centredLeft, false);

    graphics.setColour(amanita::ui::OceanLookAndFeel::secondaryText().withAlpha(0.82f));
    graphics.setFont(uiFont(9.5f * scale, juce::Font::plain, 0.075f));
    graphics.drawText("EVOLVING SPACE REVERB  /  v" JucePlugin_VersionString,
                      scaledBounds(33.0f, 42.0f, 320.0f, 16.0f),
                      juce::Justification::centredLeft, false);

    graphics.setColour(amanita::ui::OceanLookAndFeel::hairline().withAlpha(0.75f));
    graphics.fillRect(scaledBounds(32.0f, 61.0f, 896.0f, 1.0f));
    graphics.fillRect(scaledBounds(32.0f, 437.0f, 896.0f, 1.0f));
    graphics.fillRect(scaledBounds(368.0f, 460.0f, 1.0f, 105.0f));
    graphics.fillRect(scaledBounds(592.0f, 460.0f, 1.0f, 105.0f));

    graphics.setColour(amanita::ui::OceanLookAndFeel::secondaryText().withAlpha(0.78f));
    graphics.setFont(uiFont(10.0f * scale, juce::Font::bold, 0.12f));
    graphics.drawText(descriptionForCharacter(visualCharacter_).toUpperCase(),
                      scaledBounds(320.0f, 120.0f, 320.0f, 18.0f),
                      juce::Justification::centred, false);

    graphics.setColour(amanita::ui::OceanLookAndFeel::hairline().withAlpha(0.55f));
    graphics.drawRoundedRectangle(bounds.reduced(0.5f), 8.0f * scale, 1.0f);
}

void AmanitaOceanAudioProcessorEditor::resized()
{
    deepCurrent_.setSize(getWidth(), getHeight());
    deepCurrent_.render(currentAccent_);
    backgroundDirty_ = false;

    characterSelector_.setBounds(scaledBounds(184.0f, 72.0f, 592.0f, 44.0f));
    evolutionKnob_.setBounds(scaledBounds(360.0f, 154.0f, 240.0f, 268.0f));
    constexpr auto freezeHeight = 34.0f;
    const auto freezeWidth = fittedToggleWidth(freezeButton_.getButtonText(),
                                               freezeHeight, 72.0f, 120.0f);
    freezeButton_.setBounds(scaledBounds(928.0f - freezeWidth, 20.0f,
                                         freezeWidth, freezeHeight));

    constexpr std::array<float, 8> cellX { 32.0f, 144.0f, 256.0f, 368.0f,
                                           480.0f, 592.0f, 704.0f, 816.0f };
    const std::array<amanita::ui::ParameterKnob*, 8> knobs {
        &preDelayKnob_, &sizeKnob_, &decayKnob_, &lowCutKnob_,
        &dampingKnob_, &widthKnob_, &focusKnob_, &mixKnob_
    };
    for (std::size_t index = 0; index < knobs.size(); ++index)
        knobs[index]->setBounds(scaledBounds(cellX[index] + 2.0f, 452.0f, 108.0f, 126.0f));
}

void AmanitaOceanAudioProcessorEditor::timerCallback()
{
    if (!isShowing())
        return;

    constexpr auto timerRate = 30.0;
    const auto evolution = static_cast<float>(evolutionKnob_.getSlider().getValue() * 0.01);
    const auto frozen = freezeButton_.getToggleState();
    backgroundDirty_ = deepCurrent_.advance(1.0 / timerRate,
                                            visualCharacter_,
                                            evolution,
                                            frozen)
                    || backgroundDirty_;

    const auto previousAccent = currentAccent_;
    currentAccent_ = currentAccent_.interpolatedWith(targetAccent_, 0.16f);
    if (currentAccent_ == previousAccent && currentAccent_ != targetAccent_)
        currentAccent_ = targetAccent_;
    backgroundDirty_ = currentAccent_ != previousAccent || backgroundDirty_;
    lookAndFeel_.setAccentColour(currentAccent_);
    const auto highRefresh = evolutionKnob_.getSlider().isMouseButtonDown()
                          || currentAccent_ != targetAccent_
                          || deepCurrent_.needsHighRefresh(visualCharacter_, evolution, frozen);
    const auto scheduledBackgroundFrame = ++backgroundFrameCounter_ % 2 == 0;
    if ((highRefresh || scheduledBackgroundFrame) && backgroundDirty_)
    {
        deepCurrent_.render(currentAccent_);
        repaint();
        backgroundDirty_ = false;
    }
}

void AmanitaOceanAudioProcessorEditor::updateCharacterVisuals(int characterIndex)
{
    visualCharacter_ = juce::jlimit(0, 3, characterIndex);
    targetAccent_ = amanita::ui::characterAccent(visualCharacter_);
    backgroundDirty_ = true;
    repaint();
}

void AmanitaOceanAudioProcessorEditor::drawBathymetricField(juce::Graphics& graphics,
                                                             juce::Rectangle<float> field,
                                                             float evolution) const
{
    const auto centre = field.getCentre().translated(0.0f, -5.0f);
    const auto scale = field.getWidth() / 896.0f;
    const auto phase = static_cast<float>(deepCurrent_.getTimeSeconds() * 0.096);
    const auto& characterBlend = deepCurrent_.getCharacterBlend();
    constexpr auto pointsPerContour = 112;
    constexpr auto contourCount = 10;

    graphics.saveState();
    graphics.reduceClipRegion(field.toNearestInt());
    for (auto contour = 0; contour < contourCount; ++contour)
    {
        const auto spread = static_cast<float>(contour) / static_cast<float>(contourCount - 1);
        const auto radiusX = scale * (64.0f + spread * 255.0f);
        const auto radiusY = scale * (38.0f + spread * 105.0f);
        juce::Path path;
        for (auto point = 0; point <= pointsPerContour; ++point)
        {
            const auto angle = juce::MathConstants<float>::twoPi
                             * static_cast<float>(point) / pointsPerContour;
            const auto slow = std::sin(angle * 3.0f + phase + spread * 2.2f);
            const auto fine = std::sin(angle * 5.0f - phase * 0.63f + spread * 4.1f);
            const auto baseX = centre.x + std::cos(angle) * radiusX;
            const auto baseY = centre.y + std::sin(angle) * radiusY;

            const auto defaultMotion = evolution * (1.5f + 4.0f * spread) * scale;
            const auto defaultX = baseX + defaultMotion * slow;
            const auto defaultY = baseY + defaultMotion * 0.45f * fine;

            const auto rise = evolution * (0.30f + 0.70f * spread);
            const auto bloomX = baseX + scale * (slow * 3.0f + fine * 1.4f) * rise;
            const auto bloomY = baseY - scale * (8.0f + 24.0f * spread) * rise
                * (0.5f + 0.5f * std::sin(angle));

            const auto drift = evolution * (5.0f + 13.0f * spread) * scale;
            const auto driftX = baseX
                + drift * std::sin(angle * 2.0f + phase * 0.72f);
            const auto driftY = baseY
                + drift * 0.38f * std::sin(angle * 3.0f - phase);

            const auto veil = evolution * (3.0f + 11.0f * spread) * scale;
            const auto veilX = baseX + veil * 0.55f * fine;
            const auto veilY = centre.y
                + (baseY - centre.y) * (0.88f - 0.10f * evolution)
                + veil * slow;

            const auto x = characterBlend[0] * defaultX
                         + characterBlend[1] * bloomX
                         + characterBlend[2] * driftX
                         + characterBlend[3] * veilX;
            const auto y = characterBlend[0] * defaultY
                         + characterBlend[1] * bloomY
                         + characterBlend[2] * driftY
                         + characterBlend[3] * veilY;

            if (point == 0)
                path.startNewSubPath(x, y);
            else
                path.lineTo(x, y);
        }
        path.closeSubPath();
        const auto alpha = 0.052f * (1.0f - 0.30f * spread);
        graphics.setColour(currentAccent_.withAlpha(alpha));
        graphics.strokePath(path, juce::PathStrokeType(0.98f * scale,
                                                        juce::PathStrokeType::curved,
                                                        juce::PathStrokeType::rounded));
    }
    graphics.restoreState();
}

juce::Rectangle<int> AmanitaOceanAudioProcessorEditor::scaledBounds(float x,
                                                                     float y,
                                                                     float width,
                                                                     float height) const
{
    const auto scale = juce::jmin(static_cast<float>(getWidth()) / defaultWidth,
                                  static_cast<float>(getHeight()) / defaultHeight);
    const auto contentWidth = defaultWidth * scale;
    const auto contentHeight = defaultHeight * scale;
    const auto offsetX = 0.5f * (static_cast<float>(getWidth()) - contentWidth);
    const auto offsetY = 0.5f * (static_cast<float>(getHeight()) - contentHeight);
    return juce::Rectangle<float>(offsetX + x * scale,
                                  offsetY + y * scale,
                                  width * scale,
                                  height * scale)
        .toNearestInt();
}

juce::String AmanitaOceanAudioProcessorEditor::descriptionForCharacter(int characterIndex)
{
    constexpr std::array<const char*, 4> descriptions {
        "Pure / Open", "Rising / Diffusion", "Spectral / Motion", "Soft / Cloud"
    };
    return descriptions[static_cast<std::size_t>(juce::jlimit(0, 3, characterIndex))];
}
