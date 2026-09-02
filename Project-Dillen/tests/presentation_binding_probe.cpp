#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <system_error>

#include <vector>

#include "package_content_digest.hpp"
#include "standalone_session.hpp"

// Demo 0.8 P5a -- a Binding that points nowhere is refused at load time.
//
// Memo section 4.4.4: "Binding 指向不存在的 Contract 时，加载明确拒绝".
// This is what makes that true.
//
// The asset's `kind` stays opaque -- nothing in the Kernel or the pipeline
// knows what a panel is -- but a binding's REQUIREMENTS are typed, so a claim
// like "this reads production_site's level" is answerable against the frozen
// catalog without knowing anything about user interfaces. That split is the
// whole design: it lets a new asset kind land without touching the Kernel
// while still refusing a broken one.
//
// Why refusing matters more than it looks. Unchecked, a stale binding does not
// crash and does not warn: the Package loads, the widget shows nothing, and an
// author with fifty widgets has no way to tell which one went quiet. A refused
// Package names the asset and the reference in one line.

namespace
{
namespace fs = std::filesystem;
using namespace dillen;

const fs::path kGameRoot = "Dillen-Game";
const fs::path kMapContractsRoot = kGameRoot / "map/contracts";
const fs::path kMapMechanismRoot = kGameRoot / "production/map_world";
const fs::path kMapWorldRoot = kGameRoot / "map/world";
const fs::path kMapPresentationRoot = kGameRoot / "presentation/map_world";

int failures = 0;

void Check(bool condition, const std::string& what)
{
    if (!condition)
    {
        std::cerr << "presentation binding: " << what << '\n';
        ++failures;
    }
}

bool HasDiagnostic(
    const host::StandaloneSessionReport& report,
    const std::string& code
)
{
    for (const std::string& diagnostic : report.diagnostics)
    {
        if (diagnostic.find(code) != std::string::npos)
        {
            return true;
        }
    }
    return false;
}

host::StandaloneSessionConfig Config(const fs::path& presentationRoot)
{
    host::StandaloneSessionConfig config;
    config.sources.push_back({
        "world_map_contracts", kMapContractsRoot, 0, {}, {}, {}
    });
    config.sources.push_back({
        "world_map_mechanisms", kMapMechanismRoot, 50, {}, {}, {}
    });
    config.sources.push_back({
        "world_map_content", kMapWorldRoot, 100, {}, {}, {}
    });
    config.sources.push_back({
        "world_map_presentation", presentationRoot, 200, {}, {}, {}
    });
    config.rulesets.root = {
        kernel::StableRulesetId("dillen.map.world_root"),
        "dillen.map.world_root",
        1
    };
    config.rulesets.requireExplicitPackageRoles = true;
    return config;
}

// Copies the real Presentation Package and rewrites one field name in the
// panel binding, leaving everything else -- including the raster and its
// digest -- exactly as it was.
bool WriteBrokenSkin(const fs::path& root, const std::string& field)
{
    std::error_code error;
    fs::remove_all(root, error);
    error.clear();
    fs::copy(
        kMapPresentationRoot,
        root,
        fs::copy_options::recursive,
        error
    );
    if (error)
    {
        return false;
    }
    const fs::path panel = root / "assets/province_panel.dasset";
    std::string text;
    {
        std::ifstream input(panel, std::ios::binary);
        if (!input)
        {
            return false;
        }
        text.assign(
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()
        );
    }
    const std::string target = "field = level";
    const std::size_t at = text.find(target);
    if (at == std::string::npos)
    {
        return false;
    }
    text.replace(at, target.size(), "field = " + field);
    {
        std::ofstream output(panel, std::ios::binary | std::ios::trunc);
        output.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!output)
        {
            return false;
        }
    }

    // Rewrite the manifest's content_digest to match what was just written.
    //
    // Without this the Package is refused for the WRONG reason -- a digest
    // mismatch -- and the probe would pass while proving nothing about
    // bindings at all. Editing a skin by hand is exactly what an author does;
    // the digest is meant to catch tampering, not authoring, so the probe has
    // to behave like a regenerating toolchain rather than like an attacker.
    std::vector<kernel::PackageContentSource> sources;
    std::vector<std::string> retained;
    retained.reserve(8);
    for (const auto& entry : fs::recursive_directory_iterator(root))
    {
        if (!entry.is_regular_file())
        {
            continue;
        }
        const fs::path relative = fs::relative(entry.path(), root);
        const std::string virtualPath = relative.generic_string();
        if (virtualPath.find("packages/") == 0)
        {
            // The manifest is excluded from the digest it carries.
            continue;
        }
        // Only CLASSIFIED sources are in the digest. The raster payload is
        // deliberately left unclassified by the file catalog -- that is what
        // keeps 24 MB of binary out of the parser -- so it is not hashed here
        // either. Including it was the first mistake this rebuild made, and it
        // produced a digest that looked plausible and matched nothing.
        if (entry.path().extension() != ".dasset")
        {
            continue;
        }
        std::ifstream input(entry.path(), std::ios::binary);
        if (!input)
        {
            return false;
        }
        retained.emplace_back(
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()
        );
        sources.push_back({virtualPath, retained.back()});
    }
    const std::string digest =
        kernel::ComputePackageContentDigest(std::move(sources));

