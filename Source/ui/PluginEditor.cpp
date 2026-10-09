#include "PluginEditor.h"
#include "CharacterPalette.h"
#include "OceanShaderBackground.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
// The calm 4 px vertical rhythm of Amanita Analog Filter with Ocean's own
// hierarchy: the Character drop-down on the window's vertical axis as the
// heading of the group under it, the description block and the Evolution knob
// mirrored about that axis.
namespace layout
{
constexpr float axisX = 480.0f;
constexpr float titleY = 18.0f;
constexpr float subtitleY = 44.0f;
constexpr float headerRuleY = 80.0f;
constexpr float characterSelectorY = 112.0f;
constexpr float characterSelectorWidth = 340.0f;
constexpr float characterSelectorHeight = 40.0f;
// The Evolution knob is placed by the centre of its dial. The outer edge of
// its ring lies 103.6 / 224 of the dial's size from that centre, as
// OceanLookAndFeel draws it.
constexpr float evolutionCentreX = 614.0f;
constexpr float evolutionCentreY = 296.0f;
constexpr float evolutionWidth = amanita::ui::ParameterKnob::heroWidth;
constexpr float evolutionHeight = amanita::ui::ParameterKnob::heroHeight;
constexpr float evolutionDialSize = amanita::ui::ParameterKnob::heroDialSize;
constexpr float evolutionRingShare = 103.6f / 224.0f;
constexpr float evolutionX = evolutionCentreX - 0.5f * evolutionWidth;
constexpr float evolutionY = evolutionCentreY - 0.5f * evolutionDialSize;
// The description block ends as far left of the axis as the ring begins right
// of it and stands round the middle of the whole Evolution control. resized()
// takes both from the knob as it is laid out.
constexpr float descriptionWidth = amanita::ui::CharacterDescription::designWidth;
constexpr float descriptionHeight = amanita::ui::CharacterDescription::designHeight;
constexpr float contourFieldY = 126.0f;
constexpr float contourFieldHeight = 346.0f;
constexpr float footerRuleY = 484.0f;
constexpr float lowerRowY = 500.0f;
constexpr float lowerDividerY = 508.0f;
} // namespace layout

static_assert(amanita::ui::CharacterSelector::characterCount
              == amanita::ui::DeepCurrentRenderer::characterCount);
