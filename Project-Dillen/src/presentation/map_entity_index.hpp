#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "frozen_runtime_catalog.hpp"
#include "presentation_asset.hpp"
#include "presentation_view.hpp"

namespace dillen::presentation {

// Which Entity a picked pixel is.
//
// The renderer's id attachment holds a dense raster index, because that is what
// fits in sixteen bits and what the palette is addressed by. An index is not an
// identity: it is a position in a picture. Turning it into an Entity used to be
//
//     StableEntityId(namePrefix + std::to_string(index))
//
// which is not a lookup at all -- it is a rule about how the content happens to
// be spelled, and it stops being true the moment anyone renames a Package,
// reorders a spawn table or generates the same map from a different corpus. The
// world would keep loading, ticking and saving, and every click would land on
// the wrong province.
//
// This is the same objection MapCommandTranslator::Resolve already answers for
// mechanism instances -- it reads role bindings rather than assuming instance
// ids run parallel to province numbers -- carried to the other half.
//
// So the correspondence is assembled from two pieces of DATA, in two halves
// that belong where they are:
//
//   index -> source_id   ships with the raster, because it describes the
//                        raster's encoding. A Presentation asset with its own
//                        payload and digest.
//   source_id -> Entity  read out of the world, by walking the owners of the
//                        component the Package names and reading the field the
//                        Package names.
//
// Neither half is a convention, and a Package that renames everything still
// resolves.

struct MapEntityIndexSpec
{
    // All four come from the id-table asset's own properties, so this layer
    // still assumes nothing about any particular world.
    std::string entityTypeName;
    std::string componentName;
    std::uint32_t componentVersion = 1;
    std::string sourceIdFieldName;
};

enum class MapEntityIndexStatus
{
    Ok,
    NotBound,
    CatalogNotFrozen,
    AssetInvalid,
    PayloadMissing,
    // The payload does not match the digest the Package declared. Checked
    // before it is decoded, like every other payload here.
    DigestMismatch,
    ComponentMissing,
    ViewNotBound
};

class MapEntityIndex
{
public:
    // Reads the id table and resolves the component field it names. `asset`
    // must be the `map_province_ids` declaration.
    MapEntityIndexStatus Bind(
        const kernel::FrozenRuntimeCatalog& catalog,
        const kernel::PresentationAsset& asset,
        std::string& message
    );

    // Walks the world and builds both directions. Cheap enough to redo when
    // entities are created or destroyed; nothing here caches across a snapshot
    // it has not seen.
    MapEntityIndexStatus Resolve(const PresentationView& view);

    bool IsBound() const noexcept { return bound_; }
    std::uint32_t Count() const noexcept { return count_; }
    // How many raster indices found an Entity. A map whose corpus and world
    // have drifted apart shows up here as a number below Count().
    std::uint32_t Resolved() const noexcept { return resolved_; }

    kernel::EntityId EntityFor(std::uint32_t rasterIndex) const noexcept;
    // 0 when the Entity is not on the map.
    std::uint32_t IndexFor(kernel::EntityId entity) const;

    std::uint32_t SourceIdFor(std::uint32_t rasterIndex) const noexcept;

    // What the id-table asset declared. A caller that needs to read other
    // components off the same entities -- a projection filling a palette,
    // say -- takes the type and the field from here rather than repeating the
    // Package's names in C++.
    const MapEntityIndexSpec& Spec() const noexcept { return spec_; }

private:
    bool bound_ = false;
    std::uint32_t count_ = 0;
    std::uint32_t resolved_ = 0;
    kernel::ComponentTypeId component_;
    kernel::ComponentFieldSlotId sourceIdSlot_;
    kernel::EntityTypeId entityType_;
    MapEntityIndexSpec spec_;
    // Index 0 is reserved for "no province", exactly as the raster is.
    std::vector<std::uint32_t> sourceIdByIndex_;
    std::vector<kernel::EntityId> entityByIndex_;
    std::unordered_map<std::uint64_t, std::uint32_t> indexByEntity_;
};

}
