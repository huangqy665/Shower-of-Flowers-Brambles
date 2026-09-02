#include "presentation_asset.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace dillen::kernel {

namespace {

// The same FNV-derived mixer the Ruleset Fingerprint uses, kept local rather
// than shared: the two fingerprints must be free to diverge in what they cover
// without one dragging the other along.
class FingerprintWriter
{
public:
    FingerprintWriter()
        : first_(14695981039346656037ULL),
          second_(1099511628211ULL ^ 0x9E3779B97F4A7C15ULL)
    {
    }

    void Text(std::string_view text)
    {
        Unsigned(text.size());
        for (const unsigned char value : text)
        {
            Byte(value);
        }
    }

    void Unsigned(std::uint64_t value)
    {
        for (std::size_t index = 0; index < sizeof(value); ++index)
        {
            Byte(static_cast<unsigned char>(value & 0xFFU));
            value >>= 8U;
        }
    }

    PresentationFingerprint Finish() const noexcept
    {
        return {
            first_ == 0 ? 1 : first_,
            second_ == 0 ? 1 : second_
        };
    }

private:
    void Byte(unsigned char value) noexcept
    {
        first_ ^= value;
        first_ *= 1099511628211ULL;
        second_ += value;
        second_ *= 0x9E3779B97F4A7C15ULL;
        second_ ^= second_ >> 29;
    }

    std::uint64_t first_;
    std::uint64_t second_;
};

}

std::string PresentationFingerprint::ToHex() const
{
    std::ostringstream stream;
    stream << std::hex << std::setfill('0')
        << std::setw(16) << high
        << std::setw(16) << low;
    return stream.str();
}

bool operator==(
    PresentationFingerprint first,
    PresentationFingerprint second
) noexcept
{
    return first.high == second.high && first.low == second.low;
}

bool operator!=(
    PresentationFingerprint first,
    PresentationFingerprint second
) noexcept
{
    return !(first == second);
}

PresentationFingerprint ComputePresentationFingerprint(
    std::vector<PresentationAsset> assets
)
{
    if (assets.empty())
    {
        return {};
    }
    // Sorted by canonical name so that source layer order, file system order
    // and load priority cannot move the fingerprint. Two installs that declare
    // the same assets have the same presentation identity however they were
    // assembled.
    std::sort(
        assets.begin(),
        assets.end(),
        [](const PresentationAsset& first, const PresentationAsset& second)
        {
            return first.canonicalName < second.canonicalName;
        }
    );

    FingerprintWriter writer;
    writer.Text("dillen.presentation.fingerprint.v1");
    writer.Unsigned(assets.size());
    for (const PresentationAsset& asset : assets)
    {
        writer.Text(asset.canonicalName);
        writer.Text(asset.kind);
        writer.Text(asset.assetPath);
        // The payload's digest, not the payload. A presentation identity has
        // to change when the bytes behind it change, and hashing a 24 MB
        // raster on every load to discover that would be absurd.
        writer.Text(asset.assetDigest);
        writer.Unsigned(asset.properties.size());
        // std::map iterates in key order, so the property hash is stable
        // without a second sort.
        for (const auto& property : asset.properties)
        {
            writer.Text(property.first);
            writer.Text(property.second);
        }
    }
    return writer.Finish();
}

}