static_assert(amanita::ui::CharacterSelector::characterCount
              == amanita::ui::OceanShaderBackground::characterCount);

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
      harmonyKnob_(processorToUse.getParameterState(), "harmony", "Harmony",
                   decimalPercentValue),
      widthKnob_(processorToUse.getParameterState(), "width", "Width", decimalPercentValue),
      focusKnob_(processorToUse.getParameterState(), "focus", "Focus",
                 decimalPercentValue),
      mixKnob_(processorToUse.getParameterState(), "mix", "Mix", decimalPercentValue),
      currentAccent_(amanita::ui::characterAccent(characterSelector_.getSelectedItemIndex())),
      targetAccent_(currentAccent_),
      visualCharacter_(characterSelector_.getSelectedItemIndex())
{
    setComponentID("amanita-ocean-editor");
    setName("Amanita Ocean");
    setTitle("Amanita Ocean reverb editor");
    setDescription("Custom editor for the Amanita Ocean evolving reverb");
    // The OpenGL context composites the JUCE controls over the shader. The
    // software fallback still paints every pixel when the shader is absent.
    setOpaque(false);
    setLookAndFeel(&lookAndFeel_);

    lookAndFeel_.setAccentColour(currentAccent_);
    characterDescription_.setCharacter(visualCharacter_);
    characterSelector_.onChange = [this]
    {
        updateCharacterVisuals(characterSelector_.getSelectedItemIndex());
    };

    for (auto* component : std::array<juce::Component*, 12> {
             &characterSelector_, &characterDescription_, &evolutionKnob_,
             &preDelayKnob_, &sizeKnob_, &decayKnob_, &lowCutKnob_, &dampingKnob_,
             &harmonyKnob_, &widthKnob_, &focusKnob_, &mixKnob_
         })
        addAndMakeVisible(*component);

    monoSafeButton_.setComponentID("mono-safe");
    monoSafeButton_.setAccessible(true);
    monoSafeButton_.setButtonText("MONO SAFE");
    monoSafeButton_.setName("Mono Safe Stereo");
    monoSafeButton_.setTitle("Mono Safe Stereo");
    monoSafeButton_.setDescription(
        "Keep every reverb line present in mono and gently centre the sub tail");
    monoSafeButton_.setHelpText(
        "Enable the mono-safe stereo field and Sub Anchor.");
    monoSafeButton_.setTooltip(
        "Mono-safe stereo decoder and Sub Anchor");
    monoSafeButton_.setClickingTogglesState(true);
    monoSafeButton_.setWantsKeyboardFocus(true);
    monoSafeButton_.setExplicitFocusOrder(15);
    addAndMakeVisible(monoSafeButton_);
    monoSafeAttachment_
        = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            processor_.getParameterState(), "monoSafe", monoSafeButton_);

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
    freezeButton_.setExplicitFocusOrder(16);
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
    harmonyKnob_.setFocusOrder(11);
    widthKnob_.setFocusOrder(12);
    focusKnob_.setFocusOrder(13);
    mixKnob_.setFocusOrder(14);

    const auto initialCurrentVisual = processor_.getCurrentVisualSnapshot();
    deepCurrent_.setCurrentFieldSnapshot(initialCurrentVisual.flowX,
                                         initialCurrentVisual.flowY,
                                         initialCurrentVisual.strength);
    deepCurrent_.reset(visualCharacter_,
                       static_cast<float>(evolutionKnob_.getSlider().getValue() * 0.01),
                       freezeButton_.getToggleState());
    shaderBackground_ = std::make_unique<amanita::ui::OceanShaderBackground>();
    shaderBackground_->setSnapshot(
        visualCharacter_,
        static_cast<float>(evolutionKnob_.getSlider().getValue() * 0.01),
        static_cast<float>(focusKnob_.getSlider().getValue() * 0.01),
        freezeButton_.getToggleState(),
        initialCurrentVisual.flowX,
        initialCurrentVisual.flowY,
        initialCurrentVisual.strength,
        currentAccent_);

    setResizable(true, true);
    setResizeLimits(minimumWidth, minimumHeight, maximumWidth, maximumHeight);
    if (auto* constrainer = getConstrainer())
        constrainer->setFixedAspectRatio(static_cast<double>(defaultWidth) / defaultHeight);
    setSize(defaultWidth, defaultHeight);
    shaderBackground_->attachTo(*this);
    startTimerHz(30);
}

AmanitaOceanAudioProcessorEditor::~AmanitaOceanAudioProcessorEditor()
{
    stopTimer();
    if (shaderBackground_ != nullptr)
        shaderBackground_->detach();
    monoSafeAttachment_.reset();
    freezeAttachment_.reset();
    setLookAndFeel(nullptr);
}

