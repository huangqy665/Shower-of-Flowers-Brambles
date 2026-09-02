#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "province_raster_import.hpp"

namespace dillen::adapter {

// Writes an imported province map out as Dillen Content.
//
// The output is a Content Package: one Component Schema, one Relation Schema,
// one entity table and one relation table, plus a manifest and a ruleset. Four
// content files for a world of 14187 Entities and 41693 Relations -- which is
// the entire reason the table forms exist. One file per object would put 55880
// entries in the Source Lock and hash every one of them into the Ruleset
// Fingerprint.
//
// This is an offline step. The runtime never sees a raster, a CSV or this
// emitter; it sees Dillen Content like any other, and the Package's content
// digest seals it.

struct ProvinceContentOptions
{
    std::filesystem::path root;
    // Names are content decisions, so they are parameters rather than
    // constants baked into the emitter.
    std::string packageName = "dillen.map.world";
    std::string rulesetName = "dillen.map.world_root";
    std::string entityTypeName = "dillen.map.region";
    std::string namePrefix = "dillen.map.region_";
    std::string componentName = "dillen.map.geography";
    std::string relationName = "dillen.map.borders";
    std::string relationNamePrefix = "dillen.map.border_";
    std::string presentationPackageName = "dillen.map.world.presentation";
    std::string rasterAssetName = "dillen.map.world_raster";
};

enum class ProvinceContentStatus
{
    Ok,
    RootNotWritable,
    WriteFailed
};

struct ProvinceContentReport
{
    ProvinceContentStatus status = ProvinceContentStatus::Ok;
    std::string message;
    std::uint32_t entities = 0;
    std::uint32_t relations = 0;
    std::uint64_t bytes = 0;
    std::uint32_t files = 0;
    // The digest the emitted manifest carries. Recomputed from the files just
    // written, so a caller can compare it against a previous run and see
    // whether the corpus produced identical content.
    std::string contentDigest;
    // The presentation payload, verified on its own rather than through the
    // Package content digest -- 24 MB of binary is not an authoring source and
    // the file catalog never classifies it.
    std::string rasterDigest;
    std::uint64_t rasterBytes = 0;
    std::uint32_t rasterRuns = 0;

    explicit operator bool() const noexcept
    {
        return status == ProvinceContentStatus::Ok;
    }
};

ProvinceContentReport EmitProvinceContent(
    const ProvinceRasterImport& imported,
    const ProvinceContentOptions& options
);

}
