#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "political_snapshot.hpp"

namespace dillen::compatibility::hoi3::worldbuilder {

struct Hoi31936ContentOptions
{
    std::filesystem::path root;

    //
    // Existing map contracts/content.
    //
    std::string mapContractPackageName =
        "dillen.map.world.contracts";

    std::string mapContentPackageName =
        "dillen.map.world";

    std::string mapComponentName =
        "dillen.map.geography";

    std::string mapRelationName =
        "dillen.map.borders";

    std::string mapEntityTypeName =
        "dillen.map.region";

    std::string mapEntityNamePrefix =
        "dillen.map.region_";

    //
    // New country/political world.
    //
    std::string countryContractPackageName =
        "dillen.country.contracts";

    std::string worldPackageName =
        "dillen.hoi3.1936_world";

    std::string presentationPackageName =
        "dillen.hoi3.1936_world.presentation";

    std::string rulesetName =
        "dillen.hoi3.1936_world_root";

    std::string countryEntityTypeName =
        "dillen.country";

    std::string countryEntityNamePrefix =
        "dillen.country.";

    std::string countryIdentityComponentName =
        "dillen.country.identity";

    std::string ownershipRelationName =
        "dillen.country.owns_region";

    std::string controlRelationName =
        "dillen.country.controls_region";

    std::string coreRelationName =
        "dillen.country.core_on_region";

    std::string capitalRelationName =
        "dillen.country.capital_region";

    std::string modeSetAssetName =
        "dillen.hoi3.1936.map_modes";
};

enum class Hoi31936ContentStatus
{
    Ok,
    InvalidInput,
    RootNotWritable,
    WriteFailed
};

struct Hoi31936ContentReport
{
    Hoi31936ContentStatus status =
        Hoi31936ContentStatus::Ok;

    std::string message;

    std::uint32_t countries = 0;

    std::uint32_t ownerships = 0;
    std::uint32_t controls = 0;
    std::uint32_t cores = 0;
    std::uint32_t capitals = 0;

    std::uint32_t unmappedProvinces = 0;
    std::uint32_t countriesWithoutColour = 0;

    std::uint64_t bytes = 0;
    std::uint32_t files = 0;

    std::string contractDigest;
    std::string contentDigest;
    std::string presentationDigest;

    explicit operator bool() const noexcept
    {
        return status
            == Hoi31936ContentStatus::Ok;
    }
};

Hoi31936ContentReport
EmitHoi31936PoliticalContent(
    const PoliticalSnapshot& snapshot,
    const std::vector<std::uint32_t>&
        provinceSourceIdByDenseIndex,
    const Hoi31936ContentOptions& options
);

}