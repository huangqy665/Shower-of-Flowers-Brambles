#pragma once

#include <cstdint>
#include <string>

#include "map_view.hpp"
#include "mechanism_ids.hpp"

namespace dillen::presentation {

// What one viewer is looking at, and nothing else.
//
// Selection, hover, camera and viewport belong to the person in front of the
// screen. They are not part of the world: two players watching the same
// simulation disagree about all four and must still compute the same save, the
// same Fact Stream and the same Replay Checksum. That is the rule memo section
// 4.4.2 states, and until now nothing enforced it.
//
// The enforcement is not this type -- a struct nobody reads cannot fail. It is
// client_state_probe, which drives one world twice with the SAME intents and
// deliberately DIFFERENT client state, and requires the saves to match byte for
// byte. Digest() exists so that probe can also prove the two runs really did
// differ; a boundary test that passes because nothing moved on either side is
// worse than none.
//
// The failure this guards against is not a struct leaking into a save. It is
// the convenience a UI naturally reaches for: "act on the selected province",
// wired by letting the command path read the selection. The moment it does,
// what the world becomes depends on where someone was pointing, and two
// clients watching the same game diverge.

struct ClientState
{
    // The Entity under the cursor and the Entity chosen, not raster indices.
    //
    // A dense index is a position in a picture; it is stable only as long as
    // nobody regenerates the map. Holding the Entity means a selection
    // survives anything that does not destroy the province itself.
    kernel::EntityId selected;
    kernel::EntityId hovered;
    MapCamera camera;
    std::uint32_t viewportWidth = 1280;
    std::uint32_t viewportHeight = 720;

    // A stable hash over everything above.
    //
    // Only ever compared against another ClientState. It is deliberately NOT
    // derived from anything authoritative and must never be mixed into a save,
    // a fingerprint or a checksum -- it exists to let a test say "these two
    // sessions genuinely saw different things".
    std::uint64_t Digest() const noexcept;
};

bool operator==(const ClientState& first, const ClientState& second) noexcept;
bool operator!=(const ClientState& first, const ClientState& second) noexcept;

}
