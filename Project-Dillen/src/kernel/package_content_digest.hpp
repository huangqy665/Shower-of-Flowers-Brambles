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

}
