#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "authoring_pipeline.hpp"
#include "diagnostic.hpp"
#include "file_catalog.hpp"
#include "parser_registry.hpp"
#include "resolver.hpp"
#include "runtime_save_codec.hpp"
#include "template_registry.hpp"

// Parse and Resolve goldens -- the first two of the four observable results
// the Authoring DSL is frozen on. Compile is pinned separately by
// authoring_compile_golden_probe; Diagnostic by
// authoring_diagnostic_contract_probe.
//
// What each one is actually protecting:
//
//   Parse    Which files a Source Layer claims, what each is classified as,
//            and which layer wins when two claim the same virtual path. An
//            author adding a file with an unregistered extension, or a
//            template match quietly changing, shows up here and nowhere else
//            -- the Compile golden cannot see a file that was never parsed.
//
//   Resolve  The locked identity: Package Lock and Source Lock. This is what
//            a save is validated against and what the Ruleset Fingerprint is
//            taken over, so a drift here rejects existing saves.
//
// Both are taken over the Demo 0.5 vertical slice, because it is the fixture
// with five Source Layers and real cross-package structure. The coverage
// fixture behind the Compile golden is one layer and would not exercise layer
// ownership at all.

namespace
{
using namespace dillen;

class Encoder
{
public:
    void U8(std::uint8_t value) { bytes_.push_back(value); }

    void U32(std::uint32_t value)
    {
        for (int shift = 0; shift < 32; shift += 8)
        {
            bytes_.push_back(static_cast<std::uint8_t>(value >> shift));
        }
    }

    void U64(std::uint64_t value)
    {
        for (int shift = 0; shift < 64; shift += 8)
        {
            bytes_.push_back(static_cast<std::uint8_t>(value >> shift));
        }
    }

    void Text(const std::string& value)
    {
        U32(static_cast<std::uint32_t>(value.size()));
        for (const char character : value)
        {
            bytes_.push_back(static_cast<std::uint8_t>(character));
        }
    }

    const std::vector<std::uint8_t>& Bytes() const noexcept { return bytes_; }

private:
    std::vector<std::uint8_t> bytes_;
};

// Physical paths are deliberately NOT encoded: they differ between machines
// and would make the golden a property of the checkout directory rather than
// of the content. The virtual path is the identity content is authored
// against, and it is what the Source Lock records too.
void EncodeParse(Encoder& out, const parser::FileCatalog& catalog)
{
    out.Text("dillen.dsl.parse.v1");
    out.U32(static_cast<std::uint32_t>(catalog.Layers().size()));
    for (const parser::SourceLayer& layer : catalog.Layers())
    {
        out.U64(layer.id);
        out.Text(layer.name);
        out.U32(static_cast<std::uint32_t>(layer.priority));
    }
    out.U32(static_cast<std::uint32_t>(catalog.Files().size()));
    for (const parser::CatalogFile& file : catalog.Files())
    {
        out.Text(file.virtualPath);
        out.U64(file.sourceLayer);
        out.Text(file.sourceLayerName);
        out.U32(static_cast<std::uint32_t>(file.sourcePriority));
        out.U64(static_cast<std::uint64_t>(file.size));
        out.U64(file.fingerprint);
        out.U8(static_cast<std::uint8_t>(file.encoding));
        out.U8(static_cast<std::uint8_t>(file.disposition));
        out.U64(file.displacedByLayer);
        // The template match is the classification decision itself: which
        // registered authoring format claimed this file.
        out.U8(file.match.has_value() ? 1U : 0U);
        if (file.match.has_value())
        {
            out.U64(file.match->fileTemplate);
            out.U64(file.match->parser);
            out.U64(file.match->dialect);
            out.U32(static_cast<std::uint32_t>(file.match->priority));
            out.U64(static_cast<std::uint64_t>(file.match->specificity));
        }
    }
}

void EncodeResolve(
    Encoder& out,
    const kernel::PackageLock& packages,
    const kernel::SourceLock& sources
)
{
    out.Text("dillen.dsl.resolve.v1");
    out.U32(static_cast<std::uint32_t>(packages.Entries().size()));
    for (const kernel::PackageLockEntry& entry : packages.Entries())
    {
        out.U64(entry.package.value);
        out.Text(entry.canonicalName);
        out.U32(entry.version.major);
        out.U32(entry.version.minor);
        out.U32(entry.version.patch);
        out.Text(entry.contentDigest);
        out.U64(static_cast<std::uint64_t>(entry.loadIndex));
        out.U32(static_cast<std::uint32_t>(
            entry.providedCapabilities.size()));
        for (const kernel::CapabilityProvision& provision
            : entry.providedCapabilities)
        {
            out.U64(provision.capability.value);
            out.U32(provision.version);
        }
    }
    out.U32(static_cast<std::uint32_t>(sources.Entries().size()));
    for (const kernel::SourceLockEntry& entry : sources.Entries())
    {
        out.U64(entry.package.value);
        out.U32(entry.packageVersion.major);
        out.U32(entry.packageVersion.minor);
        out.U32(entry.packageVersion.patch);
        out.Text(entry.sourceLayer);
        out.Text(entry.virtualPath);
        out.U64(entry.fingerprint);
        out.U64(entry.size);
    }
}

}

