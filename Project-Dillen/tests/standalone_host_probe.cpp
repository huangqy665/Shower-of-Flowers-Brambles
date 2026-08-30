#include <cstdint>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>

#include "cli_inspector.hpp"
#include "mechanism_ids.hpp"
#include "mechanism_value.hpp"
#include "standalone_session.hpp"

namespace {

std::string Hex(std::uint64_t value)
{
    std::ostringstream output;
    output << "0x" << std::hex << value;
    return output.str();
}

}

int main()
{
    using namespace dillen;
    const std::string rootName = "dillen.demo.root";
    const std::string extensionName = "dillen.demo.audit_extension";
    host::StandaloneSessionConfig config;
    config.sources.push_back({
        "standalone_host_base",
        "Project-Dillen/tests/fixtures/dillen_authoring",
        0,
        {},
        {},
        {}
    });
    config.sources.push_back({
        "standalone_host_overlay",
        "Project-Dillen/tests/fixtures/dillen_authoring_overlay",
        100,
        {},
        {},
        {}
    });
    config.rulesets.root = {
        kernel::StableRulesetId(rootName), rootName, 1
    };
    config.rulesets.extensions.push_back({
        kernel::StableRulesetId(extensionName), extensionName, 1
    });

    host::StandaloneSession session;
    host::StandaloneSessionReport startReport;
    if (!session.Start(config, startReport)
        || !session.IsReady()
        || session.Catalog().LockedPackages().Size() != 2
        || session.Catalog().LockedSources().Size() != 14
        || session.Runtime().Query().Mechanisms().Size() != 2)
    {
        std::cerr << "Standalone Host session bootstrap failed\n";
        return 1;
    }

    const kernel::MechanismTypeId counterType =
        kernel::StableMechanismTypeId("dillen.demo.counter");
    const kernel::MechanismDefinitionId counterDefinition =
        kernel::StableMechanismDefinitionId(
            counterType, "dillen.demo.default_counter"
        );
    const kernel::MechanismInstanceId counter =
        kernel::StableMechanismInstanceId(counterDefinition, 0);
    const auto valueSlot = session.Catalog().ResolveDefinitionFieldSlot(
        counterDefinition, "value"
    );
    if (!valueSlot)
    {
        std::cerr << "Standalone Host fixture field is missing\n";
        return 2;
    }

    host::CliInspector inspector(session.Runtime(), session.Catalog());
    std::ostringstream output;
    std::ostringstream errors;
    const std::string counterId = Hex(counter.value);
    if (!inspector.ExecuteLine("status", output, errors).success
        || !inspector.ExecuteLine("list mechanisms", output, errors).success
        || !inspector.ExecuteLine(
            "show mechanism " + counterId, output, errors
        ).success
        || !inspector.ExecuteLine(
            "set mechanism " + counterId + " value 42",
            output,
            errors
        ).success)
    {
        std::cerr << "Standalone Host Query or immediate Command failed\n";
        return 3;
    }
    const kernel::MechanismValue* value =
        session.Runtime().Query().Mechanisms().FindField(counter, *valueSlot);
    if (value == nullptr
        || *value != kernel::MechanismValue(std::int64_t{42}))
    {
        std::cerr << "Standalone Host immediate Command did not commit\n";
        return 4;
    }

    const std::filesystem::path savePath =
        "Project-Dillen/standalone_host_probe.runtime";
    std::error_code fileError;
    std::filesystem::remove(savePath, fileError);
    std::filesystem::remove(savePath.string() + ".tmp", fileError);
    if (!inspector.ExecuteLine(
            "save \"" + savePath.string() + "\"", output, errors
        ).success
        || !std::filesystem::is_regular_file(savePath)
        || !inspector.ExecuteLine(
            "set mechanism " + counterId + " value 99",
            output,
            errors
        ).success
        || !inspector.ExecuteLine(
            "load \"" + savePath.string() + "\"", output, errors
        ).success)
    {
        std::filesystem::remove(savePath, fileError);
        std::cerr << "Standalone Host file persistence failed: "
                  << errors.str() << output.str() << '\n';
        return 5;
    }
    std::filesystem::remove(savePath, fileError);
    value = session.Runtime().Query().Mechanisms().FindField(
        counter, *valueSlot
    );
    if (value == nullptr
        || *value != kernel::MechanismValue(std::int64_t{42}))
    {
        std::cerr << "Standalone Host restore did not replace state\n";
        return 6;
    }

    if (!inspector.ExecuteLine(
            "enqueue mechanism " + counterId + " value 17 1 5",
            output,
            errors
        ).success
        || session.Runtime().Commands().Size() != 1
        || !inspector.ExecuteLine("tick 1", output, errors).success
        || session.Runtime().Commands().Size() != 0
        || session.Runtime().Query().Tick() != 1)
    {
        std::cerr << "Standalone Host queued Command or Tick failed\n";
        return 7;
    }

    std::istringstream script(
        "# script mode\nstatus\nshow mechanism " + counterId
        + "\nquit\n"
    );
    const host::CliRunReport runReport = inspector.Run(
        script, output, errors, false
    );
    if (!runReport
        || runReport.commands != 3
        || !runReport.exitRequested
        || output.str().find("fingerprint=") == std::string::npos
        || output.str().find("lifecycle=active") == std::string::npos
        || !errors.str().empty())
    {
        std::cerr << "Standalone Host scripted Inspector failed\n";
        return 8;
    }

    const host::CliCommandResult unknown = inspector.ExecuteLine(
        "unknown", output, errors
    );
    if (unknown.recognized || unknown.success)
    {
        std::cerr << "Standalone Host unknown command isolation failed\n";
        return 9;
    }

    std::cout
        << "Standalone Host: passed (tick "
        << session.Runtime().Query().Tick()
        << ", commands/query/persistence verified)\n";
    return 0;
}