void AmanitaOceanAudioProcessorEditor::paint(juce::Graphics& graphics)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto shaderReady = shaderBackground_ != nullptr
                          && shaderBackground_->isReady();
    if (! shaderReady)
    {
        juce::ColourGradient background(amanita::ui::OceanLookAndFeel::backgroundTop(),
                                        bounds.getTopLeft(),
                                        amanita::ui::OceanLookAndFeel::backgroundBottom(),
                                        bounds.getBottomLeft(),
                                        false);
        background.addColour(0.58, juce::Colour::fromRGB(9, 23, 26));
        graphics.setGradientFill(background);
        graphics.fillRect(bounds);
        deepCurrent_.paint(graphics, bounds);
    }
    else
    {
        // Keep the controls calm and readable without hiding the moving field.
        juce::ColourGradient readability(
            amanita::ui::OceanLookAndFeel::backgroundTop().withAlpha(0.17f),
            bounds.getTopLeft(),
            amanita::ui::OceanLookAndFeel::backgroundBottom().withAlpha(0.08f),
            bounds.getBottomLeft(),
            false);
        readability.addColour(
            0.68,
            amanita::ui::OceanLookAndFeel::backgroundBottom().withAlpha(0.035f));
        graphics.setGradientFill(readability);
        graphics.fillRect(bounds);
    }

    if (! shaderReady)
    {
        const auto contourField = scaledBounds(0.0f, layout::contourFieldY,
                                               defaultWidth, layout::contourFieldHeight)
                                      .toFloat();
        drawBathymetricField(graphics, contourField, evolutionDialCentre(),
                             deepCurrent_.getEvolution());
    }

    const auto sx = static_cast<float>(getWidth()) / defaultWidth;
    const auto sy = static_cast<float>(getHeight()) / defaultHeight;
    const auto scale = juce::jmin(sx, sy);

    const auto titleFont = uiFont(18.0f * scale, juce::Font::bold, 0.11f);
    juce::AttributedString title;
    title.setJustification(juce::Justification::centredLeft);
    title.setWordWrap(juce::AttributedString::none);
    title.append("AMANITA ", titleFont,
                 amanita::ui::OceanLookAndFeel::primaryText());
    title.append("OCEAN", titleFont, currentAccent_);
    title.draw(graphics,
               scaledBounds(32.0f, layout::titleY, 300.0f, 24.0f).toFloat());

    graphics.setColour(amanita::ui::OceanLookAndFeel::secondaryText().withAlpha(0.82f));
    graphics.setFont(uiFont(9.5f * scale, juce::Font::plain, 0.075f));
    graphics.drawText("EVOLVING SPACE REVERB  /  v" JucePlugin_VersionString,
                      scaledBounds(33.0f, layout::subtitleY, 320.0f, 16.0f),
                      juce::Justification::centredLeft, false);

    graphics.setColour(amanita::ui::OceanLookAndFeel::hairline().withAlpha(0.75f));
    graphics.fillRect(scaledBounds(32.0f, layout::headerRuleY, 896.0f, 1.0f));
    graphics.fillRect(scaledBounds(32.0f, layout::footerRuleY, 896.0f, 1.0f));
    graphics.fillRect(scaledBounds(32.0f + 896.0f / 3.0f,
                                   layout::lowerDividerY, 1.0f, 105.0f));
    graphics.fillRect(scaledBounds(32.0f + 2.0f * 896.0f / 3.0f,
                                   layout::lowerDividerY, 1.0f, 105.0f));

    graphics.setColour(amanita::ui::OceanLookAndFeel::hairline().withAlpha(0.55f));
    graphics.drawRoundedRectangle(bounds.reduced(0.5f), 8.0f * scale, 1.0f);
}

