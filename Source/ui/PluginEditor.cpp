#include "PluginEditor.h"

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
    return juce::String(value, 0) + " %";
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
    return juce::String(value, 0) + " Hz";
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
      mixKnob_(processorToUse.getParameterState(), "mix", "Mix", decimalPercentValue),
      currentAccent_(accentForCharacter(characterSelector_.getSelectedIndex())),
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
    characterSelector_.setAccentColour(currentAccent_);
    characterSelector_.onSelectionChanged = [this](int index)
    {
        updateCharacterVisuals(index);
    };

    for (auto* component : std::array<juce::Component*, 9> {
             &characterSelector_, &evolutionKnob_, &preDelayKnob_, &sizeKnob_,
             &decayKnob_, &lowCutKnob_, &dampingKnob_, &widthKnob_, &mixKnob_
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
    freezeButton_.setExplicitFocusOrder(13);
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
    mixKnob_.setFocusOrder(12);

    setResizable(true, true);
    setResizeLimits(minimumWidth, minimumHeight, maximumWidth, maximumHeight);
    if (auto* constrainer = getConstrainer())
        constrainer->setFixedAspectRatio(static_cast<double>(defaultWidth) / defaultHeight);
    setSize(defaultWidth, defaultHeight);
    startTimerHz(24);
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

    const auto heroField = scaledBounds(32.0f, 126.0f, 896.0f, 300.0f).toFloat();
    const auto evolution = static_cast<float>(evolutionKnob_.getSlider().getValue() * 0.01);

    const auto haloCentre = scaledBounds(360.0f, 146.0f, 240.0f, 240.0f)
                                .toFloat()
                                .getCentre();
    const auto haloRadius = heroField.getHeight() * (0.45f + 0.08f * evolution);
    juce::ColourGradient halo(currentAccent_.withAlpha(0.10f + 0.05f * evolution),
                              haloCentre,
                              currentAccent_.withAlpha(0.0f),
                              haloCentre.translated(haloRadius, 0.0f),
                              true);
    halo.addColour(0.36, currentAccent_.withAlpha(0.055f + 0.035f * evolution));
    graphics.setGradientFill(halo);
    graphics.fillEllipse(haloCentre.x - haloRadius,
                         haloCentre.y - haloRadius,
                         haloRadius * 2.0f,
                         haloRadius * 2.0f);

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
    graphics.fillRect(scaledBounds(416.0f, 460.0f, 1.0f, 105.0f));
    graphics.fillRect(scaledBounds(672.0f, 460.0f, 1.0f, 105.0f));

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
    characterSelector_.setBounds(scaledBounds(184.0f, 72.0f, 592.0f, 44.0f));
    evolutionKnob_.setBounds(scaledBounds(360.0f, 154.0f, 240.0f, 268.0f));
    freezeButton_.setBounds(scaledBounds(820.0f, 20.0f, 108.0f, 34.0f));

    constexpr std::array<float, 7> cellX { 32.0f, 160.0f, 288.0f, 416.0f,
                                           544.0f, 672.0f, 800.0f };
    const std::array<amanita::ui::ParameterKnob*, 7> knobs {
        &preDelayKnob_, &sizeKnob_, &decayKnob_, &lowCutKnob_,
        &dampingKnob_, &widthKnob_, &mixKnob_
    };
    for (std::size_t index = 0; index < knobs.size(); ++index)
        knobs[index]->setBounds(scaledBounds(cellX[index] + 4.0f, 452.0f, 120.0f, 126.0f));
}

void AmanitaOceanAudioProcessorEditor::timerCallback()
{
    animationPhase_ += 0.004f;
    if (animationPhase_ >= juce::MathConstants<float>::twoPi)
        animationPhase_ -= juce::MathConstants<float>::twoPi;

    currentAccent_ = currentAccent_.interpolatedWith(targetAccent_, 0.16f);
    lookAndFeel_.setAccentColour(currentAccent_);
    characterSelector_.setAccentColour(currentAccent_);
    repaint();
}

void AmanitaOceanAudioProcessorEditor::updateCharacterVisuals(int characterIndex)
{
    visualCharacter_ = juce::jlimit(0, 3, characterIndex);
    targetAccent_ = accentForCharacter(visualCharacter_);
    repaint();
}

void AmanitaOceanAudioProcessorEditor::drawBathymetricField(juce::Graphics& graphics,
                                                             juce::Rectangle<float> field,
                                                             float evolution) const
{
    const auto centre = field.getCentre().translated(0.0f, -5.0f);
    const auto scale = field.getWidth() / 896.0f;
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
            const auto slow = std::sin(angle * 3.0f + animationPhase_ + spread * 2.2f);
            const auto fine = std::sin(angle * 5.0f - animationPhase_ * 0.63f + spread * 4.1f);
            auto x = centre.x + std::cos(angle) * radiusX;
            auto y = centre.y + std::sin(angle) * radiusY;

            if (visualCharacter_ == 1)
            {
                const auto rise = evolution * (0.30f + 0.70f * spread);
                x += scale * (slow * 3.0f + fine * 1.4f) * rise;
                y -= scale * (8.0f + 24.0f * spread) * rise * (0.5f + 0.5f * std::sin(angle));
            }
            else if (visualCharacter_ == 2)
            {
                const auto drift = evolution * (5.0f + 13.0f * spread) * scale;
                x += drift * std::sin(angle * 2.0f + animationPhase_ * 0.72f);
                y += drift * 0.38f * std::sin(angle * 3.0f - animationPhase_);
            }
            else if (visualCharacter_ == 3)
            {
                const auto veil = evolution * (3.0f + 11.0f * spread) * scale;
                x += veil * 0.55f * fine;
                y = centre.y + (y - centre.y) * (0.88f - 0.10f * evolution)
                  + veil * slow;
            }
            else
            {
                const auto motion = evolution * (1.5f + 4.0f * spread) * scale;
                x += motion * slow;
                y += motion * 0.45f * fine;
            }

            if (point == 0)
                path.startNewSubPath(x, y);
            else
                path.lineTo(x, y);
        }
        path.closeSubPath();
        const auto alpha = (0.025f + 0.035f * evolution) * (1.0f - 0.30f * spread);
        graphics.setColour(currentAccent_.withAlpha(alpha));
        graphics.strokePath(path, juce::PathStrokeType(0.8f + 0.35f * evolution,
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

juce::Colour AmanitaOceanAudioProcessorEditor::accentForCharacter(int characterIndex) noexcept
{
    constexpr std::array<std::uint32_t, 4> colours {
        0xff81bfc7, 0xffc89c83, 0xff829de0, 0xffb3a6c4
    };
    return juce::Colour(colours[static_cast<std::size_t>(juce::jlimit(0, 3, characterIndex))]);
}

juce::String AmanitaOceanAudioProcessorEditor::descriptionForCharacter(int characterIndex)
{
    constexpr std::array<const char*, 4> descriptions {
        "Pure / Open", "Rising / Diffusion", "Spectral / Motion", "Soft / Cloud"
    };
    return descriptions[static_cast<std::size_t>(juce::jlimit(0, 3, characterIndex))];
}
