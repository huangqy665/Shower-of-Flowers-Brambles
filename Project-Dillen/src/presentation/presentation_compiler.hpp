#pragma once

#include <string>
#include <vector>

#include "frozen_presentation_catalog.hpp"
#include "frozen_runtime_catalog.hpp"
#include "presentation_asset.hpp"
#include "presentation_schema.hpp"

namespace dillen::presentation {

enum class PresentationCompileStatus
{
    Ok,
    SchemaNotFrozen,
    RuntimeCatalogNotFrozen,
    // The asset declares a `content` tree whose root is not a single control.
    ViewRootInvalid,
    // A control kind the Schema Registry does not have.
    ControlKindUnknown,
    // A key the kind's schema does not declare, a value the property's type
    // cannot take, or a missing required property.
    ControlPropertyInvalid,
    // Children under a kind that does not allow them.
    ChildrenNotAllowed,
    // A `field` the layout binds that the asset's `requires` block does not
    // declare, or that the Ruleset does not provide.
    FieldUnresolved,
    // A capability the asset does not declare in `requires`, that the Ruleset
    // does not publish, or an operation the contract itself does not declare.
    CapabilityUnresolved,
    // Two views with the same canonical name.
    DuplicateView
};

// Presentation Source -> Frozen Presentation Catalog.
//
// This is where every name in a Presentation Package is spent. A view that
// compiles has no unresolved references left: property values are typed and
// slotted, mechanism fields are MechanismFieldSlotIds, and the control tree is
// a flat array. A view that does not compile names the asset, the control and
// the property, and the Package does not load.
//
// The failure this replaces is worth stating, because it is the one the review
// caught: an interpreted tree can only report a problem when the problem is
// reached. A misspelled property on a control nobody clicked was a control
// that quietly did the wrong thing in a build that started cleanly. Compiling
// moves every one of those to load time by construction, rather than by
// remembering to check.
class PresentationCompiler
{
public:
    // `assets` is what the Kernel produced from the Presentation Package;
    // `runtime` is the frozen Ruleset the bindings resolve against. Both
    // fingerprints are recorded in the output so a later mismatch is
    // detectable rather than silently wrong.
    PresentationCompileStatus Compile(
        const PresentationSchemaRegistry& schema,
        const std::vector<kernel::PresentationAsset>& assets,
        const kernel::FrozenRuntimeCatalog& runtime,
        FrozenPresentationCatalog& output,
        std::string& message
    ) const;
};

}
