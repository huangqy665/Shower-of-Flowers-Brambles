#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <memory>
#include <system_error>

#include "presentation_view.hpp"
#include "standalone_session.hpp"

// Demo 0.8 P0 -- the presentation boundary.
//
// Presentation is the first layer that is allowed to be sloppy, and that is
// exactly why it needs the tightest boundary in the engine. A map renderer is
// a pure function of a published snapshot: it never writes, never blocks the
// tick, and never becomes something the simulation reads back. Three separate
// mechanisms are supposed to hold that, and this probe asserts all three so
// that "presentation is deletable" is a checked property instead of an
// intention.
//
//   1. Module direction   -- architecture_guard_probe: presentation may reach
//                            kernel/world/runtime, nothing may reach it.
//   2. Build separation   -- src/CMakeLists.txt keeps dillen::presentation out
//                            of dillen::standalone.
//   3. Content separation -- THIS probe: a Presentation Package cannot enter
//                            the determinism closure, so the Ruleset
//                            Fingerprint is identical whether one is present
//                            or not. Every Package Lock entry is hashed into
//                            that fingerprint, and a save validates against
//                            it, so a Presentation Package inside the closure
//                            would put the map skin into the save identity:
//                            change the skin, existing saves stop loading.
//
// It also pins the read-side handle behaviour, because a frame that steps
// backwards in world time is the one way a correct snapshot can still show a
// wrong world.

