#include "client_state.hpp"

#include <cstring>

namespace dillen::presentation {

namespace {

void Mix(std::uint64_t& state, std::uint64_t value) noexcept
{
    state ^= value + 0x9E3779B97F4A7C15ULL + (state << 6) + (state >> 2);
}

std::uint64_t Bits(double value) noexcept
{
    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "double is 64-bit");
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

}

std::uint64_t ClientState::Digest() const noexcept
{
    std::uint64_t state = 0xC0FFEE1234567890ULL;
    Mix(state, selected.value);
    Mix(state, hovered.value);
    Mix(state, Bits(camera.lookAtU));
    Mix(state, Bits(camera.lookAtV));
    Mix(state, Bits(camera.distance));
    Mix(state, Bits(camera.bend));
    Mix(state, viewportWidth);
    Mix(state, viewportHeight);
    return state;
}

bool operator==(const ClientState& first, const ClientState& second) noexcept
{
    return first.selected == second.selected
        && first.hovered == second.hovered
        && first.camera.lookAtU == second.camera.lookAtU
        && first.camera.lookAtV == second.camera.lookAtV
        && first.camera.distance == second.camera.distance
        && first.camera.bend == second.camera.bend
        && first.viewportWidth == second.viewportWidth
        && first.viewportHeight == second.viewportHeight;
}

bool operator!=(const ClientState& first, const ClientState& second) noexcept
{
    return !(first == second);
}

}
