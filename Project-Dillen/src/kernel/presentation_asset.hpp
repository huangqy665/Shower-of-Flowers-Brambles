#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace dillen::kernel {

// What a Presentation Package declares.
//
// This is the first thing a Presentation Package may own, and it is
// deliberately not "a map". The Kernel knows that a Presentation Package
// declares named assets, each with a kind, a payload file and a digest for it;
// it does not know what a raster, a font or a layout is. Presentation
// interprets `kind` and `properties`; nothing here does.
//
// That split matters more than it looks. A map index raster is 24 MB of binary
// -- there is no sane text form for it -- so the declaration and the payload
// have to be different files. The declaration is an authoring source like any
// other and goes through Parse and the Package content digest. The payload is
// left unclassified by the file catalog, which means the pipeline never tries
// to parse 24 MB of binary as text, and its integrity is carried by
// `assetDigest` instead.
struct PresentationAssetSource
{
    std::string sourceName;
    std::string virtualPath;
    // Directory of the declaring source on disk. `assetPath` resolves against
    // it, so a payload travels with its declaration and a Package stays
    // relocatable.
    std::string physicalDirectory;
};

// A reference a Presentation Asset makes into the frozen world.
//
// The `kind` of an asset stays opaque -- the Kernel does not know what a
// widget, a font or a layout is, and should not. But a UI binding that says
// "this panel shows a mechanism's level" is making a claim about the RULESET,
// and that claim is checkable without knowing anything about panels.
//
// So requirements are typed even though kinds are not. That is the whole
// design: memo section 4.4.4 asks that a Binding pointing at a Contract that
// does not exist be refused at load time, and this is what makes it possible
// to refuse it without teaching the Kernel about user interfaces.
//
// A binding that goes unchecked is worse than one that fails: the panel simply
// shows nothing, in a build that loaded cleanly, and the author has no idea
// which of their fifty widgets is the broken one.
struct PresentationAssetRequirement
{
    enum class Kind
    {
        MechanismField,
        ComponentField,
        Capability
    };

    Kind kind = Kind::MechanismField;
    // MechanismField: mechanism type + definition + field.
    // ComponentField: component type + version + field.
    // Capability:     contract name + version.
    std::string primaryName;
    std::string secondaryName;
    std::string fieldName;
    std::uint32_t version = 1;
};

// An untyped node in an asset's `content` tree.
//
// `properties` is a flat map of text the Kernel assigns no meaning to. This is
// the same thing with a shape: a tree of text the Kernel assigns no meaning to.
// The Kernel gains nesting and sibling order; it gains no vocabulary. It still
// does not know what a control, a row or a button is, and a new asset kind that
// needs structure lands without touching this file.
//
// Order is preserved and duplicate keys are legal, because both carry meaning
// in the trees that will be written here -- a list of five labels is five
// siblings under the same key, and their order is what the author wrote.
struct PresentationAssetNode
{
    std::string key;
    // Set when the node was `key = scalar`. Empty for `key = { ... }`.
    std::string value;
    bool block = false;
    std::vector<PresentationAssetNode> children;
};

struct PresentationAsset
{
    std::string canonicalName;
    std::string kind;
    // Free-form and defined by `kind`. Keeping it as text rather than a typed
    // union is what lets a new asset kind land without touching the Kernel.
    std::map<std::string, std::string> properties;
    // Optional, ordered, and interpreted entirely by `kind`. See
    // PresentationAssetNode.
    std::vector<PresentationAssetNode> content;
    // Optional. A binding may be a pure declaration with no payload, in which
    // case both are empty and nothing is loaded from disk.
    std::string assetPath;
    std::string assetDigest;
    std::vector<PresentationAssetRequirement> requirements;
    PresentationAssetSource source;
};

// A hash over every declared Presentation Asset, ordered by canonical name.
//
// Separate from the Ruleset Fingerprint on purpose, and that separation is the
// whole point of the boundary: a Presentation Package is outside the
// determinism closure, so changing a skin must not change what a save
// validates against. This fingerprint exists so that presentation still HAS an
// identity -- one a viewer can compare, cache against, or refuse to mix -- it
// just is not the identity the simulation is sealed with.
struct PresentationFingerprint
{
    std::uint64_t high = 0;
    std::uint64_t low = 0;

    explicit operator bool() const noexcept
    {
        return high != 0 || low != 0;
    }
    std::string ToHex() const;
};

bool operator==(PresentationFingerprint first, PresentationFingerprint second)
    noexcept;
bool operator!=(PresentationFingerprint first, PresentationFingerprint second)
    noexcept;

// `assets` need not be sorted; the fingerprint sorts by canonical name so that
// load order cannot change it.
PresentationFingerprint ComputePresentationFingerprint(
    std::vector<PresentationAsset> assets
);

}
