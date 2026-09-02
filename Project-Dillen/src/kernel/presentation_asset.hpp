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

struct PresentationAsset
{
    std::string canonicalName;
    std::string kind;
    // Free-form and defined by `kind`. Keeping it as text rather than a typed
    // union is what lets a new asset kind land without touching the Kernel.
    std::map<std::string, std::string> properties;
    std::string assetPath;
    std::string assetDigest;
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
