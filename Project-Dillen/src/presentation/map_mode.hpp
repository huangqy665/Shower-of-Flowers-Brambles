#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "algorithm_program.hpp"
#include "frozen_runtime_catalog.hpp"
#include "map_entity_index.hpp"
#include "presentation_asset.hpp"
#include "presentation_view.hpp"

namespace dillen::presentation {

// Map modes, declared by content rather than written in C++.
//
// A map mode is one rule for turning world state into a colour per province.
// Two of those were hardcoded here: ProvinceProjection's id hash and
// PoliticalMapProjection's ownership walk. The second one is the clearer case
// -- it is a map mode written as a class, and every further mode written the
// same way would be more business inside the generic presentation layer.
//
// So a mode is data. It names a READ PATH -- the Kernel's own
// AlgorithmReadPathDefinition, rooted at the province being coloured -- and a
// MAPPING from what that path reads to a colour. The read path is not
// reimplemented here: it is lowered by kernel/runtime_compiler.cpp and
// evaluated by runtime/read_path.cpp, the same code an algorithm's reads go
// through.
//
// The renderer needs no part in this. It already draws province index through
// a palette; switching mode is uploading a different palette.

enum class MapModeMappingKind
{
    // The value read IS the colour, packed 0x00RRGGBB. What a Package uses
    // when the world already carries colours -- a country's own, say.
    Value,
    // Discrete value to colour. Terrain kinds, ownership states, a flag.
    Lookup,
    // A numeric range to a gradient between two colours. Supply, victory
    // points, anything measured rather than named.
    Ramp
};

struct MapModeLookupEntry
{
    std::int64_t value = 0;
    std::uint32_t colour = 0;
};

struct CompiledMapMode
{
    // What content calls it, and what a control binds to.
    std::string id;
    std::string label;

    kernel::CompiledAlgorithmReadPath path;
    MapModeMappingKind mapping = MapModeMappingKind::Value;
    std::vector<MapModeLookupEntry> lookup;
    std::int64_t rampLow = 0;
    std::int64_t rampHigh = 0;
    std::uint32_t rampLowColour = 0;
    std::uint32_t rampHighColour = 0;

    // What a province gets when the path reads nothing -- an unowned region
    // in a political mode, a Component the Entity does not carry. Every mode
    // must declare it: without one, "no answer" and "the answer is zero" would
    // be drawn the same and no viewer could tell them apart.
    std::uint32_t absent = 0;
};

enum class MapModeStatus
{
    Ok,
    CatalogNotFrozen,
    AssetInvalid,
    ModeInvalid,
    PathUnresolved,
    NoModes,
    ModeOutOfRange,
    ViewNotBound,
    MapIndexNotBound
};

class MapModeSet
{
public:
    // Compile every declared mode against the Ruleset. A mode naming a
    // Component, field or Relation the Ruleset does not have is refused here,
    // at load, rather than drawing the wrong thing later.
    MapModeStatus Bind(
        const kernel::FrozenRuntimeCatalog& catalog,
        const kernel::PresentationAsset& asset,
        std::string& message
    );

    // Build the palette for one mode. Index 0 is "no province" and is left
    // transparent; 1..map.Count() are the provinces.
    MapModeStatus Refresh(
        const PresentationView& view,
        const MapEntityIndex& map,
        std::size_t mode
    );

    std::size_t Count() const noexcept
    {
        return modes_.size();
    }

    const CompiledMapMode& Mode(std::size_t index) const
    {
        return modes_[index];
    }

    // The mode's position, or Count() when this set does not declare it.
    std::size_t Find(const std::string& id) const noexcept;

    const std::vector<std::uint32_t>& Palette() const noexcept
    {
        return palette_;
    }

    // The colour for ground the corpus does not cover at all -- the poles a
    // latitude-band raster never reaches. Declared on the set rather than per
    // mode: it is open ocean in every mode, because the Arctic and the
    // Southern Ocean are. Absent when the asset does not name it, in which
    // case the caller keeps whatever default it had.
    bool HasPolarColour() const noexcept
    {
        return hasPolarColour_;
    }

    // 0x00RRGGBB, as content wrote it -- NOT the swizzled form Palette()
    // carries. Meaningful only when HasPolarColour().
    std::uint32_t PolarColour() const noexcept
    {
        return polarColour_;
    }

    // How many provinces the last Refresh could not read a value for. Not an
    // error -- an unowned province in a political mode is ordinary -- but a
    // number worth being able to see, because "every province is absent" and
    // "the mode works" look identical on screen when the absent colour is
    // plausible.
    std::uint32_t Absent() const noexcept
    {
        return absent_;
    }

private:
    std::vector<CompiledMapMode> modes_;
    std::vector<std::uint32_t> palette_;
    std::uint32_t absent_ = 0;
    std::uint32_t polarColour_ = 0;
    bool hasPolarColour_ = false;
    bool bound_ = false;
};

}
