#include "political_map_projection.hpp"

#include <limits>

namespace dillen::presentation {

namespace {

const kernel::PresentationAssetNode*
FindChild(
    const kernel::PresentationAssetNode& parent,
    const std::string& key
)
{
    for (const kernel::PresentationAssetNode& child
        : parent.children)
    {
        if (child.key == key)
        {
            return &child;
        }
    }

    return nullptr;
}

bool ReadScalarChild(
    const kernel::PresentationAssetNode& parent,
    const std::string& key,
    std::string& output
)
{
    const kernel::PresentationAssetNode* child =
        FindChild(
            parent,
            key
        );

    if (child == nullptr
        || child->block
        || child->value.empty())
    {
        return false;
    }

    output = child->value;

    return true;
}

bool ReadUnsignedText(
    const std::string& text,
    std::uint32_t maximum,
    std::uint32_t& output
)
{
    if (text.empty())
    {
        return false;
    }

    std::uint64_t value = 0;

    for (const char character : text)
    {
        if (character < '0'
            || character > '9')
        {
            return false;
        }

        value =
            value * 10
            + static_cast<std::uint64_t>(
                character - '0'
            );

        if (value > maximum)
        {
            return false;
        }
    }

    output =
        static_cast<std::uint32_t>(
            value
        );

    return true;
}

bool ReadUnsignedChild(
    const kernel::PresentationAssetNode& parent,
    const std::string& key,
    std::uint32_t maximum,
    std::uint32_t& output
)
{
    std::string text;

    return ReadScalarChild(
            parent,
            key,
            text)
        && ReadUnsignedText(
            text,
            maximum,
            output);
}

bool ReadProperty(
    const kernel::PresentationAsset& asset,
    const std::string& key,
    std::string& output
)
{
    const auto found =
        asset.properties.find(key);

    if (found == asset.properties.end()
        || found->second.empty())
    {
        return false;
    }

    output = found->second;

    return true;
}

std::uint32_t PackColour(
    std::uint32_t red,
    std::uint32_t green,
    std::uint32_t blue
)
{
    //
    // Same byte layout MapRenderer's current Province palette uses.
    //
    return 0xFF000000u
        | (blue << 16u)
        | (green << 8u)
        | red;
}

}

PoliticalMapProjectionStatus
PoliticalMapProjection::Bind(
    const kernel::FrozenRuntimeCatalog& catalog,
    const kernel::PresentationAsset& asset,
    std::string& message
)
{
    bound_ = false;

    colourByCountry_.clear();
    palette_.clear();

    if (!catalog.IsFrozen())
    {
        message =
            "the Runtime Catalog is not frozen";

        return
            PoliticalMapProjectionStatus::
                CatalogNotFrozen;
    }

    if (asset.kind != "country_palette")
    {
        message =
            "asset '"
            + asset.canonicalName
            + "' is a "
            + asset.kind;

        return
            PoliticalMapProjectionStatus::
                AssetInvalid;
    }

    std::string ownershipRelationName;
    std::string countryEntityTypeName;
    std::string versionText;

    if (!ReadProperty(
            asset,
            "ownership_relation",
            ownershipRelationName)
        || !ReadProperty(
            asset,
            "ownership_relation_version",
            versionText)
        || !ReadProperty(
            asset,
            "country_entity_type",
            countryEntityTypeName)
        || !ReadUnsignedText(
            versionText,
            std::numeric_limits<
                std::uint32_t
            >::max(),
            ownershipRelationVersion_)
        || ownershipRelationVersion_ == 0)
    {
        message =
            "country palette declaration "
            "is incomplete";

        return
            PoliticalMapProjectionStatus::
                AssetInvalid;
    }

    // Water is optional, and declared as three things: which Component
    // carries the flag, which field of it, and what colour to draw. Any of
    // them missing means the Package is not making the distinction, not that
    // the renderer should invent one.
    seaField_.reset();
    seaColour_ = 0;
    std::string seaComponentName;
    std::string seaFieldName;
    std::string seaColourText;
    if (ReadProperty(asset, "sea_component", seaComponentName)
        && ReadProperty(asset, "sea_field", seaFieldName)
        && ReadProperty(asset, "sea_colour", seaColourText))
    {
        std::uint32_t packed = 0;
        if (ReadUnsignedText(seaColourText, 0xFFFFFFu, packed))
        {
            seaComponent_ = kernel::StableComponentTypeId(seaComponentName);
            seaField_ = catalog.ResolveComponentFieldSlot(
                seaComponent_,
                seaComponentVersion_,
                seaFieldName
            );
            if (!seaField_)
            {
                message = "the palette names sea field '" + seaFieldName
                    + "' on '" + seaComponentName
                    + "', which the Ruleset does not have";
                return PoliticalMapProjectionStatus::AssetInvalid;
            }
            // 0x00RRGGBB in the declaration, 0xAABBGGRR in the palette the
            // renderer uploads.
            seaColour_ = 0xFF000000u
                | ((packed & 0xFFu) << 16)
                | (packed & 0xFF00u)
                | ((packed >> 16) & 0xFFu);
        }
    }

    ownershipRelation_ =
        kernel::StableRelationTypeId(
            ownershipRelationName
        );

    countryEntityType_ =
        kernel::StableEntityTypeId(
            countryEntityTypeName
        );

    //
    // Presentation does not trust a string in the asset. The selected
    // Ruleset must actually provide the Relation Schema.
    //
    if (catalog.FindRelationLayout(
            ownershipRelation_,
            ownershipRelationVersion_)
        == nullptr)
    {
        message =
            "ownership relation "
            + ownershipRelationName
            + " is not in this Ruleset";

        return
            PoliticalMapProjectionStatus::
                RelationMissing;
    }

    for (const kernel::PresentationAssetNode& node
        : asset.content)
    {
        if (node.key != "country"
            || !node.block)
        {
            message =
                "country palette content may contain "
                "only country blocks";

            return
                PoliticalMapProjectionStatus::
                    AssetInvalid;
        }

        std::string canonicalName;

        std::uint32_t red = 0;
        std::uint32_t green = 0;
        std::uint32_t blue = 0;

        if (!ReadScalarChild(
                node,
                "entity",
                canonicalName)
            || !ReadUnsignedChild(
                node,
                "red",
                255,
                red)
            || !ReadUnsignedChild(
                node,
                "green",
                255,
                green)
            || !ReadUnsignedChild(
                node,
                "blue",
                255,
                blue))
        {
            message =
                "country palette entry is incomplete";

            return
                PoliticalMapProjectionStatus::
                    AssetInvalid;
        }

        const kernel::EntityDefinitionId definition =
            kernel::StableEntityDefinitionId(
                countryEntityType_,
                canonicalName
            );

        const kernel::CompiledEntityDefinition* compiled =
            catalog.FindEntityDefinition(
                definition
            );

        if (compiled == nullptr
            || compiled->type
                != countryEntityType_)
        {
            message =
                "country palette references "
                "Entity Definition "
                + canonicalName
                + ", which is not in this Ruleset";

            return
                PoliticalMapProjectionStatus::
                    CountryMissing;
        }

        const kernel::EntityId entity =
            kernel::StableEntityId(
                definition
            );

        if (!colourByCountry_.emplace(
                entity.value,
                PackColour(
                    red,
                    green,
                    blue)
            ).second)
        {
            message =
                "country palette contains "
                "the same country twice";

            return
                PoliticalMapProjectionStatus::
                    AssetInvalid;
        }
    }

    bound_ = true;

    return
        PoliticalMapProjectionStatus::Ok;
}

PoliticalMapProjectionStatus
PoliticalMapProjection::Refresh(
    const PresentationView& view,
    const MapEntityIndex& map
)
{
    if (!bound_)
    {
        return
            PoliticalMapProjectionStatus::
                AssetInvalid;
    }

    if (!view.IsBound())
    {
        return
            PoliticalMapProjectionStatus::
                ViewNotBound;
    }

    if (!map.IsBound())
    {
        return
            PoliticalMapProjectionStatus::
                MapIndexNotBound;
    }

    palette_.assign(
        static_cast<std::size_t>(
            map.Count()
        ) + 1u,
        0u
    );

    unowned_ = 0;
    sea_ = 0;
    ambiguousOwners_ = 0;
    missingColours_ = 0;
    const runtime::ComponentQuerySnapshot& components =
        view.World().Components();

    // Water first: a sea zone has no owner, so without this it would fall
    // through to "unowned" and be drawn as unclaimed land.
    const auto paintSea = [&](std::uint32_t index,
                              kernel::EntityId province) -> bool
    {
        if (!seaField_ || !province)
        {
            return false;
        }
        const kernel::MechanismValue* value = components.FindField(
            province, seaComponent_, *seaField_);
        if (value == nullptr)
        {
            return false;
        }
        const auto* number = std::get_if<std::int64_t>(&value->data);
        if (number == nullptr || *number == 0)
        {
            return false;
        }
        palette_[index] = seaColour_;
        ++sea_;
        return true;
    };

    const runtime::RelationQuerySnapshot& relations =
        view.World().Relations();

    for (std::uint32_t index = 1;
         index <= map.Count();
         ++index)
    {
        //
        // Raster dense index -> actual Province Entity.
        //
        const kernel::EntityId province =
            map.EntityFor(index);

        if (!province)
        {
            ++unowned_;
            continue;
        }

        if (paintSea(index, province))
        {
            continue;
        }

        //
        // Province <- owns_region - Country
        //
        const std::vector<
            kernel::RelationId
        >& owners =
            relations.Incoming(
                ownershipRelation_,
                province
            );

        if (owners.empty())
        {
            ++unowned_;
            continue;
        }

        //
        // The first milestone expects exactly one owner. We diagnose ambiguity
        // here rather than silently choosing one.
        //
        // If real gameplay later proves this invariant belongs in Kernel,
        // Relation cardinality can then be added for a demonstrated reason.
        //
        if (owners.size() != 1)
        {
            ++ambiguousOwners_;
            continue;
        }

        const world::RelationRecord* ownership =
            relations.Find(
                owners.front()
            );

        if (ownership == nullptr)
        {
            ++unowned_;
            continue;
        }

        const auto colour =
            colourByCountry_.find(
                ownership->source.value
            );

        if (colour
            == colourByCountry_.end())
        {
            ++missingColours_;

            //
            // Presentation-only fallback. It has no simulation meaning.
            //
            palette_[index] =
                PackColour(
                    128,
                    128,
                    128
                );

            continue;
        }

        palette_[index] =
            colour->second;
    }

    return
        PoliticalMapProjectionStatus::Ok;
}

}