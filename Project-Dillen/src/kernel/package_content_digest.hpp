#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace dillen::kernel {

struct PackageContentSource
{
    std::string virtualPath;
    std::string_view bytes;
};

std::string ComputePackageContentDigest(
    std::vector<PackageContentSource> sources
);

// A plain SHA-256 over one buffer, with no framing at all.
//
// The Package digest above interleaves virtual paths and lengths, which is
// right for a set of sources and wrong for a single opaque payload: a
// Presentation Asset's binary is verified on its own, by whoever loads it,
// against a digest written in the declaration. Framing it would make that
// digest depend on where the file happens to sit.
std::string ComputeContentDigest(std::string_view bytes);

}
