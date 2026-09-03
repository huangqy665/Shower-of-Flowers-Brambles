#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
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

// The demo's own gameplay, kept apart from the map.
//
// WHY THIS IS A SEPARATE STRUCT AND AN OPTIONAL ONE
//
// What a province raster gives you is geography: regions, the borders between
// them, and the picture. That is general, it is what any map needs, and it is
// what this emitter can honestly claim to be an Adapter FOR.
//
// A production site on every region, an algorithm that multiplies its level by
// its corpus id, a Capability Contract called site_development and a panel
// with two buttons are none of those things. They are one demo's gameplay, and
// an emitter that always produced them would be a demo generator wearing the
// name of a map Adapter -- every future map would arrive with a production
// mechanic welded on, and the only way to find that out would be to read nine
// hundred lines.
//
// So the slice is optional and named for what it is. Leave it out and the
// output is a map: contracts, regions, borders, a raster and an id table, a
// Ruleset that loads them, and NOTHING ELSE -- no mechanism package, no
// spawns, no interface. province_map_emitter_probe loads exactly that and
// checks the world comes up with zero mechanism instances, which is the only
// way "the general half stands on its own" is a fact rather than a claim.
struct DemoProductionSlice
{
    std::string mechanismPackageName = "dillen.map.production";
    std::string mechanismName = "dillen.map.production_site";
    std::string mechanismDefinitionName = "dillen.map.site";
    std::string algorithmName = "dillen.map.production_algorithm";
    std::string spawnPrefix = "dillen.map.site_";
    // The role through which a site claims its region. Named once here and
    // declared to the Presentation Package, so a host never has to know it.
    std::string subjectRoleName = "province";
    std::string capabilityName = "dillen.map.site_development";
    std::string capabilityOperation = "adjust_level";
    std::string fontAssetName = "dillen.map.ui_font";
    // The UI font, copied into the Presentation Package with a digest of its
    // own. Optional even within the slice: leave it empty and the interface
    // has no captions rather than failing to emit.
    std::filesystem::path fontPath;
};

struct ProvinceContentOptions
{
    // Root of a Dillen game tree. The emitter owns only map/contracts,
    // map/world, production/map_world and presentation/map_world beneath it;
    // regeneration never removes unrelated game Packages or source corpora.
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

    // Absent for a plain map. See DemoProductionSlice.
    std::optional<DemoProductionSlice> slice;
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
    std::uint32_t spawns = 0;
    std::string fontDigest;
    std::uint64_t fontBytes = 0;
    // The raster's index -> source_id table, carried the same way and for the
    // same reason: it is not an authoring source and the digest is the only
    // thing binding the declaration to the bytes.
    std::string provinceIdDigest;
    std::uint64_t provinceIdBytes = 0;

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