namespace
{
namespace fs = std::filesystem;
using namespace dillen;

bool HasDiagnostic(
    const host::StandaloneSessionReport& report,
    const std::string& code
)
{
    return std::any_of(
        report.diagnostics.begin(),
        report.diagnostics.end(),
        [&code](const std::string& diagnostic)
        {
            return diagnostic.find(code) != std::string::npos;
        }
    );
}

host::StandaloneSessionConfig DemoConfig(
    const fs::path& presentationSource = {}
)
{
    host::StandaloneSessionConfig config;
    config.sources.push_back({
        "demo05_contracts", "Dillen-Game/contracts/demo_0_5", 0, {}, {}, {}
    });
    config.sources.push_back({
        "demo05_economy", "Dillen-Game/packages/economy", 10, {}, {}, {}
    });
    config.sources.push_back({
        "demo05_technology", "Dillen-Game/packages/technology", 20, {}, {}, {}
    });
    config.sources.push_back({
        "demo05_production", "Dillen-Game/packages/production", 30, {}, {}, {}
    });
    config.sources.push_back({
        "demo05_content", "Dillen-Game/content/demo_0_5", 100, {}, {}, {}
    });
    if (!presentationSource.empty())
    {
        config.sources.push_back({
            "demo05_presentation", presentationSource, 200, {}, {}, {}
        });
    }
    config.rulesets.root = {
        kernel::StableRulesetId("dillen.demo05.root"),
        "dillen.demo05.root",
        1
    };
    config.rulesets.requireExplicitPackageRoles = true;
    return config;
}

// Writes a Presentation Package that carries nothing but its manifest, which
// is all a Presentation Package may own until the first presentation artifact
// type lands with real map data.
bool WritePresentationPackage(const fs::path& root, bool required)
{
    std::error_code error;
    fs::remove_all(root, error);
    error.clear();
    fs::create_directories(root / "packages", error);
    if (error)
    {
        return false;
    }
    std::ofstream manifest(
        root / "packages/skin.dpackage",
        std::ios::binary
    );
    if (!manifest)
    {
        return false;
    }
    manifest
        << "package_manifest = {\n"
        << "    name = dillen.demo05.skin\n"
        << "    version_major = 1\n"
        << "    version_minor = 0\n"
        << "    version_patch = 0\n"
        << "    role = presentation\n"
        // The digest of a Package with zero content sources. A Presentation
        // Package may own only its manifest today, and the manifest is
        // excluded from the digest, so every such package has this one.
        << "    content_digest = \""
        << "8d75e63208cc46d868bc90a4a1b0fe96adfff33c5392be7427f231d29cbbfc84"
        << "\"\n"
        << "    load_priority = 200\n";
    if (required)
    {
        // The illegal shape: an authoritative Package declaring a dependency
        // on presentation. The Ruleset-requirement route is the other way in
        // and is refused by the same diagnostic.
        manifest
            << "    dependencies = {\n"
            << "        dependency = {\n"
            << "            name = dillen.demo05.contracts\n"
            << "            minimum_major = 1\n"
            << "            minimum_minor = 0\n"
            << "            minimum_patch = 0\n"
            << "            maximum_major = 2\n"
            << "            maximum_minor = 0\n"
            << "            maximum_patch = 0\n"
            << "            required = yes\n"
            << "        }\n"
            << "    }\n";
    }
    manifest << "}\n";
    return true;
}

// A Presentation Package must not move the Ruleset Fingerprint.
bool FingerprintIsIndifferentToPresentation()
{
    const fs::path skin =
        fs::temp_directory_path() / "dillen_p0_presentation_skin";
    if (!WritePresentationPackage(skin, false))
    {
        std::cerr << "presentation boundary: could not write the skin\n";
        return false;
    }

    host::StandaloneSession without;
    host::StandaloneSessionReport withoutReport;
    host::StandaloneSession with;
    host::StandaloneSessionReport withReport;
    const bool bothStarted =
        without.Start(DemoConfig(), withoutReport)
        && with.Start(DemoConfig(skin), withReport);

    std::error_code error;
    if (!bothStarted)
    {
        for (const std::string& diagnostic : withReport.diagnostics)
        {
            std::cerr << "  " << diagnostic << '\n';
        }
        std::cerr << "presentation boundary: a session failed to start\n";
        fs::remove_all(skin, error);
        return false;
    }

    const std::string bare = without.Catalog().Fingerprint().ToHex();
    const std::string skinned = with.Catalog().Fingerprint().ToHex();
    const std::size_t bareLock = without.Catalog().LockedPackages().Size();
    const std::size_t skinnedLock = with.Catalog().LockedPackages().Size();
    fs::remove_all(skin, error);

    if (bare != skinned)
    {
        std::cerr << "presentation boundary: the Ruleset Fingerprint moved "
                     "when a Presentation Package was loaded (" << bare
                  << " vs " << skinned << ")\n";
        return false;
    }
    // Belt and braces: the fingerprint could only have stayed put because the
    // Package Lock did, and saying so separately makes a future change that
    // adds the package but cancels out in the hash impossible to miss.
    if (bareLock != skinnedLock)
    {
        std::cerr << "presentation boundary: a Presentation Package entered "
                     "the Package Lock (" << bareLock << " vs " << skinnedLock
                  << ")\n";
        return false;
    }
    return true;
}

// Depending on a Presentation Package is refused at load time.
bool RejectsDependencyOnPresentation()
{
    const fs::path skin =
        fs::temp_directory_path() / "dillen_p0_presentation_required";
    if (!WritePresentationPackage(skin, true))
    {
        return false;
    }
    // The dependency is declared BY the presentation package here, which is
    // the shape a skin author would reach for first: "my skin needs the
    // contracts". It is still refused -- a Presentation Package that
    // participates in the dependency graph at all can be pulled into the
    // closure by it.
    host::StandaloneSession session;
    host::StandaloneSessionReport report;
    const bool started = session.Start(DemoConfig(skin), report);
    const bool rejected = !started && HasDiagnostic(
        report,
        "dillen.authoring.presentation_package_not_authoritative"
    );
    std::error_code error;
    fs::remove_all(skin, error);
    if (!rejected)
    {
        std::cerr << "presentation boundary: a Presentation Package in the "
                     "dependency graph was accepted\n";
    }
    return rejected;
}

// The read handle must not step backwards in world time.
bool ViewOnlyMovesForward()
{
    host::StandaloneSession session;
    host::StandaloneSessionReport report;
    if (!session.Start(DemoConfig(), report))
    {
        return false;
    }

    presentation::PresentationView view;
    if (view.IsBound() || view.Stamp().publication != 0)
    {
        std::cerr << "presentation boundary: a default view claims to be "
                     "bound\n";
        return false;
    }
    if (view.Advance(nullptr))
    {
        std::cerr << "presentation boundary: a null snapshot was accepted\n";
        return false;
    }

    // Two publications, taken a tick apart.
    if (!session.Runtime().RunTick(1))
    {
        return false;
    }
    const runtime::WorldQuerySnapshotHandle first =
        std::make_shared<const runtime::WorldQuerySnapshot>(
            session.Runtime().Query()
        );
    if (!session.Runtime().RunTick(2))
    {
        return false;
    }
    const runtime::WorldQuerySnapshotHandle second =
        std::make_shared<const runtime::WorldQuerySnapshot>(
            session.Runtime().Query()
        );
    if (first->Stamp().publication >= second->Stamp().publication)
    {
        std::cerr << "presentation boundary: publication did not advance "
                     "between ticks\n";
        return false;
    }

    if (!view.Advance(second) || !view.IsBound())
    {
        std::cerr << "presentation boundary: a published snapshot was "
                     "refused\n";
        return false;
    }
    // The older snapshot arriving late must not un-happen the world.
    if (view.Advance(first)
        || view.Stamp().publication != second->Stamp().publication)
    {
        std::cerr << "presentation boundary: the view stepped backwards\n";
        return false;
    }
    // Re-offering the same publication is not progress either.
    if (view.Advance(second))
    {
        std::cerr << "presentation boundary: the view re-accepted its own "
                     "snapshot\n";
        return false;
    }
    view.Reset();
    if (view.IsBound())
    {
        return false;
    }
    return true;
}

}

int main()
{
    if (!FingerprintIsIndifferentToPresentation())
    {
        return 1;
    }
    if (!RejectsDependencyOnPresentation())
    {
        return 2;
    }
    if (!ViewOnlyMovesForward())
    {
        return 3;
    }
    std::cout << "Presentation boundary: passed (Ruleset Fingerprint "
                 "indifferent to a Presentation Package, dependency on one "
                 "refused, read handle monotonic)\n";
    return 0;
}
