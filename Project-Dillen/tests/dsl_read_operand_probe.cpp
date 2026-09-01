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

// End-to-end check for the DSL v1 read operands, written as the economy /
// research / production model that motivated them. Before this work none of
// these lines could be expressed at all: the only operands were a compile-time
// constant and the scheduled event payload, so the language could say "add 1"
// and nothing else.
//
// Each assertion below corresponds to one line of that model, and to one gap
// in the table that came out of the reverse-derivation:
//
//   output    = home.ore * efficiency   cross-object read + multiplication
//   output    = min(output, 50)         binary min
//   progress += 1                       accumulation through the new path
//   completed = progress >= cost        ordered comparison, field on both sides
//   income    = sum over a Relation     aggregation
//   reach     = count of a role fan-out counting reducer

int main()
{
    using namespace dillen;

    const std::string rootName = "dillen.eco.root";
    authoring::AuthoringLaunchSelection selection;
    selection.root = {kernel::StableRulesetId(rootName), rootName, 1};
    authoring::AuthoringSession session(std::move(selection));

    parser::TemplateRegistry templates;
    parser::ParserRegistry parsers;
    parser::Resolver resolver;
    if (!session.Register(templates, parsers, resolver))
    {
        std::cerr << "read operand probe: registration failed\n";
        return 1;
    }
    templates.Freeze();
    parsers.Freeze();
    resolver.Freeze();

    parser::DiagnosticBag diagnostics;
    parser::FileCatalog fileCatalog;
    if (!fileCatalog.AddLayer({
            1,
            "dillen_dsl_v2",
            std::filesystem::path("Project-Dillen/tests/fixtures/dillen_dsl_v2"),
            0,
            {}
        })
        || !fileCatalog.Build(templates, diagnostics))
    {
        std::cerr << "read operand probe: source catalog failed\n";
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
        std::cerr << "read operand probe: parse/resolve failed\n";
        return 3;
    }

    const kernel::FrozenRuntimeCatalog& catalog = session.RuntimeCatalog();
    world::AuthoritativeWorld world;
    world::InitialWorldBuildReport buildReport;
    if (!catalog.IsFrozen()
        || !world::InitialWorldBuilder{}.Build(catalog, world, buildReport))
    {
        std::cerr << "read operand probe: world build failed\n";
        return 4;
    }

    runtime::KernelRuntime kernelRuntime(world, catalog);
    // Four ticks: the surviving keeper is due on 3, and its delivery lands
    // on the Event stage of the tick after it becomes due.
    for (std::uint64_t tick = 1; tick <= 4; ++tick)
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
                    std::cerr << "  tick fault: " << invocation.message << '\n';
                }
            }
            std::cerr << "read operand probe: tick " << tick << " failed\n";
            return 5;
        }
    }

    const kernel::MechanismDefinitionId definition =
        kernel::StableMechanismDefinitionId(
            kernel::StableMechanismTypeId("dillen.eco.site"),
            "dillen.eco.site_definition"
        );
    const kernel::MechanismInstanceId instance =
        kernel::StableMechanismInstanceId(definition, 0);
    const kernel::MechanismQuerySnapshot& mechanisms =
        kernelRuntime.Query().Mechanisms();

    const auto readField = [&](const char* name, kernel::MechanismValue& out)
    {
        const auto slot = catalog.ResolveDefinitionFieldSlot(definition, name);
        if (!slot) return false;
        const kernel::MechanismValue* value =
            mechanisms.FindField(instance, *slot);
        if (value == nullptr) return false;
        out = *value;
        return true;
    };

    kernel::MechanismValue output;
    kernel::MechanismValue progress;
    kernel::MechanismValue completed;
    kernel::MechanismValue income;
    kernel::MechanismValue reach;
    kernel::MechanismValue pulses;
    if (!readField("output", output)
        || !readField("progress", progress)
        || !readField("completed", completed)
        || !readField("income", income)
        || !readField("reach", reach)
        || !readField("pulses", pulses))
    {
        std::cerr << "read operand probe: fields unavailable\n";
        return 6;
    }

    int failures = 0;
    const auto expect = [&failures](bool ok, const std::string& what)
    {
        if (!ok)
        {
            std::cerr << "read operand probe: " << what << '\n';
            ++failures;
        }
    };

    // home is bound to the capital, whose stock.ore is 40; efficiency is 1.5.
    // 40 * 1.5 = 60, then min(60, 50) = 50.
    expect(output == kernel::MechanismValue(50.0),
        "output should be min(40 * 1.5, 50) = 50");

    // Four ticks of accumulation through a computed add.
    expect(progress == kernel::MechanismValue(std::int64_t{4}),
        "progress should have accumulated to 4");

    // cost is 30 and progress is 3, so the ordered comparison must be false --
    // and it must be evaluated at all, which the old `==`-only condition could
    // not have done against a field.
    expect(completed == kernel::MechanismValue(false),
        "completed should still be false at progress 4 of 30");

    // The capital supplies one outpost, whose stock.tax is 3.25. Summing over
    // the Relation reaches exactly that one value.
    expect(income == kernel::MechanismValue(3.25),
        "income should be the summed tax across the supplies Relation");

    // cancel_events. Create schedules two `pulse` events and one `keeper`, due
    // on ticks 2, 3 and 4; the Tick stage cancels every pending `pulse` on
    // tick 1, before either comes due. Only the keeper survives, so `pulses`
    // -- which any delivered event increments -- ends at 1. Removing the
    // cancel_events line makes it 3, which is what makes this assertion worth
    // having.
    //
    // The due ticks are staggered deliberately. Every invocation in one Event
    // phase reads the same immutable snapshot, so three deliveries in a single
    // phase would each compute 0+1 and commit 1 -- identical to one delivery,
    // and the assertion would hold whether or not cancellation worked. The
    // first version of this test had exactly that flaw and passed with the
    // feature removed.
    expect(pulses == kernel::MechanismValue(std::int64_t{1}),
        "cancel_events should remove both pulses and leave the keeper");

    // The role binds one entity, so counting the fan-out gives 1.
    expect(reach == kernel::MechanismValue(std::int64_t{1}),
        "reach should count the single bound role target");

    if (failures != 0)
    {
        std::cerr << "read operand probe: " << failures << " failure(s)\n";
        return 7;
    }

    std::cout << "DSL read operand probe: passed (cross-object read, "
                 "binary operators, ordered comparison, sum and count "
                 "reducers all evaluated)\n";
    return 0;
}
