#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <string>

#include "province_raster_import.hpp"

// Demo 0.8 P1a -- province raster import, measured on the real corpus.
//
// This runs against the actual world map in Dillen-Game/map/source: a
// 5616x2160 raster and a table of 14187 provinces. It is deliberately not
// a synthetic fixture. The question P1a
// exists to answer is whether the engine's primitives survive a real
// four-digit heterogeneous world, and a hand-built fixture would answer a
// different, easier question.
//
// What is asserted here is the import itself: the numbers, the invariants that
// generated content depends on, and determinism. Load-time and tick budgets at
// this scale come next, once the content form exists to load.

namespace
{
namespace fs = std::filesystem;
using namespace dillen;

const fs::path kMapRoot = "Dillen-Game/map/source";

int failures = 0;

void Check(bool condition, const std::string& what)
{
    if (!condition)
    {
        std::cerr << "province raster import: " << what << '\n';
        ++failures;
    }
}

}

int main()
{
    adapter::ProvinceRasterImportOptions options;
    options.raster = kMapRoot / "provinces.bmp";
    options.definitions = kMapRoot / "definition.csv";
    options.wrapHorizontally = true;
    // The map in Dillen-Game is already north-up, so this is off. The option
    // exists for corpora that are not -- HOI3's own bitmaps display with
    // north at the bottom -- and the gate at the end of this probe is what
    // catches it being wrong in either direction.
    options.northAtImageBottom = false;

    if (!fs::exists(options.raster) || !fs::exists(options.definitions))
    {
        std::cerr << "province raster import: the map corpus is missing from "
                  << kMapRoot << '\n';
        return 1;
    }

    const auto start = std::chrono::steady_clock::now();
    const adapter::ProvinceRasterImport imported =
        adapter::ImportProvinceRaster(options);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start
    ).count();

    if (!imported)
    {
        std::cerr << "province raster import failed: " << imported.message
                  << '\n';
        return 2;
    }

    // --- the corpus, as measured ---
    Check(imported.width == 5616 && imported.height == 2160,
        "raster is not 5616x2160");
    Check(imported.Count() == 14187,
        "expected 14187 provinces, got "
            + std::to_string(imported.Count()));
    Check(imported.sourceIdByIndex.front() == 0,
        "dense index 0 must be reserved for 'no province'");
    Check(imported.sourceIdByIndex.back() == 14187,
        "the highest corpus id should be 14187, got "
            + std::to_string(imported.sourceIdByIndex.back()));
    Check(imported.indexRaster.size()
            == static_cast<std::size_t>(imported.width) * imported.height,
        "index raster is not width * height");

    // This corpus is contiguous 1..14187, which is worth pinning: it was
    // NOT contiguous until the importer learned to read the twenty-four
    // quoted rows, and the twenty-four ocean provinces they hold looked
    // exactly like retired ids. A silent undercount of real provinces is
    // the failure this asserts against.
    Check(imported.sourceIdByIndex.size() == imported.Count() + 1,
        "dense index space has holes");
    bool ascending = true;
    for (std::size_t index = 2; index < imported.sourceIdByIndex.size();
        ++index)
    {
        ascending = ascending
            && imported.sourceIdByIndex[index]
                > imported.sourceIdByIndex[index - 1];
    }
    Check(ascending,
        "dense indices must be assigned in ascending corpus id, or generated "
        "content stops being reproducible");

    // --- colours that are not provinces ---
    //
    // The corpus disclaims 57 colours outright (a row with a colour and an
    // empty id) and those cover the open sea. Anything NOT in the table is a
    // different matter and is held to a far tighter bound: it means the raster
    // and the table have drifted, and at scale that would silently delete
    // provinces from the world.
    std::uint64_t unassignedPixels = 0;
    for (const adapter::NonProvinceColour& colour : imported.unassigned)
    {
        unassignedPixels += colour.pixels;
    }
    std::uint64_t unknownPixels = 0;
    for (const adapter::NonProvinceColour& colour : imported.unknown)
    {
        unknownPixels += colour.pixels;
    }
    // The corpus disclaims 57 colours; how many of them the raster actually
    // paints is a property of the map, not of the table.
    Check(imported.unassigned.size() <= 57,
        "more disclaimed colours appear in the raster than the table "
        "declares: " + std::to_string(imported.unassigned.size()));
    // White plus a handful of single-pixel artefacts. A regression that broke
    // colour matching would push this into the thousands immediately.
    Check(imported.unknown.size() <= 32,
        std::to_string(imported.unknown.size())
            + " colours are absent from the definition table entirely");
    const double unknownShare = 100.0 * static_cast<double>(unknownPixels)
        / static_cast<double>(imported.indexRaster.size());
    Check(unknownShare < 0.2,
        "colours absent from the table cover "
            + std::to_string(unknownShare) + "% of the raster");

    // --- the adjacency graph ---
    Check(!imported.adjacency.empty(), "no adjacency was derived");
    bool sortedUnique = true;
    bool ordered = true;
    bool inRange = true;
    for (std::size_t index = 0; index < imported.adjacency.size(); ++index)
    {
        const adapter::ProvinceAdjacency& edge = imported.adjacency[index];
        ordered = ordered && edge.first < edge.second;
        inRange = inRange
            && edge.first >= 1
            && edge.second <= imported.Count();
        if (index > 0)
        {
            const adapter::ProvinceAdjacency& previous =
                imported.adjacency[index - 1];
            sortedUnique = sortedUnique
                && (previous.first < edge.first
                    || (previous.first == edge.first
                        && previous.second < edge.second));
        }
    }
    Check(ordered, "every edge must be stored with first < second");
    Check(inRange, "an edge names an index outside 1..Count()");
    Check(sortedUnique,
        "adjacency must be sorted and deduplicated -- generated content has to "
        "be byte-identical across runs");

    // A world map's provinces have a handful of neighbours each. A degree far
    // outside that band means the scan is wrong: too low and borders are being
    // missed, too high and unrelated regions are being joined.
    std::map<std::uint32_t, std::uint32_t> degree;
    for (const adapter::ProvinceAdjacency& edge : imported.adjacency)
    {
        ++degree[edge.first];
        ++degree[edge.second];
    }
    const double averageDegree = degree.empty()
        ? 0.0
        : (2.0 * static_cast<double>(imported.adjacency.size()))
            / static_cast<double>(imported.Count());
    Check(averageDegree > 2.0 && averageDegree < 12.0,
        "average neighbour count " + std::to_string(averageDegree)
            + " is outside the plausible band for a province map");

    // Every province that occupies pixels must have at least one neighbour.
    // An isolated province is either a one-pixel artefact or a bug in the
    // scan, and either way the simulation graph would have an unreachable
    // node in it.
    std::set<std::uint16_t> painted;
    for (const std::uint16_t index : imported.indexRaster)
    {
        if (index != 0)
        {
            painted.insert(index);
        }
    }
    std::map<std::uint16_t, std::uint64_t> paintedPixels;
    for (const std::uint16_t index : imported.indexRaster)
    {
        if (index != 0)
        {
            ++paintedPixels[index];
        }
    }
    std::uint32_t isolated = 0;
    for (const std::uint16_t index : painted)
    {
        if (degree.find(index) == degree.end())
        {
            ++isolated;
        }
    }
    // Not zero, and it should not be.
    //
    // The reference corpus has nine: Yap, Ulithi, Wolelai, Truk, Majuro and
    // their neighbours -- Pacific atolls of 33 to 253 pixels each, surrounded
    // entirely by water the corpus disclaimed. They have no land neighbour
    // because they have no land neighbour. Sea connections are a separate
    // corpus file and a later concern.
    //
    // The bound still has teeth: it is the border scan itself that would push
    // this into the thousands if it broke, and an isolated node in the
    // simulation graph is unreachable, not merely unusual.
    Check(isolated <= 16,
        std::to_string(isolated)
            + " painted provinces have no neighbour, which is far more than "
              "the corpus's isolated atolls");

    // --- determinism ---
    //
    // Generated content is sealed by a content digest, so an import that
    // varied between runs would produce a Package that fails its own gate on
    // the second load. Hash-map iteration order is the obvious way for that to
    // happen, which is why the dense index is derived from a sort rather than
    // from traversal order.
    const adapter::ProvinceRasterImport again =
        adapter::ImportProvinceRaster(options);
    Check(static_cast<bool>(again), "the second import failed");
    Check(again.sourceIdByIndex == imported.sourceIdByIndex,
        "index assignment is not reproducible");
    Check(again.indexRaster == imported.indexRaster,
        "the index raster is not reproducible");
    Check(again.adjacency.size() == imported.adjacency.size(),
        "the adjacency graph is not reproducible");

    // --- the map is the right way up ---------------------------------
    //
    // Nothing else in this project can catch this. Every count, every
    // adjacency and every digest is invariant under a vertical mirror, so an
    // upside-down world imports cleanly, ticks correctly, saves and reloads,
    // and is simply wrong. It was found by looking at a window.
    //
    // The corpus's own bitmap is mirrored relative to geography, so the flag
    // that undoes it needs a check that knows which way is north. These six
    // provinces are the corpus's own landmarks; the ids are its, and if it
    // ever renumbers them this fails loudly rather than quietly.
    {
        struct Landmark
        {
            std::uint32_t sourceId;
            const char* name;
        };
        // Far north and far south, by name in definition.csv.
        const Landmark north[] = {
            {9, "Tromso"}, {59, "Murmansk"}, {8086, "Reykjavik"}
        };
        const Landmark south[] = {
            {8054, "Cape Town"}, {10496, "Punta Arenas"}, {10498, "Ushuaia"}
        };

        // Corpus id -> dense index, then dense index -> mean row.
        std::vector<std::uint64_t> rowSum(
            imported.sourceIdByIndex.size(), 0
        );
        std::vector<std::uint64_t> pixels(
            imported.sourceIdByIndex.size(), 0
        );
        for (std::uint32_t y = 0; y < imported.height; ++y)
        {
            const std::uint16_t* scan = imported.indexRaster.data()
                + static_cast<std::size_t>(y) * imported.width;
            for (std::uint32_t x = 0; x < imported.width; ++x)
            {
                const std::uint16_t index = scan[x];
                if (index != 0 && index < rowSum.size())
                {
                    rowSum[index] += y;
                    ++pixels[index];
                }
            }
        }
        const auto meanRow = [&](std::uint32_t sourceId) -> double
        {
            for (std::size_t index = 1;
                index < imported.sourceIdByIndex.size();
                ++index)
            {
                if (imported.sourceIdByIndex[index] == sourceId
                    && pixels[index] != 0)
                {
                    return static_cast<double>(rowSum[index])
                        / static_cast<double>(pixels[index]);
                }
            }
            return -1.0;
        };

        double northest = static_cast<double>(imported.height);
        double southest = -1.0;
        for (const Landmark& mark : north)
        {
            const double row = meanRow(mark.sourceId);
            Check(row >= 0.0,
                std::string("the corpus no longer has ") + mark.name);
            if (row >= 0.0)
            {
                northest = std::min(northest, row);
                Check(row < static_cast<double>(imported.height) * 0.35,
                    std::string(mark.name)
                        + " is not in the northern third of the raster");
            }
        }
        for (const Landmark& mark : south)
        {
            const double row = meanRow(mark.sourceId);
            Check(row >= 0.0,
                std::string("the corpus no longer has ") + mark.name);
            if (row >= 0.0)
            {
                southest = std::max(southest, row);
                Check(row > static_cast<double>(imported.height) * 0.65,
                    std::string(mark.name)
                        + " is not in the southern third of the raster");
            }
        }
        Check(southest > northest,
            "the map is upside down: the southern landmarks are above the "
            "northern ones");
    }

    if (failures != 0)
    {
        std::cerr << "province raster import: " << failures
                  << " failure(s)\n";
        return 3;
    }

    std::cout << "Province raster import: passed (" << imported.width << "x"
              << imported.height << ", " << imported.Count()
              << " provinces, " << imported.adjacency.size()
              << " derived adjacencies, average degree " << averageDegree
              << ", " << isolated << " isolated, "
              << imported.unassigned.size() << " disclaimed colours, "
              << imported.unknown.size() << " unknown, "
              << elapsed << " ms)\n";
    return 0;
}
