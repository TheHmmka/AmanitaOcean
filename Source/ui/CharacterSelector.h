#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>

namespace amanita::ui
{
// Drop-down that owns the choice of Character and writes it to the host
// parameter. The arrow keys step through the Characters and wrap at either
// end; Return or Space opens the list. Only the bare keys do: with a modifier
// held they are passed on, and while the list is open the keys are the list's.
class CharacterSelector final : public juce::ComboBox
{
public:
    static constexpr int characterCount = 7;

    explicit CharacterSelector(juce::AudioProcessorValueTreeState& state);

    bool keyPressed(const juce::KeyPress& key) override;

private:
    void moveSelection(int offset);

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CharacterSelector)
};

// Text block that introduces the selected Character: the name, a subtitle, a
// hairline and a paragraph. It is laid out at its design size and scaled to
// its bounds, so the lines break alike at every editor size. The text stands
// centred on a line its owner sets, measured from the top of the name's
// capitals to the baseline of the paragraph's last line.
class CharacterDescription final : public juce::Component
{
public:
    struct Text
    {
        const char* name;
        const char* subtitle;
        const char* paragraph;
    };

    static constexpr int designWidth = 248;
    static constexpr int designHeight = 160;
    static constexpr int maximumParagraphLines = 6;
    // A last line shorter than this share of the paragraph's width is a widow.
    static constexpr float widowShare = 0.25f;

    CharacterDescription();

    void paint(juce::Graphics& graphics) override;

    void setCharacter(int characterIndex);
    // The line the middle of the text lies on, in this component's
    // coordinates from its top. It need not fall on a whole pixel.
    void setTextCentre(float centreY);

    [[nodiscard]] static const Text& text(int characterIndex) noexcept;
    // A paragraph broken into lines of a width, as the block sets it. Where
    // that leaves a widow, the lines are narrowed just enough to bring words
    // down to it without adding a line.
    [[nodiscard]] static juce::TextLayout paragraphLayout(const char* text, float width);
    // True when every line of the shown Character lies inside the block
    // unshortened and the paragraph keeps to its lines.
    [[nodiscard]] bool textFits() const;

private:
    // Where the parts of the text lie at the design size, from the middle of
    // the text: three baselines, the top of the hairline and the top of the
    // paragraph's layout.
    struct Rows
    {
        float nameBaseline = 0.0f;
        float subtitleBaseline = 0.0f;
        float rule = 0.0f;
        float paragraph = 0.0f;
        float lastBaseline = 0.0f;
    };

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;
    [[nodiscard]] float designScale() const noexcept;

    int characterIndex_ = 0;
    juce::TextLayout paragraph_;
    Rows rows_;
    float textCentre_ = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CharacterDescription)
};
} // namespace amanita::ui
