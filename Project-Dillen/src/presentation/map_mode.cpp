#include "map_mode.hpp"

#include <algorithm>
#include <cstdlib>

#include "algorithm_execution_budget.hpp"
#include "algorithm_execution_policy.hpp"
#include "fixed_point.hpp"
#include "read_path.hpp"
#include "runtime_compiler.hpp"

namespace dillen::presentation {

namespace {

const kernel::PresentationAssetNode* Child(
    const kernel::PresentationAssetNode& node,
    const std::string& key
)
{
    for (const kernel::PresentationAssetNode& child : node.children)
    {
        if (child.key == key)
        {
            return &child;
        }
    }
    return nullptr;
}

bool ReadUnsigned(
    const std::string& text,
    std::uint64_t limit,
    std::uint64_t& out
)
{
    if (text.empty())
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
        value = value * 10u + static_cast<std::uint64_t>(character - '0');
        if (value > limit)
        {
            return false;
        }
    }
    out = value;
    return true;
}

bool ReadSigned(const std::string& text, std::int64_t& out)
{
    if (text.empty())
    {
        return false;
    }
    const bool negative = text[0] == '-';
    std::uint64_t magnitude = 0;
    if (!ReadUnsigned(
            negative ? text.substr(1) : text,
            0x7FFFFFFFFFFFFFFFull,
            magnitude))
    {
        return false;
    }
    out = negative
        ? -static_cast<std::int64_t>(magnitude)
        : static_cast<std::int64_t>(magnitude);
    return true;
}

// 0x00RRGGBB as content writes it, to the 0xAABBGGRR the palette texture
// wants. One conversion, in one place, so a mode's declared colour and the
// pixel it produces cannot drift apart.
std::uint32_t PackColour(std::uint64_t declared)
{
    return 0xFF000000u
        | ((declared & 0xFFull) << 16)
        | static_cast<std::uint32_t>(declared & 0xFF00ull)
        | static_cast<std::uint32_t>((declared >> 16) & 0xFFull);
}

bool ReadColour(
    const kernel::PresentationAssetNode& node,
    const std::string& key,
    std::uint32_t& out
)
{
    const kernel::PresentationAssetNode* child = Child(node, key);
    if (child == nullptr || child->block)
    {
        return false;
    }
    std::uint64_t declared = 0;
    if (!ReadUnsigned(child->value, 0xFFFFFFull, declared))
    {
        return false;
    }
    out = PackColour(declared);
    return true;
}

std::uint32_t Blend(std::uint32_t low, std::uint32_t high, std::int64_t num,
                    std::int64_t den)
{
    if (den <= 0)
    {
        return low;
    }
    const std::int64_t clamped = num < 0 ? 0 : (num > den ? den : num);
    std::uint32_t out = 0xFF000000u;
    for (int shift = 0; shift <= 16; shift += 8)
    {
        const std::int64_t a = (low >> shift) & 0xFF;
        const std::int64_t b = (high >> shift) & 0xFF;
        const std::int64_t mixed = a + (b - a) * clamped / den;
        out |= static_cast<std::uint32_t>(mixed & 0xFF) << shift;
    }
    return out;
}

}

