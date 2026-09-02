#include "map_entity_index.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <variant>

#include "package_content_digest.hpp"

namespace dillen::presentation {

namespace {

bool ReadProperty(
    const kernel::PresentationAsset& asset,
    const std::string& key,
    std::string& output
)
{
    const auto entry = asset.properties.find(key);
    if (entry == asset.properties.end() || entry->second.empty())
    {
        return false;
    }
    output = entry->second;
    return true;
}

bool ReadUnsigned(
    const kernel::PresentationAsset& asset,
    const std::string& key,
    std::uint32_t& output
)
{
    std::string text;
    if (!ReadProperty(asset, key, text))
    {
        return false;
    }
    std::uint64_t value = 0;
    for (const char character : text)
    {
        if (character < '0' || character > '9')
        {
            return false;
        }
        value = value * 10 + static_cast<std::uint64_t>(character - '0');
        if (value > 0xFFFFFFFFull)
        {
            return false;
        }
    }
    output = static_cast<std::uint32_t>(value);
    return true;
}

}

MapEntityIndexStatus MapEntityIndex::Bind(
    const kernel::FrozenRuntimeCatalog& catalog,
    const kernel::PresentationAsset& asset,
    std::string& message
)
{
    bound_ = false;
    count_ = 0;
    resolved_ = 0;
    sourceIdByIndex_.clear();
    entityByIndex_.clear();
    indexByEntity_.clear();

    if (!catalog.IsFrozen())
    {
        message = "the Runtime Catalog is not frozen";
        return MapEntityIndexStatus::CatalogNotFrozen;
    }
    if (asset.kind != "map_province_ids")
    {
        message = "asset '" + asset.canonicalName + "' is a " + asset.kind;
        return MapEntityIndexStatus::AssetInvalid;
    }

    MapEntityIndexSpec& spec = spec_;
    spec = MapEntityIndexSpec{};
    std::string format;
    if (!ReadProperty(asset, "format", format)
        || format != "source_id32"
        || !ReadUnsigned(asset, "count", count_)
        || !ReadProperty(asset, "entity_type", spec.entityTypeName)
        || !ReadProperty(asset, "component", spec.componentName)
        || !ReadUnsigned(asset, "component_version", spec.componentVersion)
        || !ReadProperty(asset, "source_id_field", spec.sourceIdFieldName)
        || count_ == 0)
    {
        message = "the id table declaration is incomplete";
        return MapEntityIndexStatus::AssetInvalid;
    }

    entityType_ = kernel::StableEntityTypeId(spec.entityTypeName);
    component_ = kernel::StableComponentTypeId(spec.componentName);
    const kernel::CompiledComponentLayout* layout =
        catalog.FindComponentLayout(component_, spec.componentVersion);
    if (layout == nullptr)
    {
        message = "component " + spec.componentName + " is not in this Ruleset";
        return MapEntityIndexStatus::ComponentMissing;
    }
    const auto slot = layout->fieldSlotsByName.find(spec.sourceIdFieldName);
    if (slot == layout->fieldSlotsByName.end())
    {
        message = "component " + spec.componentName + " has no field "
            + spec.sourceIdFieldName;
        return MapEntityIndexStatus::ComponentMissing;
    }
    sourceIdSlot_ = slot->second;

    const std::filesystem::path payload =
        std::filesystem::path(asset.source.physicalDirectory)
            / asset.assetPath;
    std::string bytes;
    {
        std::ifstream stream(payload, std::ios::binary);
        if (!stream)
        {
            message = "payload " + payload.string() + " could not be opened";
            return MapEntityIndexStatus::PayloadMissing;
        }
        bytes.assign(
            std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>()
        );
    }
    // Before decoding, not after. A table that does not match its declaration
    // is not a table with a problem, it is a different file -- and the whole
    // point of this one is that it says which province is which.
    const std::string digest = kernel::ComputeContentDigest(bytes);
    if (digest != asset.assetDigest)
    {
        message = "payload digest " + digest + " does not match the declared "
            + asset.assetDigest;
        return MapEntityIndexStatus::DigestMismatch;
    }
    const std::size_t expected =
        (static_cast<std::size_t>(count_) + 1u) * 4u;
    if (bytes.size() != expected)
    {
        message = "the id table is not " + std::to_string(count_ + 1)
            + " little-endian source ids";
        return MapEntityIndexStatus::AssetInvalid;
    }

    sourceIdByIndex_.assign(static_cast<std::size_t>(count_) + 1, 0u);
    for (std::uint32_t index = 0; index <= count_; ++index)
    {
        const std::size_t at = static_cast<std::size_t>(index) * 4u;
        std::uint32_t value = 0;
        for (int shift = 0; shift < 32; shift += 8)
        {
            value |= static_cast<std::uint32_t>(
                static_cast<unsigned char>(bytes[at + shift / 8])) << shift;
        }
        sourceIdByIndex_[index] = value;
    }
    bound_ = true;
    return MapEntityIndexStatus::Ok;
}

MapEntityIndexStatus MapEntityIndex::Resolve(const PresentationView& view)
{
    if (!bound_)
    {
        return MapEntityIndexStatus::NotBound;
    }
    if (!view.IsBound())
    {
        return MapEntityIndexStatus::ViewNotBound;
    }

    entityByIndex_.assign(
        static_cast<std::size_t>(count_) + 1,
        kernel::EntityId{}
    );
    indexByEntity_.clear();
    resolved_ = 0;

    // source_id -> Entity, read out of the world. Every entity that carries
    // the component the Package named is a candidate; nothing is assumed about
    // how many there are or what they are called.
    std::unordered_map<std::uint32_t, std::uint64_t> entityBySourceId;
    const runtime::ComponentQuerySnapshot& components =
        view.World().Components();
    const std::vector<kernel::EntityId>& owners =
        components.FindOwners(component_);
    entityBySourceId.reserve(owners.size() * 2);
    for (const kernel::EntityId owner : owners)
    {
        const kernel::MechanismValue* value = components.FindField(
            owner,
            component_,
            sourceIdSlot_
        );
        if (value == nullptr)
        {
            continue;
        }
        const auto* number = std::get_if<std::int64_t>(&value->data);
        if (number == nullptr || *number < 0 || *number > 0xFFFFFFFF)
        {
            continue;
        }
        // Two entities claiming one source_id would make a click ambiguous.
        // The first wins and the second is dropped rather than silently
        // replacing it; Resolved() is what makes that visible.
        entityBySourceId.emplace(
            static_cast<std::uint32_t>(*number),
            owner.value
        );
    }

    indexByEntity_.reserve(entityBySourceId.size() * 2);
    for (std::uint32_t index = 1; index <= count_; ++index)
    {
        const auto found = entityBySourceId.find(sourceIdByIndex_[index]);
        if (found == entityBySourceId.end())
        {
            continue;
        }
        entityByIndex_[index] = kernel::EntityId{found->second};
        indexByEntity_.emplace(found->second, index);
        ++resolved_;
    }
    return MapEntityIndexStatus::Ok;
}

kernel::EntityId MapEntityIndex::EntityFor(
    std::uint32_t rasterIndex
) const noexcept
{
    if (rasterIndex == 0 || rasterIndex >= entityByIndex_.size())
    {
        return {};
    }
    return entityByIndex_[rasterIndex];
}

std::uint32_t MapEntityIndex::IndexFor(kernel::EntityId entity) const
{
    if (!entity)
    {
        return 0;
    }
    const auto found = indexByEntity_.find(entity.value);
    return found == indexByEntity_.end() ? 0 : found->second;
}

std::uint32_t MapEntityIndex::SourceIdFor(
    std::uint32_t rasterIndex
) const noexcept
{
    if (rasterIndex >= sourceIdByIndex_.size())
    {
        return 0;
    }
    return sourceIdByIndex_[rasterIndex];
}

}
