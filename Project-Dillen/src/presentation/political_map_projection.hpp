#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "frozen_runtime_catalog.hpp"
#include "map_entity_index.hpp"
#include "presentation_asset.hpp"
#include "presentation_view.hpp"

namespace dillen::presentation {

enum class PoliticalMapProjectionStatus
{
    Ok,

    CatalogNotFrozen,
    AssetInvalid,
    RelationMissing,
    CountryMissing,

    ViewNotBound,
    MapIndexNotBound
};

class PoliticalMapProjection
{
public:
    PoliticalMapProjectionStatus Bind(
        const kernel::FrozenRuntimeCatalog& catalog,
        const kernel::PresentationAsset& asset,
        std::string& message
    );

    PoliticalMapProjectionStatus Refresh(
        const PresentationView& view,
        const MapEntityIndex& map
    );

    const std::vector<std::uint32_t>&
    Palette() const noexcept
    {
        return palette_;
    }

    std::uint32_t Unowned() const noexcept
    {
        return unowned_;
    }

    std::uint32_t AmbiguousOwners() const noexcept
    {
        return ambiguousOwners_;
    }

    std::uint32_t MissingColours() const noexcept
    {
        return missingColours_;
    }

    // Water, as opposed to land nobody has claimed. Zero when the palette
    // asset did not say how to tell them apart.
    std::uint32_t Sea() const noexcept
    {
        return sea_;
    }

private:
    bool bound_ = false;

    kernel::RelationTypeId
        ownershipRelation_;

    std::uint32_t
        ownershipRelationVersion_ = 1;

    kernel::EntityTypeId
        countryEntityType_;

    std::unordered_map<
        std::uint64_t,
        std::uint32_t
    > colourByCountry_;

    std::vector<std::uint32_t>
        palette_;

    // Optional. Absent when the Package declares no way to tell water from
    // unclaimed land, in which case everything unowned is drawn alike --
    // which is what happened before the corpus carried the distinction.
    kernel::ComponentTypeId seaComponent_;
    std::uint32_t seaComponentVersion_ = 1;
    std::optional<kernel::ComponentFieldSlotId> seaField_;
    std::uint32_t seaColour_ = 0;

    std::uint32_t sea_ = 0;
    std::uint32_t unowned_ = 0;
    std::uint32_t ambiguousOwners_ = 0;
    std::uint32_t missingColours_ = 0;
};

}