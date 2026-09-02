#include "frozen_presentation_catalog.hpp"

namespace dillen::presentation {

PresentationViewId StablePresentationViewId(std::string_view canonicalName)
{
    // The same local hash the Schema Registry uses, and for the same reason:
    // presentation identity is outside the determinism closure and must be
    // free to move without touching the simulation's.
    std::uint64_t value = 14695981039346656037ULL;
    for (const unsigned char character : canonicalName)
    {
        value ^= character;
        value *= 1099511628211ULL;
    }
    return {value == 0 ? 1 : value};
}

const CompiledPresentationView* FrozenPresentationCatalog::FindView(
    PresentationViewId id
) const
{
    for (const CompiledPresentationView& view : views_)
    {
        if (view.id == id)
        {
            return &view;
        }
    }
    return nullptr;
}

const CompiledPresentationView* FrozenPresentationCatalog::FindView(
    std::string_view name
) const
{
    return FindView(StablePresentationViewId(name));
}

const kernel::PresentationAsset* FrozenPresentationCatalog::FindAsset(
    std::string_view kind
) const
{
    for (const kernel::PresentationAsset& asset : assets_)
    {
        if (asset.kind == kind)
        {
            return &asset;
        }
    }
    return nullptr;
}

}
