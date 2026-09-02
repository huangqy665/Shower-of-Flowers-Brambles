#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "mechanism_ids.hpp"

namespace dillen::presentation {

// The Presentation Schema: what a control kind is, and what it may carry.
//
// WHY THIS IS CODE AND NOT A DSL SOURCE
//
// Every other schema in this project is data -- Component, Relation, Mechanism
// -- and the obvious move would be a `.dcontrolschema` alongside them. It is
// the wrong move here, and the reason is specific rather than a matter of
// taste.
//
// A Component Schema declares a shape the Kernel can then store, compare and
// migrate generically: the data adds capability. A control kind cannot work
// that way. Something has to lay a `panel` out and draw a `button`, and that
// something is code. A data schema could therefore only ever declare a SUBSET
// of the kinds the layout engine already implements -- and a kind it declared
// that the engine did not implement would have to be refused at load. So the
// data would carry no capability the code does not already carry: it would be
// a second copy of the same semantics, kept in sync by hand.
//
// This project has refused that twice for exactly this reason -- once when the
// declarative and the scripted paths threatened to become two implementations
// of one semantics, and once when the morph was nearly rewritten in GLSL. The
// rule it settled on applies unchanged: ONE SOURCE OF TRUTH PER SEMANTICS.
//
// What IS data is the thing data can actually define: the LAYOUT -- which
// controls exist, how they nest, what they bind to, what they say. That is
// what a Presentation Package authors, and it is what the Compiler checks
// against this schema.
//
// So this is a registry rather than a hard-coded switch: validation becomes
// table-driven, properties get types and defaults, a missing or misspelled
// property is reported with the name and the expected type, and the compiled
// form addresses properties by SLOT rather than by string.

using ControlKindId =
    kernel::StrongId<struct ControlKindIdTag, std::uint64_t, 0>;
// A dense index into a control kind's property list. This is the whole point
// of compiling: after Compile nothing looks a property up by name.
using ControlPropertySlotId =
    kernel::StrongId<struct ControlPropertySlotIdTag, std::uint32_t, UINT32_MAX>;

ControlKindId StableControlKindId(std::string_view canonicalName);

// The types a control property may have.
//
// Deliberately small. Every entry here is something the layout engine or the
// binding resolver can act on; a type nothing consumes would be a promise the
// engine cannot keep.
enum class ControlPropertyKind
{
    // Free text: a caption, an id.
    Text,
    // A whole number.
    Integer,
    // `yes` / `no`.
    Boolean,
    // A pixel count, or `fill`, or `fill:<weight>`.
    Extent,
    // `vertical` / `horizontal`.
    Axis,
    // The name of a mechanism field. The Compiler resolves it against the
    // Ruleset and the asset's `requires` block, so a layout cannot bind to a
    // field the Package never declared or the Ruleset does not provide.
    MechanismField,
    // The name of a Capability Contract. The Compiler resolves it against
    // the Ruleset and the asset's `requires` block, so a control can only act
    // through a contract the Package declared and the world publishes.
    Capability,
    // An operation on that contract. Checked against the operations the
    // contract itself declares, so a Package cannot invent a verb.
    Operation
};

struct ControlPropertySchema
{
    std::string name;
    ControlPropertyKind kind = ControlPropertyKind::Text;
    // A property that must be present. The Compiler reports the control and
    // the property name rather than producing a control with a hole in it.
    bool required = false;
    // Applied when the property is absent. Parsed by the same reader the
    // authored value goes through, so a default cannot be a value the schema
    // would have rejected.
    std::string defaultValue;
};

struct ControlKindSchema
{
    std::string name;
    ControlKindId id;
    // Whether nested controls are legal here. A `label` with children is an
    // authoring mistake worth naming rather than ignoring.
    bool allowsChildren = false;
    std::vector<ControlPropertySchema> properties;

    // UINT32_MAX when the kind has no such property.
    ControlPropertySlotId Slot(std::string_view property) const;
};

enum class PresentationSchemaStatus
{
    Ok,
    Frozen,
    DuplicateKind,
    DuplicateProperty,
    KindInvalid
};

// Every control kind the layout engine understands, addressable by id.
//
// Frozen once built, for the same reason the Runtime Catalog is: a vocabulary
// that could grow after compilation would let a later addition change what an
// already-compiled view means.
class PresentationSchemaRegistry
{
public:
    PresentationSchemaStatus Add(
        ControlKindSchema schema,
        std::string& message
    );

    void Freeze() noexcept { frozen_ = true; }
    bool IsFrozen() const noexcept { return frozen_; }
    std::size_t Size() const noexcept { return kinds_.size(); }

    const ControlKindSchema* Find(ControlKindId kind) const;
    const ControlKindSchema* Find(std::string_view name) const;
    // Dense index, so a compiled control can name its kind without a lookup.
    std::uint32_t IndexOf(ControlKindId kind) const;
    const std::vector<ControlKindSchema>& Kinds() const noexcept
    {
        return kinds_;
    }

private:
    bool frozen_ = false;
    std::vector<ControlKindSchema> kinds_;
};

// The vocabulary this layout engine implements.
//
// The single place a new control kind is declared -- next to, and reviewable
// against, the code that lays it out. Returns false, leaving the registry
// empty and unfrozen, if the slot constants below no longer match the schema.
bool RegisterBuiltinControls(PresentationSchemaRegistry& registry);

// Slot constants for the built-in kinds.
//
// The compiled form addresses properties by slot; these are how the layout
// engine names the slots it reads without going back to a string.
// RegisterBuiltinControls checks every one of them against the schema it just
// built, in every configuration, so a schema edit that moves a property cannot
// silently repoint one of them.
namespace builtin {

// Shared by every kind.
inline constexpr std::uint32_t kId = 0;
inline constexpr std::uint32_t kText = 1;
inline constexpr std::uint32_t kSize = 2;
inline constexpr std::uint32_t kBackground = 3;
// panel
inline constexpr std::uint32_t kPanelAxis = 4;
inline constexpr std::uint32_t kPanelPadding = 5;
inline constexpr std::uint32_t kPanelGap = 6;
// value
inline constexpr std::uint32_t kValueField = 4;
// button
//
// A button used to carry `action = adjust_level`, a verb this layer compared
// against a string literal it held itself. It now names a public Capability
// Contract, an operation that contract declares, and the field and delta the
// operation applies -- every one of which is resolved against the Ruleset at
// load. The generic layer knows about capabilities, operations, fields and
// deltas, which are Kernel concepts; it no longer knows about production
// levels, which are not.
inline constexpr std::uint32_t kButtonCapability = 4;
inline constexpr std::uint32_t kButtonCapabilityVersion = 5;
inline constexpr std::uint32_t kButtonOperation = 6;
inline constexpr std::uint32_t kButtonField = 7;
inline constexpr std::uint32_t kButtonAmount = 8;

}

}
