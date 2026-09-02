#include "presentation_schema.hpp"

namespace dillen::presentation {

namespace {

// A hash local to presentation, not the one the simulation's Stable Identity
// layer uses.
//
// The two closures are already separate -- a Presentation Package carries no
// Package Lock entry and no Ruleset Fingerprint contribution -- and sharing a
// hash would quietly couple them again, so that a change to the identity of
// the simulation would move the identity of a skin.
std::uint64_t HashName(std::string_view text) noexcept
{
    std::uint64_t value = 14695981039346656037ULL;
    for (const unsigned char character : text)
    {
        value ^= character;
        value *= 1099511628211ULL;
    }
    return value == 0 ? 1 : value;
}

ControlPropertySchema Text(std::string name, std::string fallback = {})
{
    return {std::move(name), ControlPropertyKind::Text, false,
        std::move(fallback)};
}

ControlPropertySchema Extent(std::string name, std::string fallback)
{
    return {std::move(name), ControlPropertyKind::Extent, false,
        std::move(fallback)};
}

ControlPropertySchema Integer(std::string name, std::string fallback)
{
    return {std::move(name), ControlPropertyKind::Integer, false,
        std::move(fallback)};
}

ControlPropertySchema Boolean(std::string name, std::string fallback)
{
    return {std::move(name), ControlPropertyKind::Boolean, false,
        std::move(fallback)};
}

}

ControlKindId StableControlKindId(std::string_view canonicalName)
{
    return {HashName(canonicalName)};
}

ControlPropertySlotId ControlKindSchema::Slot(
    std::string_view property
) const
{
    for (std::size_t index = 0; index < properties.size(); ++index)
    {
        if (properties[index].name == property)
        {
            return {static_cast<std::uint32_t>(index)};
        }
    }
    return {};
}

PresentationSchemaStatus PresentationSchemaRegistry::Add(
    ControlKindSchema schema,
    std::string& message
)
{
    if (frozen_)
    {
        message = "the Presentation Schema Registry is frozen";
        return PresentationSchemaStatus::Frozen;
    }
    if (schema.name.empty())
    {
        message = "a control kind needs a name";
        return PresentationSchemaStatus::KindInvalid;
    }
    schema.id = StableControlKindId(schema.name);
    if (Find(schema.id) != nullptr)
    {
        message = "control kind '" + schema.name + "' is declared twice";
        return PresentationSchemaStatus::DuplicateKind;
    }
    for (std::size_t index = 0; index < schema.properties.size(); ++index)
    {
        if (schema.properties[index].name.empty())
        {
            message = "a property of '" + schema.name + "' has no name";
            return PresentationSchemaStatus::KindInvalid;
        }
        for (std::size_t other = 0; other < index; ++other)
        {
            if (schema.properties[other].name
                == schema.properties[index].name)
            {
                message = "property '" + schema.properties[index].name
                    + "' of '" + schema.name + "' is declared twice";
                return PresentationSchemaStatus::DuplicateProperty;
            }
        }
    }
    kinds_.push_back(std::move(schema));
    return PresentationSchemaStatus::Ok;
}

const ControlKindSchema* PresentationSchemaRegistry::Find(
    ControlKindId kind
) const
{
    for (const ControlKindSchema& schema : kinds_)
    {
        if (schema.id == kind)
        {
            return &schema;
        }
    }
    return nullptr;
}

const ControlKindSchema* PresentationSchemaRegistry::Find(
    std::string_view name
) const
{
    for (const ControlKindSchema& schema : kinds_)
    {
        if (schema.name == name)
        {
            return &schema;
        }
    }
    return nullptr;
}

std::uint32_t PresentationSchemaRegistry::IndexOf(ControlKindId kind) const
{
    for (std::size_t index = 0; index < kinds_.size(); ++index)
    {
        if (kinds_[index].id == kind)
        {
            return static_cast<std::uint32_t>(index);
        }
    }
    return UINT32_MAX;
}

bool RegisterBuiltinControls(PresentationSchemaRegistry& registry)
{
    std::string message;

    // The first four properties are shared by every kind and in the same
    // order, which is what lets `builtin::kId` and friends be constants
    // instead of lookups. The check at the end of this function is the guard
    // on that.
    const auto common = []()
    {
        std::vector<ControlPropertySchema> properties;
        properties.push_back(Text("id"));
        properties.push_back(Text("text"));
        properties.push_back(Extent("size", "0"));
        // Whether this control paints a surface behind it.
        //
        // Declared by the layout rather than inferred. The previous version
        // painted a panel only when its id happened to be "province_panel",
        // so renaming the root in a Package silently lost the background --
        // the exact failure the "refuse, do not ignore" rule exists to
        // prevent, committed in the file that states the rule.
        properties.push_back(Boolean("background", "no"));
        return properties;
    };

    {
        ControlKindSchema panel;
        panel.name = "panel";
        panel.allowsChildren = true;
        panel.properties = common();
        panel.properties.push_back(
            {"axis", ControlPropertyKind::Axis, false, "vertical"});
        panel.properties.push_back(Integer("padding", "0"));
        panel.properties.push_back(Integer("gap", "0"));
        registry.Add(std::move(panel), message);
    }
    {
        ControlKindSchema label;
        label.name = "label";
        label.properties = common();
        registry.Add(std::move(label), message);
    }
    {
        ControlKindSchema value;
        value.name = "value";
        value.properties = common();
        value.properties.push_back(
            {"field", ControlPropertyKind::MechanismField, true, {}});
        registry.Add(std::move(value), message);
    }
    {
        ControlKindSchema button;
        button.name = "button";
        button.properties = common();
        button.properties.push_back(
            {"capability", ControlPropertyKind::Capability, true, {}});
        button.properties.push_back(Integer("capability_version", "1"));
        button.properties.push_back(
            {"operation", ControlPropertyKind::Operation, true, {}});
        button.properties.push_back(
            {"field", ControlPropertyKind::MechanismField, true, {}});
        button.properties.push_back(Integer("amount", "0"));
        registry.Add(std::move(button), message);
    }

    // The slot constants are only sound while the schema puts these
    // properties where they say. Checked unconditionally -- an `assert` would
    // be compiled out of Release and leave the constants unguarded in the only
    // build anyone runs -- because a property inserted in the middle would
    // silently repoint every one of them and produce a layout that reads its
    // gap out of its padding.
    //
    // On failure the registry is left empty and unfrozen, so every later use
    // fails loudly rather than compiling against a vocabulary that is not
    // there.
    const auto slotHolds = [&registry](
        const char* kind,
        const char* property,
        std::uint32_t expected)
    {
        const ControlKindSchema* schema = registry.Find(kind);
        return schema != nullptr && schema->Slot(property).value == expected;
    };
    const bool slotsHold =
        slotHolds("panel", "id", builtin::kId)
        && slotHolds("panel", "text", builtin::kText)
        && slotHolds("panel", "size", builtin::kSize)
        && slotHolds("panel", "background", builtin::kBackground)
        && slotHolds("panel", "axis", builtin::kPanelAxis)
        && slotHolds("panel", "padding", builtin::kPanelPadding)
        && slotHolds("panel", "gap", builtin::kPanelGap)
        && slotHolds("label", "background", builtin::kBackground)
        && slotHolds("value", "field", builtin::kValueField)
        && slotHolds("button", "capability", builtin::kButtonCapability)
        && slotHolds("button", "capability_version",
            builtin::kButtonCapabilityVersion)
        && slotHolds("button", "operation", builtin::kButtonOperation)
        && slotHolds("button", "field", builtin::kButtonField)
        && slotHolds("button", "amount", builtin::kButtonAmount);
    if (!slotsHold)
    {
        registry = PresentationSchemaRegistry{};
        return false;
    }

    registry.Freeze();
    return true;
}

}
