#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "frozen_runtime_catalog.hpp"
#include "map_command.hpp"
#include "presentation_view.hpp"

namespace dillen::presentation {

// What a panel shows for one province.
//
// The read side of the mechanism UI, and deliberately separate from anything
// that draws: it turns a province index into a list of labelled values, and a
// window is only one of the things that can consume that. Being drawable is
// not what makes it correct, so it is testable without a GPU -- the same split
// that kept the morph and the read model out of the backend.
//
// It reads through PresentationView like everything else here, so a panel
// cannot write, and it finds a province's mechanism through the same role
// bindings MapCommandTranslator resolves. Sharing that lookup is not laziness:
// if the panel and the command path disagreed about which instance a province
// owns, a player would read one province's numbers and command another's.
//
// The fields it shows are named by the caller, and a UI binding declares the
// same names in its `requires` block, so a panel that reads a field the
// Ruleset does not provide is refused at load time rather than showing blanks.

struct MechanismPanelField
{
    std::string label;
    // The slot this value came from. A consumer matches on this rather than
    // on the label: after compilation the layout addresses fields by slot, and
    // matching two resolved slots cannot go wrong the way comparing two
    // strings that happen to look alike can.
    kernel::MechanismFieldSlotId slot;
    // Integers as themselves; decimals at the fixed-point INTERNAL scale
    // (10^4), exactly as ProvinceProjection carries them, so nothing between
    // the authoritative value and a rendered string rounds differently on a
    // different machine.
    std::int64_t value = 0;
    bool isDecimal = false;
};

struct MechanismPanelReadout
{
    bool valid = false;
    kernel::EntityId entity;
    std::vector<MechanismPanelField> fields;
};

enum class MechanismPanelStatus
{
    Ok,
    NotBound,
    CatalogNotFrozen,
    DefinitionMissing,
    FieldMissing
};

class MechanismPanel
{
public:
    // Binds to what a compiled view already resolved.
    //
    // The names are gone on purpose. Before this, a host had to pass the
    // mechanism, the Definition and every field name as string literals in
    // C++ -- so a Package could only be replaced by one with identical names.
    // The compiled view carries the Definition it resolved against and the
    // slots it reads; the panel takes those.
    MechanismPanelStatus Bind(
        const kernel::FrozenRuntimeCatalog& catalog,
        kernel::MechanismDefinitionId definition,
        const std::vector<std::string>& fieldNames,
        const std::vector<kernel::MechanismFieldSlotId>& fieldSlots,
        std::string& message
    );

    bool IsBound() const noexcept { return bound_; }

    // An out-of-range or unbound province gives an invalid readout rather than
    // an empty one. "This province has no mechanism" and "this mechanism has
    // no values" are different answers and a panel should not conflate them.
    MechanismPanelReadout Read(
        const MapCommandTranslator& translator,
        const PresentationView& view,
        kernel::EntityId entity
    ) const;

private:
    struct BoundField
    {
        std::string label;
        kernel::MechanismFieldSlotId slot;
    };

    bool bound_ = false;
    std::vector<BoundField> fields_;
};

}
