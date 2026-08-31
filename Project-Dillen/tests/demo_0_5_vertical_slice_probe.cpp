#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>

#include "authoring_pipeline.hpp"
#include "diagnostic.hpp"
#include "file_catalog.hpp"
#include "initial_world_builder.hpp"
#include "kernel_runtime.hpp"
#include "parser_registry.hpp"
#include "resolver.hpp"
#include "template_registry.hpp"

// Demo 0.5 vertical slice: economy, research and production as real content.
//
// This is the model that was used to reverse-derive DSL v1, written out in
// full. Before that work every line of it was inexpressible -- the language
// had no way to read a value at run time, so `output = ore * infra_level`,
// `progress >= cost` and `sum over owned provinces` were all impossible and
// the draft of this file was a list of "cannot express" annotations.
//
// It is also the first fixture built to the Package role split (memo section
// 1.2.2), with five Source Layers:
//
//   contracts   Contract Package    public ABI only: Capability Contracts,
//                                   shared Component Schemas, Relation Schema
//   production  Mechanism Package   implementation
//   economy     Mechanism Package   implementation
//   research    Mechanism Package   implementation
//   content     Content Package     concrete world: entities, relations,
//                                   definitions, spawns, the Root Ruleset
//
// The three Mechanism Packages do not reference each other. They meet only
// through the Contract Package's Component Schemas and Relation Schema, which
// is the property the split exists to enforce.

namespace
{
using namespace dillen;

int failures = 0;

void Expect(bool condition, const std::string& what)
{
    if (!condition)
    {
        std::cerr << "demo 0.5: " << what << '\n';
        ++failures;
    }
}

}