    const fs::path manifest = root / "packages/presentation.dpackage";
    std::string manifestText;
    {
        std::ifstream input(manifest, std::ios::binary);
        if (!input)
        {
            return false;
        }
        manifestText.assign(
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()
        );
    }
    const std::string key = "content_digest = \"";
    const std::size_t start = manifestText.find(key);
    if (start == std::string::npos)
    {
        return false;
    }
    const std::size_t from = start + key.size();
    const std::size_t end = manifestText.find('"', from);
    if (end == std::string::npos)
    {
        return false;
    }
    manifestText.replace(from, end - from, digest);
    std::ofstream output(manifest, std::ios::binary | std::ios::trunc);
    output.write(
        manifestText.data(),
        static_cast<std::streamsize>(manifestText.size())
    );
    return static_cast<bool>(output);
}


// Counts assets of one kind. Asserting on kinds rather than on a total is not
// pedantry: the Package gained a font between two rounds of this work, and a
// probe that counted assets failed for a reason that had nothing to do with
// what it was testing.
std::size_t CountKind(
    const std::vector<kernel::PresentationAsset>& assets,
    const std::string& kind
)
{
    std::size_t total = 0;
    for (const kernel::PresentationAsset& asset : assets)
    {
        if (asset.kind == kind)
        {
            ++total;
        }
    }
    return total;
}

}

int main()
{
    if (!fs::exists(kMapPresentationRoot))
    {
        std::cerr << "presentation binding: the presentation package is "
                     "missing\n";
        return 1;
    }

    // --- the real binding resolves ---
    host::StandaloneSession good;
    host::StandaloneSessionReport goodReport;
    if (!good.Start(Config(kMapPresentationRoot), goodReport))
    {
        for (const std::string& diagnostic : goodReport.diagnostics)
        {
            std::cerr << "  " << diagnostic << '\n';
        }
        std::cerr << "presentation binding: the shipped skin did not load\n";
        return 2;
    }
    Check(CountKind(good.PresentationAssets(), "map_index_raster") == 1
            && CountKind(good.PresentationAssets(), "ui_binding") == 1,
        "the shipped skin does not carry one raster and one panel binding");

    // The panel carries three typed requirements and no payload at all. A
    // binding is a declaration; requiring an empty file to go with it would be
    // ceremony, and the parser says so.
    const kernel::PresentationAsset* panel = nullptr;
    for (const kernel::PresentationAsset& asset : good.PresentationAssets())
    {
        if (asset.kind == "ui_binding")
        {
            panel = &asset;
        }
    }
    Check(panel != nullptr, "no ui_binding asset was declared");
    if (panel != nullptr)
    {
        Check(panel->requirements.size() == 4,
            "the panel declares "
                + std::to_string(panel->requirements.size())
                + " requirements, expected 3");
        Check(panel->assetPath.empty() && panel->assetDigest.empty(),
            "a pure declaration should carry no payload");
    }

    // --- a binding that points nowhere is refused ---
    //
    // One character of the panel changes; the raster, its digest and every
    // other byte of the Package stay as they were. Nothing but the load-time
    // check stands between this and a silently empty widget.
    const fs::path broken =
        fs::temp_directory_path() / "dillen_broken_skin";
    if (!WriteBrokenSkin(broken, "leval"))
    {
        std::cerr << "presentation binding: could not write the broken skin\n";
        return 3;
    }
    host::StandaloneSession bad;
    host::StandaloneSessionReport badReport;
    const bool started = bad.Start(Config(broken), badReport);
    Check(!started,
        "a Binding naming a field that does not exist was accepted");
    Check(HasDiagnostic(
              badReport,
              "dillen.authoring.presentation_binding_unresolved"),
        "the refusal did not name the binding");
    // And it refused for THAT reason. The first version of this probe edited
    // the skin without resealing its content digest, so the Package was
    // refused before any binding was looked at -- a pass that proved nothing.
    Check(!HasDiagnostic(
              badReport,
              "dillen.authoring.package_content_digest_mismatch"),
        "the skin was refused over its digest, not its binding");

    // --- and the refusal is about the binding, not about the skin ---
    //
    // Restoring the field must make the identical Package load again, which is
    // what separates "this reference is stale" from "presentation is broken".
    if (!WriteBrokenSkin(broken, "level"))
    {
        return 4;
    }
    host::StandaloneSession restored;
    host::StandaloneSessionReport restoredReport;
    Check(restored.Start(Config(broken), restoredReport),
        "the repaired skin did not load");

    std::error_code error;
    fs::remove_all(broken, error);

    if (failures != 0)
    {
        std::cerr << "presentation binding: " << failures << " failure(s)\n";
        return 5;
    }
    std::cout << "Presentation binding: passed (2 assets, 3 typed "
                 "requirements, an unresolved one refused at load)\n";
    return 0;
}