void AmanitaOceanAudioProcessorEditor::resized()
{
    characterSelector_.setBounds(
        scaledBounds(layout::axisX - 0.5f * layout::characterSelectorWidth,
                     layout::characterSelectorY,
                     layout::characterSelectorWidth, layout::characterSelectorHeight));
    evolutionKnob_.setBounds(
        scaledBounds(layout::evolutionX, layout::evolutionY,
                     layout::evolutionWidth, layout::evolutionHeight));
    // The description block takes the whole pixels nearest to the mirror
    // image of the Evolution ring's left edge about the window's axis and to
    // the middle of the Evolution control, and sets its text on that middle
    // itself.
    const auto ringLeft = evolutionDialCentre().x - evolutionRingRadius();
    const auto controlMiddle = evolutionControlMiddle();
    const auto descriptionSize = scaledBounds(0.0f, 0.0f, layout::descriptionWidth,
                                              layout::descriptionHeight);
    const auto descriptionBounds = descriptionSize.withPosition(
        juce::roundToInt(static_cast<float>(getWidth()) - ringLeft)
            - descriptionSize.getWidth(),
        juce::roundToInt(controlMiddle - 0.5f * static_cast<float>(descriptionSize.getHeight())));
    characterDescription_.setBounds(descriptionBounds);
    characterDescription_.setTextCentre(controlMiddle
                                        - static_cast<float>(descriptionBounds.getY()));
    constexpr auto freezeHeight = 34.0f;
    const auto freezeWidth = fittedToggleWidth(freezeButton_.getButtonText(),
                                               freezeHeight, 72.0f, 120.0f);
    const auto freezeX = 928.0f - freezeWidth;
    freezeButton_.setBounds(scaledBounds(freezeX, 20.0f,
                                         freezeWidth, freezeHeight));
    const auto monoSafeWidth = fittedToggleWidth(monoSafeButton_.getButtonText(),
                                                 freezeHeight, 88.0f, 136.0f);
    monoSafeButton_.setBounds(scaledBounds(freezeX - 8.0f - monoSafeWidth, 20.0f,
                                           monoSafeWidth, freezeHeight));

    constexpr auto rowLeft = 32.0f;
    constexpr auto rowWidth = 896.0f;
    constexpr auto cellWidth = rowWidth / 9.0f;
    const std::array<amanita::ui::ParameterKnob*, 9> knobs {
        &preDelayKnob_, &sizeKnob_, &decayKnob_, &lowCutKnob_,
        &dampingKnob_, &harmonyKnob_, &widthKnob_, &focusKnob_, &mixKnob_
    };
    for (std::size_t index = 0; index < knobs.size(); ++index)
    {
        const auto x = rowLeft + static_cast<float>(index) * cellWidth;
        knobs[index]->setBounds(scaledBounds(x + 2.0f, layout::lowerRowY,
                                             cellWidth - 4.0f, 126.0f));
    }

    // The background gathers round the Evolution knob and lies low behind the
    // description block, wherever this size has put them.
    const auto width = static_cast<float>(getWidth());
    const auto height = static_cast<float>(getHeight());
    const auto focalPoint = evolutionDialCentre();
    const auto calmRegion = characterDescription_.getBounds().toFloat();
    deepCurrent_.setFocalPoint(focalPoint.x / width, focalPoint.y / height);
    deepCurrent_.setSize(getWidth(), getHeight());
    if (shaderBackground_ != nullptr)
        shaderBackground_->setLayout({ focalPoint.x / width, focalPoint.y / height },
                                     { calmRegion.getX() / width,
                                       calmRegion.getY() / height,
                                       calmRegion.getWidth() / width,
                                       calmRegion.getHeight() / height });

    if (shaderBackground_ == nullptr || ! shaderBackground_->isReady())
        deepCurrent_.render(currentAccent_);
    else
        shaderBackground_->triggerRepaint();
    backgroundDirty_ = false;
}

void AmanitaOceanAudioProcessorEditor::timerCallback()
{
    if (!isShowing())
        return;

    constexpr auto timerRate = 30.0;
    const auto evolution = static_cast<float>(evolutionKnob_.getSlider().getValue() * 0.01);
    const auto frozen = freezeButton_.getToggleState();
    const auto currentVisual = processor_.getCurrentVisualSnapshot();
    deepCurrent_.setCurrentFieldSnapshot(currentVisual.flowX,
                                         currentVisual.flowY,
                                         currentVisual.strength);
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
    if (currentAccent_ != previousAccent)
        repaint(scaledBounds(30.0f, 16.0f, 305.0f, 29.0f));

    const auto shaderReady = shaderBackground_ != nullptr
                          && shaderBackground_->isReady();
    if (shaderReady != shaderReadyPreviously_)
    {
        shaderReadyPreviously_ = shaderReady;
        backgroundDirty_ = true;
        // OpenGLContext::triggerRepaint() does not invalidate JUCE's cached
        // component texture. Repaint once so the opaque CPU fallback is
        // replaced by the transparent readability overlay (or vice versa).
        repaint();
    }

    if (shaderBackground_ != nullptr)
    {
        shaderBackground_->setSnapshot(
            visualCharacter_,
            deepCurrent_.getEvolution(),
            static_cast<float>(focusKnob_.getSlider().getValue() * 0.01),
            frozen,
            currentVisual.flowX,
            currentVisual.flowY,
            currentVisual.strength,
            currentAccent_);
    }

    if (shaderReady)
    {
        if (currentAccent_ != previousAccent)
        {
            for (auto* knob : std::array<amanita::ui::ParameterKnob*, 10> {
                     &evolutionKnob_, &preDelayKnob_, &sizeKnob_, &decayKnob_,
                     &lowCutKnob_, &dampingKnob_, &harmonyKnob_, &widthKnob_,
                     &focusKnob_, &mixKnob_
                 })
                knob->repaint();
            characterSelector_.repaint();
            freezeButton_.repaint();
            monoSafeButton_.repaint();
        }

        // The shader integrates its own smoothed Freeze/mode transitions. A
        // fixed 30 Hz request keeps those transitions continuous and leaves
        // the audio thread completely uninvolved.
        shaderBackground_->triggerRepaint();
        backgroundDirty_ = false;
    }
    else if (backgroundDirty_)
    {
        deepCurrent_.render(currentAccent_);
        repaint();
        backgroundDirty_ = false;
    }
}

