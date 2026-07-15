#include "DeepCurrentRenderer.h"

#include <algorithm>
#include <cmath>

namespace amanita::ui
{
namespace
{
constexpr auto cacheScale = 1.0f;
constexpr auto maximumCacheWidth = 1200;
constexpr auto maximumCacheHeight = 750;
constexpr auto contourCount = 15;
constexpr auto pointsPerContour = 104;
constexpr auto flowLineCount = 13;
constexpr auto pointsPerFlowLine = 96;

[[nodiscard]] float smoothingCoefficient(double seconds, double timeConstant) noexcept
{
    if (seconds <= 0.0)
        return 0.0f;

    return static_cast<float>(1.0 - std::exp(-seconds / timeConstant));
}

[[nodiscard]] double phaseForPeriod(double timeSeconds, double periodSeconds) noexcept
{
    return timeSeconds * juce::MathConstants<double>::twoPi / periodSeconds;
}

void drawRadialGlow(juce::Graphics& graphics,
                    juce::Point<float> centre,
                    float radiusX,
                    float radiusY,
                    juce::Colour colour,
                    float alpha)
{
    juce::Graphics::ScopedSaveState state(graphics);
    graphics.addTransform(juce::AffineTransform::scale(radiusX, radiusY)
                              .translated(centre.x, centre.y));
    juce::ColourGradient gradient(colour.withAlpha(alpha),
                                  0.0f,
                                  0.0f,
                                  colour.withAlpha(0.0f),
                                  1.0f,
                                  0.0f,
                                  true);
    gradient.addColour(0.48, colour.withAlpha(alpha * 0.54f));
    graphics.setGradientFill(gradient);
    graphics.fillEllipse(-1.0f, -1.0f, 2.0f, 2.0f);
}
} // namespace

void DeepCurrentRenderer::reset(int characterIndex,
                                float evolution,
                                bool frozen,
                                double timeSeconds) noexcept
{
    const auto safeCharacter = juce::jlimit(0, characterCount - 1, characterIndex);
    characterBlend_.fill(0.0f);
    characterBlend_[static_cast<std::size_t>(safeCharacter)] = 1.0f;
    evolution_ = juce::jlimit(0.0f, 1.0f, evolution);
    motion_ = frozen ? 0.0f : 1.0f;
    timeSeconds_ = std::isfinite(timeSeconds) ? timeSeconds : 0.0;
}

void DeepCurrentRenderer::setSize(int logicalWidth, int logicalHeight)
{
    if (logicalWidth <= 0 || logicalHeight <= 0)
    {
        overlay_ = {};
        return;
    }

    const auto scale = std::min({ cacheScale,
                                  static_cast<float>(maximumCacheWidth)
                                      / static_cast<float>(logicalWidth),
                                  static_cast<float>(maximumCacheHeight)
                                      / static_cast<float>(logicalHeight) });
    const auto width = juce::jmax(1, juce::roundToInt(static_cast<float>(logicalWidth) * scale));
    const auto height = juce::jmax(1, juce::roundToInt(static_cast<float>(logicalHeight) * scale));
    if (overlay_.isValid() && overlay_.getWidth() == width && overlay_.getHeight() == height)
        return;

    overlay_ = juce::Image(juce::Image::ARGB, width, height, true,
                           juce::SoftwareImageType {});
}

bool DeepCurrentRenderer::advance(double elapsedSeconds,
                                  int characterIndex,
                                  float evolution,
                                  bool frozen) noexcept
{
    const auto previousTime = timeSeconds_;
    const auto previousEvolution = evolution_;
    const auto previousMotion = motion_;
    const auto previousBlend = characterBlend_;
    const auto elapsed = juce::jlimit(0.0, 0.1,
                                      std::isfinite(elapsedSeconds) ? elapsedSeconds : 0.0);
    const auto safeCharacter = juce::jlimit(0, characterCount - 1, characterIndex);
    const auto characterSmoothing = smoothingCoefficient(elapsed, 0.80);
    auto blendSum = 0.0f;
    for (auto index = 0; index < characterCount; ++index)
    {
        const auto target = index == safeCharacter ? 1.0f : 0.0f;
        auto& blend = characterBlend_[static_cast<std::size_t>(index)];
        blend += (target - blend) * characterSmoothing;
        blendSum += blend;
    }
    if (blendSum > 1.0e-6f)
        for (auto& blend : characterBlend_)
            blend /= blendSum;

    const auto safeEvolution = std::isfinite(evolution)
        ? juce::jlimit(0.0f, 1.0f, evolution)
        : evolution_;
    evolution_ += (safeEvolution - evolution_)
                * smoothingCoefficient(elapsed, 0.30);

    const auto targetMotion = frozen ? 0.0f : 1.0f;
    const auto motionTime = frozen ? 0.36 : 0.55;
    motion_ += (targetMotion - motion_) * smoothingCoefficient(elapsed, motionTime);
    if (frozen && motion_ < 0.015f)
        motion_ = 0.0f;

    timeSeconds_ += elapsed * static_cast<double>(motion_);

    auto blendChanged = false;
    for (auto index = 0; index < characterCount; ++index)
        blendChanged = blendChanged
            || std::abs(characterBlend_[static_cast<std::size_t>(index)]
                        - previousBlend[static_cast<std::size_t>(index)]) > 1.0e-6f;

    return std::abs(timeSeconds_ - previousTime) > 1.0e-9
        || std::abs(evolution_ - previousEvolution) > 1.0e-6f
        || std::abs(motion_ - previousMotion) > 1.0e-6f
        || blendChanged;
}

void DeepCurrentRenderer::render(juce::Colour accent)
{
    if (!overlay_.isValid())
        return;

    overlay_.clear(overlay_.getBounds(), juce::Colours::transparentBlack);
    juce::Graphics graphics(overlay_);
    const auto width = static_cast<float>(overlay_.getWidth());
    const auto height = static_cast<float>(overlay_.getHeight());
    const auto unit = juce::jmin(width / 480.0f, height / 300.0f);
    const auto evolutionDepth = 0.12f + 0.88f * evolution_;
    const auto veilBlend = characterBlend_[3];

    const auto period43 = phaseForPeriod(timeSeconds_, 43.0);
    const auto period67 = phaseForPeriod(timeSeconds_, 67.0);
    const auto period89 = phaseForPeriod(timeSeconds_, 89.0);
    const auto glowColour = juce::Colour::fromRGB(42, 99, 104)
                                .interpolatedWith(accent.darker(0.36f), 0.55f);
    const auto glowAlpha = 0.038f * (1.0f - 0.12f * veilBlend);
    const auto glowScale = 0.96f + 0.08f * evolution_;

    drawRadialGlow(graphics,
                   { width * static_cast<float>(0.48 + 0.24 * std::sin(period67)),
                     height * static_cast<float>(0.31 + 0.16 * std::cos(period89)) },
                   width * 0.68f * glowScale,
                   height * 0.48f * glowScale,
                   glowColour,
                   glowAlpha);
    drawRadialGlow(graphics,
                   { width * static_cast<float>(0.18 + 0.12 * std::cos(period89 + 1.7)),
                     height * static_cast<float>(0.66 + 0.13 * std::sin(period43)) },
                   width * 0.54f * glowScale,
                   height * 0.42f * glowScale,
                   accent.darker(0.58f),
                   glowAlpha * 0.82f);
    drawRadialGlow(graphics,
                   { width * static_cast<float>(0.82 + 0.10 * std::sin(period43 + 2.4)),
                     height * static_cast<float>(0.52 + 0.15 * std::cos(period67 + 0.6)) },
                   width * 0.50f * glowScale,
                   height * 0.44f * glowScale,
                   juce::Colour::fromRGB(28, 74, 82).interpolatedWith(accent, 0.24f),
                   glowAlpha * 0.76f);

    const auto flowColour = juce::Colour::fromRGB(48, 91, 96)
                                .interpolatedWith(accent, 0.60f);
    for (auto line = 0; line < flowLineCount; ++line)
    {
        const auto lane = (static_cast<float>(line) + 0.34f)
                        / (static_cast<float>(flowLineCount) - 0.32f);
        const auto baseY = height * lane;
        const auto seed = static_cast<float>(line) * 0.73f;
        juce::Path path;
        for (auto point = 0; point <= pointsPerFlowLine; ++point)
        {
            const auto along = static_cast<float>(point)
                             / static_cast<float>(pointsPerFlowLine);
            const auto normalisedX = -0.08f + 1.16f * along;
            const auto baseX = width * normalisedX;
            const auto angle = normalisedX * juce::MathConstants<float>::twoPi
                             * (1.02f + 0.025f * static_cast<float>(line))
                             + seed;
            const auto primary = std::sin(angle
                                        + static_cast<float>(timeSeconds_ * 0.072));
            const auto secondary = std::sin(angle * 2.17f
                                          - static_cast<float>(timeSeconds_ * 0.041)
                                          + seed * 0.61f);

            const auto defaultAmount = unit * (3.0f + 8.0f * evolutionDepth);
            const auto defaultX = baseX + unit * 1.8f * secondary;
            const auto defaultY = baseY + defaultAmount
                * (0.78f * primary + 0.22f * secondary);

            const auto fromCentre = lane - 0.5f;
            const auto bloomBreath = std::sin(static_cast<float>(timeSeconds_ * 0.068)
                                            + normalisedX * 2.4f + seed * 0.17f);
            const auto bloomAmount = unit * (4.0f + 11.0f * evolutionDepth);
            const auto bloomX = baseX + unit * 2.2f * primary;
            const auto bloomY = baseY
                + bloomAmount * (0.58f * primary + 0.22f * secondary)
                + fromCentre * height * 0.075f * evolutionDepth * bloomBreath;

            const auto driftAmount = unit * (5.0f + 15.0f * evolutionDepth);
            const auto driftX = baseX + unit * (3.0f + 6.0f * evolutionDepth)
                * std::sin(seed + static_cast<float>(timeSeconds_ * 0.083));
            const auto driftY = baseY + driftAmount
                * std::sin(angle * 0.84f
                         + static_cast<float>(timeSeconds_ * 0.096)
                         + seed * 0.24f);

            const auto veilAmount = unit * (4.0f + 10.0f * evolutionDepth);
            const auto veilX = baseX + unit * 2.6f * secondary;
            const auto veilY = baseY + veilAmount
                * (0.52f * primary + 0.48f * secondary);

            const auto x = characterBlend_[0] * defaultX
                         + characterBlend_[1] * bloomX
                         + characterBlend_[2] * driftX
                         + characterBlend_[3] * veilX;
            const auto y = characterBlend_[0] * defaultY
                         + characterBlend_[1] * bloomY
                         + characterBlend_[2] * driftY
                         + characterBlend_[3] * veilY;
            if (point == 0)
                path.startNewSubPath(x, y);
            else
                path.lineTo(x, y);
        }

        const auto edgeDistance = std::abs(lane - 0.5f) * 2.0f;
        const auto alpha = 0.054f
                         * (1.0f - 0.12f * edgeDistance)
                         * (1.0f - 0.10f * veilBlend);
        graphics.setColour(flowColour.withAlpha(alpha * 0.18f));
        graphics.strokePath(path,
                            juce::PathStrokeType(5.4f * unit,
                                                 juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
        graphics.setColour(flowColour.withAlpha(alpha));
        graphics.strokePath(path,
                            juce::PathStrokeType(0.58f * unit,
                                                 juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
    }

    const auto centre = juce::Point<float> {
        width * static_cast<float>(0.50 + 0.035 * std::sin(period67 + 0.4)),
        height * static_cast<float>(0.48 + 0.028 * std::cos(period89 + 1.2))
    };
    const auto lineColour = juce::Colour::fromRGB(48, 88, 92)
                                .interpolatedWith(accent, 0.60f);

    for (auto contour = 0; contour < contourCount; ++contour)
    {
        const auto spread = static_cast<float>(contour)
                          / static_cast<float>(contourCount - 1);
        const auto radiusX = width * (0.085f + 0.585f * spread);
        const auto radiusY = height * (0.052f + 0.435f * spread);
        juce::Path path;
        for (auto point = 0; point <= pointsPerContour; ++point)
        {
            const auto angle = juce::MathConstants<float>::twoPi
                             * static_cast<float>(point) / pointsPerContour;
            const auto cosine = std::cos(angle);
            const auto sine = std::sin(angle);
            const auto baseX = centre.x + cosine * radiusX;
            const auto baseY = centre.y + sine * radiusY;
            const auto slow = std::sin(angle * 3.0f
                                     + static_cast<float>(timeSeconds_ * 0.072)
                                     + spread * 2.2f);
            const auto fine = std::sin(angle * 5.0f
                                     - static_cast<float>(timeSeconds_ * 0.047)
                                     + spread * 4.1f);

            const auto defaultAmount = unit * (1.2f + 5.2f * spread) * evolutionDepth;
            const auto defaultX = baseX + defaultAmount * slow;
            const auto defaultY = baseY + defaultAmount * 0.42f * fine;

            const auto bloomPulse = std::sin(static_cast<float>(timeSeconds_ * 0.083)
                                           + spread * 5.3f);
            const auto bloomAmount = unit * (2.0f + 9.0f * spread) * evolutionDepth;
            const auto bloomX = baseX + cosine * bloomAmount * bloomPulse
                              + slow * bloomAmount * 0.24f;
            const auto bloomY = baseY + sine * bloomAmount * bloomPulse * 0.70f
                              - (0.5f + 0.5f * sine) * bloomAmount * 0.62f;

            const auto driftAmount = unit * (2.5f + 11.5f * spread) * evolutionDepth;
            const auto driftX = baseX + driftAmount
                * std::sin(angle * 2.0f + static_cast<float>(timeSeconds_ * 0.094));
            const auto driftY = baseY + driftAmount * 0.36f
                * std::sin(angle * 3.0f - static_cast<float>(timeSeconds_ * 0.071));

            const auto veilAmount = unit * (1.8f + 8.5f * spread) * evolutionDepth;
            const auto veilX = baseX + veilAmount * 0.52f * fine;
            const auto veilY = centre.y
                             + (baseY - centre.y) * (0.93f - 0.055f * evolution_)
                             + veilAmount * slow;

            const auto x = characterBlend_[0] * defaultX
                         + characterBlend_[1] * bloomX
                         + characterBlend_[2] * driftX
                         + characterBlend_[3] * veilX;
            const auto y = characterBlend_[0] * defaultY
                         + characterBlend_[1] * bloomY
                         + characterBlend_[2] * driftY
                         + characterBlend_[3] * veilY;
            if (point == 0)
                path.startNewSubPath(x, y);
            else
                path.lineTo(x, y);
        }
        path.closeSubPath();

        const auto alpha = 0.056f
                         * (1.0f - 0.28f * spread)
                         * (1.0f - 0.12f * veilBlend);
        graphics.setColour(lineColour.withAlpha(alpha * 0.24f));
        graphics.strokePath(path,
                            juce::PathStrokeType(2.5f * unit,
                                                 juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
        graphics.setColour(lineColour.withAlpha(alpha));
        graphics.strokePath(path,
                            juce::PathStrokeType(0.68f * unit,
                                                 juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
    }

}

void DeepCurrentRenderer::paint(juce::Graphics& graphics,
                                juce::Rectangle<float> bounds) const
{
    if (!overlay_.isValid() || bounds.isEmpty())
        return;

    juce::Graphics::ScopedSaveState state(graphics);
    graphics.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
    graphics.drawImage(overlay_, bounds);
}

bool DeepCurrentRenderer::hasFrame() const noexcept
{
    return overlay_.isValid();
}

double DeepCurrentRenderer::getTimeSeconds() const noexcept
{
    return timeSeconds_;
}

float DeepCurrentRenderer::getEvolution() const noexcept
{
    return evolution_;
}

int DeepCurrentRenderer::getFrameWidth() const noexcept
{
    return overlay_.getWidth();
}

int DeepCurrentRenderer::getFrameHeight() const noexcept
{
    return overlay_.getHeight();
}

bool DeepCurrentRenderer::needsHighRefresh(int characterIndex,
                                           float evolution,
                                           bool frozen) const noexcept
{
    const auto safeCharacter = juce::jlimit(0, characterCount - 1, characterIndex);
    const auto safeEvolution = std::isfinite(evolution)
        ? juce::jlimit(0.0f, 1.0f, evolution)
        : evolution_;
    const auto evolutionMoving = std::abs(evolution_ - safeEvolution) > 5.0e-4f;
    const auto characterMoving
        = characterBlend_[static_cast<std::size_t>(safeCharacter)] < 0.998f;
    const auto motionMoving = frozen ? motion_ > 0.0f : motion_ < 0.998f;
    return evolutionMoving || characterMoving || motionMoving;
}

const std::array<float, DeepCurrentRenderer::characterCount>&
DeepCurrentRenderer::getCharacterBlend() const noexcept
{
    return characterBlend_;
}
} // namespace amanita::ui
