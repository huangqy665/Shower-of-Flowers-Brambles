#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "frozen_runtime_catalog.hpp"
#include "presentation_asset.hpp"
#include "presentation_schema.hpp"

namespace dillen::presentation {

// The Frozen Presentation Catalog: what a Presentation Package becomes once it
// has been compiled, and the only thing the runtime is allowed to read.
//
// Before this existed, a control tree was an editable tree of strings that the
// layout engine interpreted every time it was loaded: `if (key == "axis")`,
// `action != "adjust_level"`, a field name looked up by string. That is the
// authoring form, and running on the authoring form is what this whole project
// refuses everywhere else -- Mechanisms, Components, Relations and Algorithms
// all go Source -> Registry -> Compile -> Frozen Catalog, and the Runtime never
// sees a name.
//
// So this is the presentation half of that same shape:
//
//   Presentation Source (.dasset `content`, untyped to the Kernel)
//        |  PresentationCompiler, against the Schema Registry
//        |  and the Frozen Runtime Catalog
//   Frozen Presentation Catalog  <- immutable, addressed by id, no strings
//
// After Compile, every property is a typed value at a known slot, every
// mechanism field is a resolved MechanismFieldSlotId, and the tree is a flat
// array. Nothing downstream parses anything.
//
// TWO IDENTITIES, AND WHY BOTH ARE HERE
//
// A compiled view is only meaningful for the sources it came from AND for the
// Ruleset it was resolved against -- its field slots are indices into that
// Ruleset's layouts. Carrying both fingerprints lets a mismatch be refused
// instead of silently reading field 3 of the wrong mechanism. The
// PresentationFingerprint is the one the Kernel already computes over the
// sources; no second hash of the same thing is invented here.

using PresentationViewId =
    kernel::StrongId<struct PresentationViewIdTag, std::uint64_t, 0>;

PresentationViewId StablePresentationViewId(std::string_view canonicalName);

// A property value after compilation.
//
// One record per property the kind declares, dense and in schema order, with
// defaults already applied. `kind` is copied from the schema so a reader can
// assert what it is getting without going back to the registry.
struct CompiledControlValue
{
    ControlPropertyKind kind = ControlPropertyKind::Text;
    // Text: the string. Everything else leaves this empty.
    std::string text;
    // Integer, Boolean (0/1), Axis (0 vertical, 1 horizontal), Extent (the
    // fixed pixel count, or the weight when `fill` is set).
    std::int64_t number = 0;
    // Extent only.
    bool fill = false;
    // MechanismField only: resolved against the Ruleset at compile time. A
    // field the Ruleset does not provide is a compile error, not an empty
    // control at runtime.
    kernel::MechanismFieldSlotId fieldSlot;
    // Capability only: resolved the same way, and checked against the
    // capability the asset declares in `requires`.
    kernel::CapabilityId capability;
};

// A control, flattened.
//
// Children are a contiguous range because the tree is stored depth-first: the
// layout walk is then a walk over an array rather than a chase through
// pointers, and the whole view is one allocation that can be shared by every
// viewer without copying.
struct CompiledControl
{
    ControlKindId kind;
    // Index into the Schema Registry, so nothing looks the kind up by id.
    std::uint32_t kindIndex = 0;
    std::uint32_t firstChild = 0;
    std::uint32_t childCount = 0;
    std::uint32_t firstValue = 0;
    std::uint32_t valueCount = 0;
};

struct CompiledPresentationView
{
    PresentationViewId id;
    std::string canonicalName;
    // Depth first; the root is index 0.
    std::vector<CompiledControl> controls;
    std::vector<CompiledControlValue> values;
    // The mechanism fields this view reads, in tree order and de-duplicated.
    // A panel binds with exactly these, so the layout and the reader cannot
    // drift apart.
    std::vector<std::string> boundFieldNames;
    std::vector<kernel::MechanismFieldSlotId> boundFieldSlots;
    // The Definition the fields were resolved against.
    kernel::MechanismDefinitionId definition;
    // The role through which an instance of that Definition claims the Entity
    // a control acts on.
    //
    // Declared by the Package, in the binding's `properties`. It used to be a
    // string literal in the host -- `"province"` -- which meant a Package
    // could only be swapped for one that happened to use the same word for
    // the same idea.
    std::string subjectRoleName;
};

class FrozenPresentationCatalog
{
public:
    bool IsFrozen() const noexcept { return frozen_; }
    std::size_t ViewCount() const noexcept { return views_.size(); }

    // The identity of the sources this was compiled from, and of the Ruleset
    // it was resolved against. A consumer that holds a different one of either
    // is holding a catalog that does not describe its world.
    kernel::PresentationFingerprint SourceFingerprint() const noexcept
    {
        return sourceFingerprint_;
    }
    kernel::RulesetFingerprint RulesetFingerprint() const noexcept
    {
        return rulesetFingerprint_;
    }

    const PresentationSchemaRegistry& Schema() const noexcept
    {
        return schema_;
    }

    const CompiledPresentationView* FindView(PresentationViewId id) const;
    const CompiledPresentationView* FindView(std::string_view name) const;
    const std::vector<CompiledPresentationView>& Views() const noexcept
    {
        return views_;
    }

    // Assets that carry a payload rather than a control tree -- the index
    // raster, the font. Their declarations stay as the Kernel produced them:
    // their properties are consumed by loaders that already verify a digest,
    // and giving them a second typed form here would be a second place for the
    // same statement to live. What they gain from this catalog is being
    // addressable and being covered by one fingerprint.
    const kernel::PresentationAsset* FindAsset(std::string_view kind) const;
    const std::vector<kernel::PresentationAsset>& Assets() const noexcept
    {
        return assets_;
    }

private:
    friend class PresentationCompiler;

    bool frozen_ = false;
    PresentationSchemaRegistry schema_;
    kernel::PresentationFingerprint sourceFingerprint_;
    kernel::RulesetFingerprint rulesetFingerprint_;
    std::vector<CompiledPresentationView> views_;
    std::vector<kernel::PresentationAsset> assets_;
};

}
