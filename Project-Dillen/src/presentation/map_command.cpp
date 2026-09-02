#include "map_command.hpp"

#include <utility>

namespace dillen::presentation {

MapCommandStatus MapCommandTranslator::Bind(
    const kernel::FrozenRuntimeCatalog& catalog,
    const MapCommandSpec& spec,
    std::string& message
)
{
    bound_ = false;
    resolved_ = 0;
    instanceByEntity_.clear();
    provided_.clear();

    if (!catalog.IsFrozen())
    {
        message = "the Runtime Catalog is not frozen";
        return MapCommandStatus::CatalogNotFrozen;
    }
    if (!spec.definition || spec.roleName.empty())
    {
        message = "the command spec is incomplete";
        return MapCommandStatus::SpecInvalid;
    }
    const kernel::CompiledMechanismDefinition* definition =
        catalog.FindDefinition(spec.definition);
    if (definition == nullptr)
    {
        message = "the view's Definition is not in this Ruleset";
        return MapCommandStatus::DefinitionMissing;
    }
    const auto role = catalog.ResolveRoleSlot(
        definition->type,
        definition->schemaVersion,
        spec.roleName
    );
    if (!role)
    {
        message = "role " + spec.roleName + " is not on this Definition";
        return MapCommandStatus::RoleMissing;
    }

    definition_ = spec.definition;
    role_ = *role;
    // What this Definition publicly offers, resolved by the Runtime Compiler
    // from its `provides_capabilities` declaration. Copied once here so
    // Translate compares ids rather than walking the catalog per click.
    provided_ = definition->providedCapabilities;
    bound_ = true;
    return MapCommandStatus::Ok;
}

MapCommandStatus MapCommandTranslator::Resolve(const PresentationView& view)
{
    if (!bound_)
    {
        return MapCommandStatus::NotBound;
    }
    if (!view.IsBound())
    {
        return MapCommandStatus::ViewNotBound;
    }

    instanceByEntity_.clear();
    resolved_ = 0;
    const kernel::MechanismQuerySnapshot& mechanisms =
        view.World().Mechanisms();
    for (const kernel::MechanismInstanceId id
        : mechanisms.FindByDefinition(definition_))
    {
        const kernel::MechanismInstance* instance = mechanisms.Find(id);
        if (instance == nullptr
            || role_.value >= instance->roles.size()
            || instance->roles[role_.value].empty())
        {
            continue;
        }
        const kernel::MechanismReference& bound =
            instance->roles[role_.value].front();
        if (bound.kind != kernel::MechanismReferenceKind::Entity)
        {
            continue;
        }
        // Two instances claiming one Entity would make a click ambiguous. The
        // first wins and the second is dropped rather than silently replacing
        // it, and Resolved() is what makes that visible.
        if (instanceByEntity_.emplace(bound.value, id.value).second)
        {
            ++resolved_;
        }
    }
    return MapCommandStatus::Ok;
}

kernel::MechanismInstanceId MapCommandTranslator::InstanceFor(
    kernel::EntityId entity
) const noexcept
{
    if (!bound_ || !entity)
    {
        return {};
    }
    const auto found = instanceByEntity_.find(entity.value);
    if (found == instanceByEntity_.end())
    {
        return {};
    }
    return kernel::MechanismInstanceId{found->second};
}

MapCommandStatus MapCommandTranslator::Translate(
    const MapIntent& intent,
    kernel::WorldTransaction& output
) const
{
    output.commands.clear();
    if (!bound_)
    {
        return MapCommandStatus::NotBound;
    }
    if (!intent)
    {
        return MapCommandStatus::IntentEmpty;
    }
    const kernel::MechanismInstanceId instance = InstanceFor(intent.entity);
    if (!instance)
    {
        return MapCommandStatus::EntityHasNoMechanism;
    }

    // The check that makes this a CONTRACT rather than a name.
    //
    // The control named a Capability the Ruleset publishes and the Package
    // declared it uses; what is checked here is the third thing, which neither
    // of those covers: that the mechanism about to be commanded has itself
    // offered to be reached that way. A UI cannot act on a Definition that
    // never declared it provides the contract, however well the name matches.
    bool offered = false;
    for (const kernel::CapabilityProvision& provision : provided_)
    {
        if (provision.capability == intent.capability
            && provision.version == intent.capabilityVersion)
        {
            offered = true;
            break;
        }
    }
    if (!offered)
    {
        return MapCommandStatus::CapabilityNotProvided;
    }

    // A delta, so several inputs landing in one tick accumulate instead of
    // overwriting one another. Presentation reads a snapshot; the world moves
    // on after it, and an absolute write would treat that read as a lock.
    output.commands.push_back(
        kernel::WorldCommand::Mechanism(
            kernel::MechanismCommand::AddField(
                instance,
                intent.field,
                kernel::MechanismValue{std::int64_t{intent.delta}}
            )
        )
    );
    return MapCommandStatus::Ok;
}

}
