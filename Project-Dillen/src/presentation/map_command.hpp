#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "frozen_runtime_catalog.hpp"
#include "presentation_view.hpp"
#include "world_transaction.hpp"

namespace dillen::presentation {

// What a player asked for, and how it becomes a World Transaction.
//
// The rule this file exists to keep is that PRESENTATION NEVER WRITES. It
// holds a shared_ptr<const WorldQuerySnapshot> and nothing else, so there is
// no path from a click to the authoritative stores that runs through here.
// What a click produces is an INTENT -- a small value naming a province and a
// change -- and what this translator produces is a kernel::WorldTransaction,
// which is inert until something outside presentation submits it.
//
// That "something outside" is the application: the executable that links both
// the host and presentation, submits through the same KernelRuntime::Enqueue
// the CLI Inspector uses, and owns the only mutable reference in the picture.
// No module links both, so the direction the architecture guard enforces is
// untouched.
//
// The point of the arrangement, stated as a claim worth falsifying: A UI
// PRODUCES NOTHING THE HOST API CANNOT ALREADY EXPRESS. If a panel ever needs
// a route the CLI does not have, that is a hole in the Host API and should be
// visible as one, not quietly patched with a presentation-only entry point.

// What a control asks for.
//
// There is no verb here any more. `MapIntent::Kind::AdjustLevel` used to be an
// enum in this header with one business value in it, compared against the
// string "adjust_level" in the control compiler -- a Demo's vocabulary written
// into the generic layer, bypassing the public contract entirely. What a
// button carries now is a Capability Contract the Ruleset publishes, an
// operation that contract declares, and the field and delta it applies. Every
// one of those is resolved at load; none of them is a name this layer knows.
struct MapIntent
{
    // The Entity, not a position in a picture. A dense raster index is what
    // fits in the id attachment and what the palette is addressed by; it is
    // not an identity. MapEntityIndex converts one to the other from data,
    // once, at the edge.
    kernel::EntityId entity;
    kernel::CapabilityId capability;
    std::uint32_t capabilityVersion = 1;
    kernel::MechanismFieldSlotId field;
    // A delta, not an absolute: several inputs landing in one tick must
    // accumulate rather than overwrite one another, which is the same reason
    // MechanismAddFieldOperation exists.
    std::int64_t delta = 0;

    explicit operator bool() const noexcept
    {
        return static_cast<bool>(entity)
            && static_cast<bool>(capability)
            && static_cast<bool>(field);
    }
};

struct MapCommandSpec
{
    // The Definition whose instances this translator commands, and the role
    // through which they claim an Entity. Both come from the compiled view;
    // no entity naming convention, province count or field name is needed
    // any more.
    kernel::MechanismDefinitionId definition;
    std::string roleName;
};

enum class MapCommandStatus
{
    Ok,
    NotBound,
    CatalogNotFrozen,
    SpecInvalid,
    DefinitionMissing,
    RoleMissing,
    ViewNotBound,
    EntityHasNoMechanism,
    // The instance does not publicly provide the capability the control names.
    // The check that makes this a contract rather than a name.
    CapabilityNotProvided,
    IntentEmpty
};

class MapCommandTranslator
{
public:
    MapCommandStatus Bind(
        const kernel::FrozenRuntimeCatalog& catalog,
        const MapCommandSpec& spec,
        std::string& message
    );

    // Builds the Entity -> instance map by reading each instance's role.
    //
    // It cannot be arithmetic. Instances are numbered by creation ordinal in
    // Spawn order, and Spawns are ordered by hashed id, so instance 5 is not
    // province 5 and never will be. Walking the bindings once is both the
    // correct answer and an end-to-end check that the Spawns bound what they
    // claimed.
    MapCommandStatus Resolve(const PresentationView& view);

    bool IsBound() const noexcept { return bound_; }
    std::uint32_t Resolved() const noexcept { return resolved_; }

    // The instance that claims this Entity, or empty. Exposed so a panel and
    // the command path cannot disagree about whose numbers are on screen.
    kernel::MechanismInstanceId InstanceFor(
        kernel::EntityId entity
    ) const noexcept;

    // Turns an intent into an authoritative Command.
    //
    // The capability check here is the point of the whole arrangement: the
    // instance must PUBLICLY PROVIDE the contract the control named, as
    // declared by its Definition and resolved by the Runtime Compiler. A UI
    // cannot reach a mechanism that has not offered to be reached.
    MapCommandStatus Translate(
        const MapIntent& intent,
        kernel::WorldTransaction& output
    ) const;

private:
    bool bound_ = false;
    kernel::MechanismDefinitionId definition_;
    kernel::MechanismRoleSlotId role_;
    std::vector<kernel::CapabilityProvision> provided_;
    std::unordered_map<std::uint64_t, std::uint64_t> instanceByEntity_;
    std::uint32_t resolved_ = 0;
};

}
