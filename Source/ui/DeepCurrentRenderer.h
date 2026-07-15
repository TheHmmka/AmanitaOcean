#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

namespace amanita::ui
{
class DeepCurrentRenderer final
{
public:
    static constexpr int characterCount = 4;

    void reset(int characterIndex,
               float evolution,
               bool frozen,
               double timeSeconds = 0.0) noexcept;
    void setSize(int logicalWidth, int logicalHeight);
    [[nodiscard]] bool advance(double elapsedSeconds,
                               int characterIndex,
                               float evolution,
                               bool frozen) noexcept;
    void render(juce::Colour accent);
    void paint(juce::Graphics& graphics, juce::Rectangle<float> bounds) const;

    [[nodiscard]] bool hasFrame() const noexcept;
    [[nodiscard]] double getTimeSeconds() const noexcept;
    [[nodiscard]] float getEvolution() const noexcept;
    [[nodiscard]] int getFrameWidth() const noexcept;
    [[nodiscard]] int getFrameHeight() const noexcept;
    [[nodiscard]] bool needsHighRefresh(int characterIndex,
                                        float evolution,
                                        bool frozen) const noexcept;
    [[nodiscard]] const std::array<float, characterCount>&
    getCharacterBlend() const noexcept;

private:
    juce::Image overlay_;
    std::array<float, characterCount> characterBlend_ { 1.0f, 0.0f, 0.0f, 0.0f };
    double timeSeconds_ = 0.0;
    float evolution_ = 0.0f;
    float motion_ = 1.0f;
};
} // namespace amanita::ui
