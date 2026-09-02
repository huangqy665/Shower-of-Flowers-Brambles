#include "province_projection.hpp"

#include <variant>

#include "fixed_point.hpp"
#include "mechanism_ids.hpp"

namespace dillen::presentation {

std::uint32_t PaletteSideFor(std::uint32_t provinceCount) noexcept
{
    std::uint32_t side = 1;
    while (static_cast<std::uint64_t>(side) * side
        < static_cast<std::uint64_t>(provinceCount) + 1u)
    {
        side *= 2;
    }
    return side;
}

ProvinceProjectionStatus ProvinceProjection::Bind(
    const kernel::FrozenRuntimeCatalog& catalog,
    const ProvinceProjectionSpec& spec,
    const MapEntityIndex& entities,
    std::string& message
)
{
    bound_ = false;
    columns_.clear();
    entities_.clear();
    values_.clear();
    missingRows_ = 0;
    stamp_ = {};

    if (!catalog.IsFrozen())
    {
        message = "the Runtime Catalog is not frozen";
        return ProvinceProjectionStatus::CatalogNotFrozen;
    }
    if (spec.count == 0
        || spec.columns.empty()
        || spec.entityTypeName.empty()
        )
    {
        message = "the projection spec names no world";
        return ProvinceProjectionStatus::SpecInvalid;
    }

    columns_.reserve(spec.columns.size());
    for (const ProvinceProjectionColumn& column : spec.columns)
    {
        const kernel::ComponentTypeId component =
            kernel::StableComponentTypeId(column.componentName);
        const auto slot = catalog.ResolveComponentFieldSlot(
            component,
            column.schemaVersion,
            column.fieldName
        );
        if (!slot)
        {
            message = "component field " + column.componentName + "."
                + column.fieldName + " is not in this Ruleset";
            return ProvinceProjectionStatus::ComponentFieldMissing;
        }
        columns_.push_back({component, *slot});
    }

    // The row -> Entity table, taken from MapEntityIndex rather than
    // rebuilt from a naming rule.
    //
    // This used to be `StableEntityId(namePrefix + std::to_string(index))`,
    // which is not a lookup: it is an assumption about how the content is
    // spelled, and a Package that renamed or reordered anything would have
    // produced a projection quietly reading the wrong provinces. The index is
    // still the row number the palette is addressed by; it is no longer
    // pretending to be an identity.
    if (!entities.IsBound())
    {
        message = "the map entity index is not bound";
        return ProvinceProjectionStatus::SpecInvalid;
    }
    entities_.assign(spec.count + 1, kernel::EntityId{});
    for (std::uint32_t index = 1; index <= spec.count; ++index)
    {
        entities_[index] = entities.EntityFor(index);
    }

    count_ = spec.count;
    values_.assign(
        static_cast<std::size_t>(count_ + 1) * columns_.size(),
        std::int64_t{0}
    );
    bound_ = true;
    return ProvinceProjectionStatus::Ok;
}

bool ProvinceProjection::IsBound() const noexcept
{
    return bound_;
}

ProvinceProjectionStatus ProvinceProjection::Refresh(
    const PresentationView& view
)
{
    if (!bound_)
    {
        return ProvinceProjectionStatus::NotBound;
    }
    if (!view.IsBound())
    {
        return ProvinceProjectionStatus::ViewNotBound;
    }

    const runtime::ComponentQuerySnapshot& components =
        view.World().Components();
    const std::size_t columnCount = columns_.size();
    missingRows_ = 0;

    // Row 0 stays zero: the raster paints 0 where there is no province, so a
    // renderer indexes this table with whatever the id texture gave it and
    // never has to branch on "is this a province".
    for (std::size_t cell = 0; cell < columnCount; ++cell)
    {
        values_[cell] = 0;
    }

    for (std::uint32_t index = 1; index <= count_; ++index)
    {
        std::int64_t* row = values_.data()
            + static_cast<std::size_t>(index) * columnCount;
        bool present = true;
        for (std::size_t column = 0; column < columnCount; ++column)
        {
            const kernel::MechanismValue* value = components.FindField(
                entities_[index],
                columns_[column].component,
                columns_[column].field
            );
            if (value == nullptr)
            {
                // A province the world does not have. Zero the row and count
                // it; drawing a hole is better than drawing a lie, and the
                // count is what makes the hole visible.
                row[column] = 0;
                present = false;
                continue;
            }
            if (const auto* integer =
                std::get_if<std::int64_t>(&value->data))
            {
                row[column] = *integer;
            }
            else if (const auto* decimal = std::get_if<double>(&value->data))
            {
                // Carried at the internal fixed-point scale rather than as a
                // double. Everything downstream -- a palette, a shader
                // uniform -- can divide when it wants a float; nothing between
                // here and there can round differently on another machine.
                const kernel::FixedPointValue scaled =
                    kernel::DecimalToInternal(*decimal);
                if (!scaled)
                {
                    return ProvinceProjectionStatus::ValueNotNumeric;
                }
                row[column] = scaled.scaled;
            }
            else if (const auto* flag = std::get_if<bool>(&value->data))
            {
                row[column] = *flag ? 1 : 0;
            }
            else
            {
                // Strings, references, lists. A map mode that wants one of
                // those needs a column type this table does not have yet, and
                // silently projecting a zero would hide that.
                return ProvinceProjectionStatus::ValueNotNumeric;
            }
        }
        if (!present)
        {
            ++missingRows_;
        }
    }

    stamp_ = view.Stamp();
    return ProvinceProjectionStatus::Ok;
}

std::int64_t ProvinceProjection::Value(
    std::uint32_t index,
    std::uint32_t column
) const noexcept
{
    if (!bound_ || index > count_ || column >= columns_.size())
    {
        return 0;
    }
    return values_[
        static_cast<std::size_t>(index) * columns_.size() + column
    ];
}

}