MapModeStatus MapModeSet::Bind(
    const kernel::FrozenRuntimeCatalog& catalog,
    const kernel::PresentationAsset& asset,
    std::string& message
)
{
    bound_ = false;
    modes_.clear();
    palette_.clear();
    polarColour_ = 0;
    hasPolarColour_ = false;

    if (!catalog.IsFrozen())
    {
        message = "the Runtime Catalog is not frozen";
        return MapModeStatus::CatalogNotFrozen;
    }
    if (asset.kind != "map_mode_set")
    {
        message = "asset '" + asset.canonicalName + "' is a " + asset.kind;
        return MapModeStatus::AssetInvalid;
    }

    // Optional. What the renderer paints where the corpus has no ground at
    // all -- the poles a latitude band never reaches. Content's call, not the
    // renderer's; a malformed value is a mistake worth stopping for rather
    // than a silent fall-back to whatever colour the renderer shipped with.
    const auto polar = asset.properties.find("polar_colour");
    if (polar != asset.properties.end())
    {
        std::uint64_t declared = 0;
        if (!ReadUnsigned(polar->second, 0xFFFFFFull, declared))
        {
            message = "the map mode set's polar_colour is not a 0x00RRGGBB "
                "value";
            return MapModeStatus::AssetInvalid;
        }
        polarColour_ = static_cast<std::uint32_t>(declared);
        hasPolarColour_ = true;
    }

    for (const kernel::PresentationAssetNode& node : asset.content)
    {
        if (node.key != "mode" || !node.block)
        {
            continue;
        }
        CompiledMapMode mode;

        const kernel::PresentationAssetNode* id = Child(node, "id");
        const kernel::PresentationAssetNode* label = Child(node, "label");
        if (id == nullptr || id->value.empty())
        {
            message = "a map mode has no id";
            return MapModeStatus::ModeInvalid;
        }
        mode.id = id->value;
        mode.label = label != nullptr ? label->value : mode.id;
        if (Find(mode.id) != modes_.size())
        {
            message = "two map modes are both called '" + mode.id + "'";
            return MapModeStatus::ModeInvalid;
        }

        // --- the read path, authored the way an algorithm's is ------------
        kernel::AlgorithmReadPathDefinition path;
        path.root = kernel::AlgorithmReadRoot::SubjectEntity;
        path.terminal = kernel::AlgorithmReadTerminal::ComponentField;
        path.reduce = kernel::AlgorithmReduce::RequireOne;

        const kernel::PresentationAssetNode* component =
            Child(node, "component");
        const kernel::PresentationAssetNode* field =
            Child(node, "component_field");
        const kernel::PresentationAssetNode* version =
            Child(node, "component_version");
        if (component == nullptr || field == nullptr || version == nullptr)
        {
            message = "map mode '" + mode.id + "' must name a component, a "
                "component_field and a component_version";
            return MapModeStatus::ModeInvalid;
        }
        path.component = kernel::StableComponentTypeId(component->value);
        path.componentField = field->value;
        std::uint64_t schemaVersion = 0;
        if (!ReadUnsigned(version->value, 0xFFFFFFFFull, schemaVersion)
            || schemaVersion == 0)
        {
            message = "map mode '" + mode.id
                + "' has an invalid component_version";
            return MapModeStatus::ModeInvalid;
        }

        // Optional hop. A political mode reads the OWNER's colour, which is
        // one incoming Relation away from the province.
        const kernel::PresentationAssetNode* relation =
            Child(node, "relation");
        if (relation != nullptr)
        {
            const kernel::PresentationAssetNode* direction =
                Child(node, "relation_direction");
            path.traverseRelation = true;
            path.relationType =
                kernel::StableRelationTypeId(relation->value);
            path.direction = (direction != nullptr
                && direction->value == "incoming")
                ? kernel::AlgorithmRelationDirection::Incoming
                : kernel::AlgorithmRelationDirection::Outgoing;
        }

        std::string why;
        if (!kernel::LowerSubjectReadPath(
                path,
                catalog,
                static_cast<std::uint32_t>(schemaVersion),
                why,
                mode.path))
        {
            message = "map mode '" + mode.id + "': " + why;
            return MapModeStatus::PathUnresolved;
        }

        // --- the mapping --------------------------------------------------
        if (!ReadColour(node, "absent", mode.absent))
        {
            message = "map mode '" + mode.id + "' must declare an absent "
                "colour: without one, a province the path reads nothing for "
                "cannot be told from one whose answer is zero";
            return MapModeStatus::ModeInvalid;
        }
        const kernel::PresentationAssetNode* mapping = Child(node, "mapping");
        if (mapping == nullptr || !mapping->block)
        {
            message = "map mode '" + mode.id + "' has no mapping";
            return MapModeStatus::ModeInvalid;
        }
        const kernel::PresentationAssetNode* kind = Child(*mapping, "kind");
        const std::string kindText = kind != nullptr ? kind->value : "";
        if (kindText == "value")
        {
            mode.mapping = MapModeMappingKind::Value;
        }
        else if (kindText == "lookup")
        {
            mode.mapping = MapModeMappingKind::Lookup;
            for (const kernel::PresentationAssetNode& entry
                : mapping->children)
            {
                if (entry.key != "entry" || !entry.block)
                {
                    continue;
                }
                const kernel::PresentationAssetNode* value =
                    Child(entry, "value");
                MapModeLookupEntry row;
                if (value == nullptr
                    || !ReadSigned(value->value, row.value)
                    || !ReadColour(entry, "colour", row.colour))
                {
                    message = "map mode '" + mode.id
                        + "' has a lookup entry without a value and a colour";
                    return MapModeStatus::ModeInvalid;
                }
                mode.lookup.push_back(row);
            }
            if (mode.lookup.empty())
            {
                message = "map mode '" + mode.id + "' looks up nothing";
                return MapModeStatus::ModeInvalid;
            }
        }
        else if (kindText == "ramp")
        {
            mode.mapping = MapModeMappingKind::Ramp;
            const kernel::PresentationAssetNode* low = Child(*mapping, "low");
            const kernel::PresentationAssetNode* high =
                Child(*mapping, "high");
            if (low == nullptr || high == nullptr
                || !ReadSigned(low->value, mode.rampLow)
                || !ReadSigned(high->value, mode.rampHigh)
                || !ReadColour(*mapping, "low_colour", mode.rampLowColour)
                || !ReadColour(*mapping, "high_colour", mode.rampHighColour))
            {
                message = "map mode '" + mode.id + "' has an incomplete ramp";
                return MapModeStatus::ModeInvalid;
            }
            if (mode.rampHigh <= mode.rampLow)
            {
                message = "map mode '" + mode.id
                    + "' has a ramp whose high is not above its low";
                return MapModeStatus::ModeInvalid;
            }
        }
        else
        {
            message = "map mode '" + mode.id + "' has mapping kind '"
                + kindText + "', which is not value, lookup or ramp";
            return MapModeStatus::ModeInvalid;
        }

        modes_.push_back(std::move(mode));
    }

    if (modes_.empty())
    {
        message = "the map mode set declares no modes";
        return MapModeStatus::NoModes;
    }
    bound_ = true;
    return MapModeStatus::Ok;
}

