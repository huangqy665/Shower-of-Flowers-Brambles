#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "frozen_runtime_catalog.hpp"
#include "presentation_view.hpp"

namespace dillen::presentation {

// Turns a published snapshot into a flat, index-addressed table of province
// attributes.
//
// This is the whole read side of the map. A renderer draws one quad and one
// tessellated grid; what it needs per frame is not the world but a small table
// it can upload: row `i` holds whatever the map mode wants to know about the
// province whose dense index is `i`. The id raster stores that same index, so
// the shader's lookup is a texel fetch and costs nothing per province.
//
// Three properties make it safe to sit between an authoritative tick and a
// frame:
//
//   * It reads through PresentationView, which owns a
//     shared_ptr<const WorldQuerySnapshot>. There is no non-const path to the
//     stores, and the snapshot it read stays alive for as long as the frame
//     needs it even though the Kernel Runtime has ticked past.
//   * Refresh() is a pure function of the snapshot. The same snapshot always
//     produces the same bytes -- the probe asserts it, because a projection
//     that drifted would put presentation state into a picture that is
//     supposed to be derived state only.
//   * Every value is an integer. Decimals are carried at the fixed-point
//     INTERNAL scale (10^4) rather than as doubles, so nothing between the
//     authoritative value and the texture upload can round differently on a
//     different machine.
//
// Row 0 is reserved and always zero. The raster paints 0 where there is no
// province, so a renderer can index this table directly with whatever it read
// out of the id texture and never branch.

struct ProvinceProjectionColumn
{
    std::string componentName;
    std::uint32_t schemaVersion = 1;
    std::string fieldName;
};

struct ProvinceProjectionSpec
{
    // The naming convention is a Content decision, so it arrives as data. A
    // presentation artifact type will carry it once there is one; until then
    // the caller states it, and nothing in this layer assumes a particular
    // world.
    std::string entityTypeName;
    std::string namePrefix;
    std::uint32_t count = 0;
    std::vector<ProvinceProjectionColumn> columns;
};

enum class ProvinceProjectionStatus
{
    Ok,
    NotBound,
    CatalogNotFrozen,
    SpecInvalid,
    ComponentFieldMissing,
    ViewNotBound,
    ValueNotNumeric
};

class ProvinceProjection
{
public:
    // Resolves names to slots once, against the frozen catalog. Everything
    // per-publication after this is index arithmetic and store lookups; no
    // string ever appears on the refresh path.
    ProvinceProjectionStatus Bind(
        const kernel::FrozenRuntimeCatalog& catalog,
        const ProvinceProjectionSpec& spec,
        std::string& message
    );

    bool IsBound() const noexcept;

    // Rebuilds the table from `view`. Returns Ok even when rows are missing --
    // MissingRows() reports how many, because a world that lost provinces
    // should be visible rather than silently drawn as zeroes.
    ProvinceProjectionStatus Refresh(const PresentationView& view);

    std::uint32_t Count() const noexcept { return count_; }
    std::uint32_t Columns() const noexcept
    {
        return static_cast<std::uint32_t>(columns_.size());
    }
    std::uint32_t MissingRows() const noexcept { return missingRows_; }
    runtime::WorldQueryStamp Stamp() const noexcept { return stamp_; }

    // Row-major, (Count() + 1) rows by Columns() columns. Row 0 is reserved.
    const std::vector<std::int64_t>& Values() const noexcept
    {
        return values_;
    }
    std::int64_t Value(
        std::uint32_t index,
        std::uint32_t column
    ) const noexcept;

private:
    struct BoundColumn
    {
        kernel::ComponentTypeId component;
        kernel::ComponentFieldSlotId field;
    };

    bool bound_ = false;
    std::uint32_t count_ = 0;
    std::uint32_t missingRows_ = 0;
    std::vector<BoundColumn> columns_;
    // index -> EntityId, resolved once at Bind. Row 0 is the reserved
    // "no province" slot and holds an empty id.
    std::vector<kernel::EntityId> entities_;
    std::vector<std::int64_t> values_;
    runtime::WorldQueryStamp stamp_;
};

}