int main()
{
    const std::string rootName = "dillen.demo05.root";
    authoring::AuthoringLaunchSelection selection;
    selection.root = {kernel::StableRulesetId(rootName), rootName, 1};
    selection.requireExplicitPackageRoles = true;
    authoring::AuthoringSession session(std::move(selection));

    parser::TemplateRegistry templates;
    parser::ParserRegistry parsers;
    parser::Resolver resolver;
    if (!session.Register(templates, parsers, resolver))
    {
        std::cerr << "frontend golden: registration failed\n";
        return 1;
    }
    templates.Freeze();
    parsers.Freeze();
    resolver.Freeze();

    const std::filesystem::path root = "Dillen-Game";
    parser::DiagnosticBag diagnostics;
    parser::FileCatalog fileCatalog;
    const bool layered =
        fileCatalog.AddLayer({1, "contracts", root / "demo_0_5/contracts", 0, {}})
        && fileCatalog.AddLayer({2, "economy", root / "economy/demo_0_5", 10, {}})
        && fileCatalog.AddLayer({3, "technology", root / "technology/demo_0_5", 20, {}})
        && fileCatalog.AddLayer({4, "production", root / "production/demo_0_5", 30, {}})
        && fileCatalog.AddLayer({5, "content", root / "demo_0_5/content", 100, {}});
    if (!layered || !fileCatalog.Build(templates, diagnostics))
    {
        std::cerr << "frontend golden: source catalog failed\n";
        return 2;
    }

    Encoder parseEncoder;
    EncodeParse(parseEncoder, fileCatalog);
    const std::vector<std::uint8_t>& parseBytes = parseEncoder.Bytes();
    const std::uint64_t parseChecksum =
        persistence::StableRuntimeChecksum(parseBytes);

    parser::ParseWorkspace workspace;
    if (!fileCatalog.Parse(parsers, workspace, diagnostics)
        || !resolver.Resolve(workspace, diagnostics))
    {
        for (const parser::Diagnostic& diagnostic : diagnostics.All())
        {
            std::cerr << parser::FormatDiagnostic(diagnostic) << '\n';
        }
        std::cerr << "frontend golden: parse/resolve failed\n";
        return 3;
    }

    const kernel::FrozenRuntimeCatalog& catalog = session.RuntimeCatalog();
    Encoder resolveEncoder;
    EncodeResolve(
        resolveEncoder,
        catalog.LockedPackages(),
        catalog.LockedSources()
    );
    const std::vector<std::uint8_t>& resolveBytes = resolveEncoder.Bytes();
    const std::uint64_t resolveChecksum =
        persistence::StableRuntimeChecksum(resolveBytes);

    // Both metrics on both goldens: three times now an injected defect in this
    // codebase has held the byte count and moved only the checksum.
    constexpr std::size_t kGoldenParseBytes = 3912;
    constexpr std::uint64_t kGoldenParseChecksum = 1278464547742860928ULL;
    constexpr std::size_t kGoldenResolveBytes = 3137;
    constexpr std::uint64_t kGoldenResolveChecksum = 15737711886577553487ULL;

    int failures = 0;
    if (parseBytes.size() != kGoldenParseBytes
        || parseChecksum != kGoldenParseChecksum)
    {
        std::cerr << "Parse result drifted:\n"
                  << "  bytes    : " << parseBytes.size()
                  << " (expected " << kGoldenParseBytes << ")\n"
                  << "  checksum : " << parseChecksum
                  << " (expected " << kGoldenParseChecksum << ")\n"
                  << "A file changed extension, classification, layer or\n"
                  << "displacement. If deliberate, the authoring surface\n"
                  << "changed and existing Packages need a migration note.\n";
        ++failures;
    }
    if (resolveBytes.size() != kGoldenResolveBytes
        || resolveChecksum != kGoldenResolveChecksum)
    {
        std::cerr << "Resolve result drifted:\n"
                  << "  bytes    : " << resolveBytes.size()
                  << " (expected " << kGoldenResolveBytes << ")\n"
                  << "  checksum : " << resolveChecksum
                  << " (expected " << kGoldenResolveChecksum << ")\n"
                  << "Package Lock or Source Lock identity moved. Every\n"
                  << "existing save validates against this, so a deliberate\n"
                  << "change needs a version bump and a migration.\n";
        ++failures;
    }
    if (failures != 0)
    {
        return 4;
    }

    std::cout << "Authoring frontend goldens: passed ("
              << fileCatalog.Layers().size() << " layers, "
              << fileCatalog.ActiveClassifiedFileCount()
              << " classified files, " << parseBytes.size()
              << " parse bytes, " << resolveBytes.size()
              << " resolve bytes)\n";
    return 0;
}