std::size_t MapModeSet::Find(const std::string& id) const noexcept
{
    for (std::size_t index = 0; index < modes_.size(); ++index)
    {
        if (modes_[index].id == id)
        {
            return index;
        }
    }
    return modes_.size();
}

MapModeStatus MapModeSet::Refresh(
    const PresentationView& view,
    const MapEntityIndex& map,
    std::size_t mode
)
{
    if (!bound_)
    {
        return MapModeStatus::AssetInvalid;
    }
    if (mode >= modes_.size())
    {
        return MapModeStatus::ModeOutOfRange;
    }
    if (!view.IsBound())
    {
        return MapModeStatus::ViewNotBound;
    }
    if (!map.IsBound())
    {
        return MapModeStatus::MapIndexNotBound;
    }

    const CompiledMapMode& rule = modes_[mode];
    palette_.assign(static_cast<std::size_t>(map.Count()) + 1u, 0u);
    absent_ = 0;

    for (std::uint32_t index = 1; index <= map.Count(); ++index)
    {
        const kernel::EntityId province = map.EntityFor(index);
        if (!province)
        {
            palette_[index] = rule.absent;
            ++absent_;
            continue;
        }
        // One budget per province, sized for a fan-out this rule could
        // plausibly make. A projection is not a Tick and cannot commit
        // anything, but the read path charges for fan-out and a shared budget
        // would make the fourteen thousandth province behave differently from
        // the first.
        kernel::AlgorithmExecutionPolicy policy;
        runtime::AlgorithmExecutionBudget budget(policy);
        const runtime::ReadPathResult read = runtime::EvaluateSubjectReadPath(
            rule.path,
            view.World(),
            province,
            budget
        );
        if (!read)
        {
            palette_[index] = rule.absent;
            ++absent_;
            continue;
        }
        // Read paths carry values at the internal fixed-point scale; a colour
        // and a lookup key are whole numbers, so they come back here rather
        // than being compared against a scaled constant.
        std::int64_t value = 0;
        if (!kernel::InternalToInteger(read.scaled, value))
        {
            palette_[index] = rule.absent;
            ++absent_;
            continue;
        }

        switch (rule.mapping)
        {
        case MapModeMappingKind::Value:
            palette_[index] = PackColour(
                static_cast<std::uint64_t>(value) & 0xFFFFFFull);
            break;
        case MapModeMappingKind::Lookup:
        {
            bool found = false;
            for (const MapModeLookupEntry& entry : rule.lookup)
            {
                if (entry.value == value)
                {
                    palette_[index] = entry.colour;
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                palette_[index] = rule.absent;
                ++absent_;
            }
            break;
        }
        case MapModeMappingKind::Ramp:
            palette_[index] = Blend(
                rule.rampLowColour,
                rule.rampHighColour,
                value - rule.rampLow,
                rule.rampHigh - rule.rampLow
            );
            break;
        }
    }
    return MapModeStatus::Ok;
}

}
