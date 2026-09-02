#include "mechanism_panel.hpp"

#include <variant>

#include "fixed_point.hpp"
#include "mechanism_ids.hpp"

namespace dillen::presentation {

MechanismPanelStatus MechanismPanel::Bind(
    const kernel::FrozenRuntimeCatalog& catalog,
    kernel::MechanismDefinitionId definition,
    const std::vector<std::string>& fieldNames,
    const std::vector<kernel::MechanismFieldSlotId>& fieldSlots,
    std::string& message
)
{
    bound_ = false;
    fields_.clear();

    if (!catalog.IsFrozen())
    {
        message = "the Runtime Catalog is not frozen";
        return MechanismPanelStatus::CatalogNotFrozen;
    }
    if (!definition || catalog.FindDefinition(definition) == nullptr)
    {
        message = "the view's Definition is not in this Ruleset";
        return MechanismPanelStatus::DefinitionMissing;
    }
    if (fieldNames.size() != fieldSlots.size())
    {
        message = "the view's field names and slots disagree";
        return MechanismPanelStatus::FieldMissing;
    }

    fields_.reserve(fieldSlots.size());
    for (std::size_t index = 0; index < fieldSlots.size(); ++index)
    {
        // Already resolved by the Compiler; re-checked here because a slot
        // that survived compilation against another Ruleset is exactly the
        // failure a resolved index cannot report on its own.
        const auto slot = catalog.ResolveDefinitionFieldSlot(
            definition,
            fieldNames[index]
        );
        if (!slot || *slot != fieldSlots[index])
        {
            message = "field " + fieldNames[index]
                + " does not resolve to the slot the view compiled";
            return MechanismPanelStatus::FieldMissing;
        }
        fields_.push_back({fieldNames[index], *slot});
    }
    bound_ = true;
    return MechanismPanelStatus::Ok;
}

MechanismPanelReadout MechanismPanel::Read(
    const MapCommandTranslator& translator,
    const PresentationView& view,
    kernel::EntityId entity
) const
{
    MechanismPanelReadout readout;
    readout.entity = entity;
    if (!bound_ || !view.IsBound())
    {
        return readout;
    }
    // The same lookup the command path uses. If these two ever diverged a
    // player would read one province's numbers and command another's.
    const kernel::MechanismInstanceId instance =
        translator.InstanceFor(entity);
    if (!instance)
    {
        return readout;
    }

    const kernel::MechanismQuerySnapshot& mechanisms =
        view.World().Mechanisms();
    readout.fields.reserve(fields_.size());
    for (const BoundField& field : fields_)
    {
        const kernel::MechanismValue* value = mechanisms.FindField(
            instance,
            field.slot
        );
        if (value == nullptr)
        {
            return {};
        }
        MechanismPanelField entry;
        entry.label = field.label;
        entry.slot = field.slot;
        if (const auto* integer = std::get_if<std::int64_t>(&value->data))
        {
            entry.value = *integer;
        }
        else if (const auto* decimal = std::get_if<double>(&value->data))
        {
            const kernel::FixedPointValue scaled =
                kernel::DecimalToInternal(*decimal);
            if (!scaled)
            {
                return {};
            }
            entry.value = scaled.scaled;
            entry.isDecimal = true;
        }
        else if (const auto* flag = std::get_if<bool>(&value->data))
        {
            entry.value = *flag ? 1 : 0;
        }
        else
        {
            // Strings and references need a field type this readout does not
            // have. Refusing is better than rendering a zero that looks like
            // data.
            return {};
        }
        readout.fields.push_back(std::move(entry));
    }
    readout.valid = true;
    return readout;
}

}