void AmanitaOceanAudioProcessorEditor::updateCharacterVisuals(int characterIndex)
{
    visualCharacter_ = juce::jlimit(
        0, amanita::ui::CharacterSelector::characterCount - 1, characterIndex);
    targetAccent_ = amanita::ui::characterAccent(visualCharacter_);
    characterDescription_.setCharacter(visualCharacter_);
    backgroundDirty_ = true;
    repaint();
}

void AmanitaOceanAudioProcessorEditor::drawBathymetricField(juce::Graphics& graphics,
                                                             juce::Rectangle<float> field,
                                                             juce::Point<float> centre,
                                                             float evolution) const
{
    const auto scale = field.getWidth() / defaultWidth;
    const auto phase = static_cast<float>(deepCurrent_.getTimeSeconds() * 0.096);
    const auto& characterBlend = deepCurrent_.getCharacterBlend();
    const auto currentFlowX = deepCurrent_.getCurrentFieldFlowX();
    const auto currentFlowY = deepCurrent_.getCurrentFieldFlowY();
    const auto currentFieldStrength = deepCurrent_.getCurrentFieldStrength();
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

            const auto currentDepth = (0.18f + 0.82f * currentFieldStrength)
                                    * (4.0f + 15.0f * spread) * scale;
            const auto currentX = baseX
                + currentDepth * (0.82f * currentFlowX + 0.18f * fine);
            const auto currentY = baseY
                + currentDepth * (0.58f * currentFlowY + 0.20f * slow);

            const auto fathomSwell = std::sin(phase * 0.56f - spread * 1.2f);
            const auto fathom = evolution * (3.0f + 10.0f * spread) * scale;
            const auto fathomX = baseX + fathom * std::cos(angle) * fathomSwell;
            const auto fathomY = baseY - fathom * 0.62f * std::sin(angle) * fathomSwell
                + fathom * 0.16f * fine;

            const auto undertowSwell = std::sin(phase * 0.43f + spread * 1.2f);
            const auto undertow = evolution * (2.6f + 8.5f * spread) * scale;
            const auto undertowX = baseX + undertow * std::cos(angle) * undertowSwell;
            const auto undertowY = baseY - undertow * 0.62f * std::sin(angle) * undertowSwell
                + undertow * 0.16f * fine;

            const auto x = characterBlend[0] * defaultX
                         + characterBlend[1] * bloomX
                         + characterBlend[2] * driftX
                         + characterBlend[3] * veilX
                         + characterBlend[4] * currentX
                         + characterBlend[5] * fathomX
                         + characterBlend[6] * undertowX;
            const auto y = characterBlend[0] * defaultY
                         + characterBlend[1] * bloomY
                         + characterBlend[2] * driftY
                         + characterBlend[3] * veilY
                         + characterBlend[4] * currentY
                         + characterBlend[5] * fathomY
                         + characterBlend[6] * undertowY;

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

juce::Point<float> AmanitaOceanAudioProcessorEditor::evolutionDialCentre() const
{
    const auto& dial = evolutionKnob_.getSlider();
    return getLocalArea(&dial, dial.getLocalBounds()).toFloat().getCentre();
}

float AmanitaOceanAudioProcessorEditor::evolutionRingRadius() const
{
    return static_cast<float>(evolutionKnob_.getSlider().getWidth())
         * layout::evolutionRingShare;
}

float AmanitaOceanAudioProcessorEditor::evolutionControlMiddle()
{
    const auto ringTop = evolutionDialCentre().y - evolutionRingRadius();
    const auto valueFoot = static_cast<float>(evolutionKnob_.getY())
                         + evolutionKnob_.getValueBaseline();
    return 0.5f * (ringTop + valueFoot);
}