int main()
{
    const std::string rootName = "dillen.g.root";
    authoring::AuthoringLaunchSelection selection;
    selection.root = {kernel::StableRulesetId(rootName), rootName, 1};
    authoring::AuthoringSession session(std::move(selection));

    parser::TemplateRegistry templates;
    parser::ParserRegistry parsers;
    parser::Resolver resolver;
    if (!session.Register(templates, parsers, resolver))
    {
        std::cerr << "demo 0.5: registration failed\n";
        return 1;
    }
    templates.Freeze();
    parsers.Freeze();
    resolver.Freeze();

    const std::filesystem::path root =
        "Project-Dillen/tests/fixtures/dillen_demo_0_5";
    parser::DiagnosticBag diagnostics;
    parser::FileCatalog fileCatalog;
    const bool layered =
        fileCatalog.AddLayer({1, "contracts", root / "contracts", 0, {}})
        && fileCatalog.AddLayer({2, "production", root / "production", 10, {}})
        && fileCatalog.AddLayer({3, "economy", root / "economy", 20, {}})
        && fileCatalog.AddLayer({4, "research", root / "research", 30, {}})
        && fileCatalog.AddLayer({5, "content", root / "content", 100, {}});
    if (!layered || !fileCatalog.Build(templates, diagnostics))
    {
        for (const parser::Diagnostic& diagnostic : diagnostics.All())
        {
            std::cerr << parser::FormatDiagnostic(diagnostic) << '\n';
        }
        std::cerr << "demo 0.5: source catalog failed\n";
        return 2;
    }

    parser::ParseWorkspace workspace;
    if (!fileCatalog.Parse(parsers, workspace, diagnostics)
        || !resolver.Resolve(workspace, diagnostics))
    {
        for (const parser::Diagnostic& diagnostic : diagnostics.All())
        {
            std::cerr << parser::FormatDiagnostic(diagnostic) << '\n';
        }
        std::cerr << "demo 0.5: parse/resolve failed\n";
        return 3;
    }

    const kernel::FrozenRuntimeCatalog& catalog = session.RuntimeCatalog();
    world::AuthoritativeWorld world;
    world::InitialWorldBuildReport buildReport;
    if (!catalog.IsFrozen()
        || !world::InitialWorldBuilder{}.Build(catalog, world, buildReport))
    {
        std::cerr << "demo 0.5: world build failed\n";
        return 4;
    }

    // Five Packages, each owning exactly one Source Layer, all locked.
    Expect(catalog.LockedPackages().Size() == 5,
        "five Packages should be locked");
    Expect(catalog.LockedSources().Size() == 28,
        "every authoring source should be in the Source Lock, got "
            + std::to_string(catalog.LockedSources().Size()));

    runtime::KernelRuntime kernelRuntime(world, catalog);
    constexpr std::uint64_t kTicks = 5;
    for (std::uint64_t tick = 1; tick <= kTicks; ++tick)
    {
        if (!kernelRuntime.RunTick(tick)
            || kernelRuntime.LastTickAlgorithms().FailedCount() != 0
            || kernelRuntime.LastCreateAlgorithms().FailedCount() != 0)
        {
            for (const runtime::AlgorithmInvocationResult& invocation
                : kernelRuntime.LastTickAlgorithms().invocations)
            {
                if (!invocation)
                {
                    std::cerr << "  fault: " << invocation.message << '\n';
                }
            }
            std::cerr << "demo 0.5: tick " << tick << " failed\n";
            return 5;
        }
    }

    const kernel::MechanismQuerySnapshot& mechanisms =
        kernelRuntime.Query().Mechanisms();
    const auto field = [&](const char* mechanism,
                           const char* definitionName,
                           const char* fieldName,
                           kernel::MechanismValue& out)
    {
        const kernel::MechanismDefinitionId definition =
            kernel::StableMechanismDefinitionId(
                kernel::StableMechanismTypeId(mechanism),
                definitionName
            );
        const auto slot =
            catalog.ResolveDefinitionFieldSlot(definition, fieldName);
        if (!slot) return false;
        const kernel::MechanismValue* value = mechanisms.FindField(
            kernel::StableMechanismInstanceId(definition, 0),
            *slot
        );
        if (value == nullptr) return false;
        out = *value;
        return true;
    };

    kernel::MechanismValue output;
    kernel::MechanismValue income;
    kernel::MechanismValue provinces;
    kernel::MechanismValue balance;
    kernel::MechanismValue solvent;
    kernel::MechanismValue progress;
    kernel::MechanismValue completed;
    const bool read =
        field("dillen.g.production_site", "dillen.g.site_north",
              "goods_output", output)
        && field("dillen.g.national_budget", "dillen.g.alvara_budget",
                 "income", income)
        && field("dillen.g.national_budget", "dillen.g.alvara_budget",
                 "provinces", provinces)
        && field("dillen.g.national_budget", "dillen.g.alvara_budget",
                 "balance", balance)
        && field("dillen.g.national_budget", "dillen.g.alvara_budget",
                 "solvent", solvent)
        && field("dillen.g.research_project", "dillen.g.metallurgy",
                 "progress", progress)
        && field("dillen.g.research_project", "dillen.g.metallurgy",
                 "completed", completed);
    if (!read)
    {
        std::cerr << "demo 0.5: fields unavailable\n";
        return 6;
    }

    // Production. north_reach has ore 12, infra_level 1.50, capacity 20.
    // 12 * 1.50 = 18, and min(18, 20) = 18.
    Expect(output == kernel::MechanismValue(18.0),
        "goods_output should be min(12 * 1.50, 20) = 18");

    // Economy. Alvara owns north_reach (ore 12) and south_vale (ore 5), so the
    // sum across the owns Relation is 17 and the count is 2.
    Expect(income == kernel::MechanismValue(17.0),
        "income should be the summed ore across owned provinces = 17");
    Expect(provinces == kernel::MechanismValue(std::int64_t{2}),
        "provinces should count both owned provinces");

    // balance accumulates income - upkeep = 17 - 4 = 13 per tick, five times.
    Expect(balance == kernel::MechanismValue(65.0),
        "balance should have integrated to 5 * (17 - 4) = 65");
    Expect(solvent == kernel::MechanismValue(true),
        "a positive balance should stay solvent");

    // Research. Alvara's treasury.science is 3, so progress reaches 15 in five
    // ticks and passes the cost of 12 on the fourth.
    Expect(progress == kernel::MechanismValue(std::int64_t{15}),
        "progress should have accumulated 5 * 3 = 15");
    Expect(completed == kernel::MechanismValue(true),
        "metallurgy should have completed once progress passed cost");

    if (failures != 0)
    {
        std::cerr << "demo 0.5: " << failures << " failure(s)\n";
        return 7;
    }

    std::cout << "Demo 0.5 vertical slice: passed ("
              << catalog.LockedPackages().Size() << " Packages, "
              << catalog.LockedSources().Size() << " locked sources, "
              << mechanisms.Size() << " instances x " << kTicks
              << " ticks; production, economy and research all evaluated "
                 "through read paths)\n";
    return 0;
}
