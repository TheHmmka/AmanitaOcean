#pragma once

#include <juce_graphics/juce_graphics.h>

#include <array>
#include <cstdint>

namespace amanita::ui
{
[[nodiscard]] inline juce::Colour characterAccent(int characterIndex) noexcept
{
    constexpr std::array<std::uint32_t, 5> colours {
        0xff81bfc7, 0xffc89c83, 0xff829de0, 0xffb3a6c4, 0xff74c6a8
    };
    const auto safeIndex = static_cast<std::size_t>(juce::jlimit(0, 4, characterIndex));
    return juce::Colour(colours[safeIndex]);
}
} // namespace amanita::ui
