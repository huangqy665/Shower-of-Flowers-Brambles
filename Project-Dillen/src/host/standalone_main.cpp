#include <charconv>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "cli_inspector.hpp"
#include "mechanism_ids.hpp"
#include "standalone_session.hpp"

namespace {

void PrintUsage(std::ostream& output)
{
    output
        << "Project Dillen Standalone Host\n"
        << "usage: project-dillen --content-root <path> "
           "--root <name>@<version> [options]\n"
        << "options:\n"
        << "  --source <name>@<priority>=<path> add a Source Layer\n"
        << "  --extension <name>@<version>  add an Extension Ruleset\n"
        << "  --commands <path>             execute a command file\n"
        << "  --no-prompt                   suppress interactive prompt\n"
        << "  --help                        show this message\n";
}

bool ParseVersionedRuleset(
    std::string_view text,
    dillen::authoring::SelectedRulesetVersion& output
)
{
    const std::size_t separator = text.rfind('@');
    if (separator == std::string_view::npos
        || separator == 0 || separator + 1 == text.size())
    {
        return false;
    }
    std::uint32_t version = 0;
    const std::string_view versionText = text.substr(separator + 1);
    const auto result = std::from_chars(
        versionText.data(),
        versionText.data() + versionText.size(),
        version
    );
    if (result.ec != std::errc{}
        || result.ptr != versionText.data() + versionText.size()
        || version == 0)
    {
        return false;
    }
    output.canonicalName = std::string(text.substr(0, separator));
    output.id = dillen::kernel::StableRulesetId(output.canonicalName);
    output.version = version;
    return true;
}

bool ParseSourceLayer(
    std::string_view text,
    dillen::host::StandaloneSourceLayerConfig& output
)
{
    const std::size_t equals = text.find('=');
    const std::size_t at = text.rfind('@', equals);
    if (equals == std::string_view::npos
        || at == std::string_view::npos
        || at == 0
        || at + 1 >= equals
        || equals + 1 >= text.size())
    {
        return false;
    }
    int priority = 0;
    const std::string_view priorityText = text.substr(
        at + 1,
        equals - at - 1
    );
    const auto result = std::from_chars(
        priorityText.data(),
        priorityText.data() + priorityText.size(),
        priority
    );
    if (result.ec != std::errc{}
        || result.ptr != priorityText.data() + priorityText.size())
    {
        return false;
    }
    output.name = std::string(text.substr(0, at));
    output.priority = priority;
    output.root = std::string(text.substr(equals + 1));
    return true;
}

}

int main(int argumentCount, char** arguments)
{
    dillen::host::StandaloneSessionConfig config;
    std::filesystem::path commandFile;
    bool showPrompt = true;
    for (int index = 1; index < argumentCount; ++index)
    {
        const std::string_view argument(arguments[index]);
        if (argument == "--help")
        {
            PrintUsage(std::cout);
            return 0;
        }
        if (argument == "--no-prompt")
        {
            showPrompt = false;
            continue;
        }
        if (index + 1 >= argumentCount)
        {
            std::cerr << "missing value for " << argument << '\n';
            PrintUsage(std::cerr);
            return 2;
        }
        const std::string_view value(arguments[++index]);
        if (argument == "--content-root")
        {
            config.contentRoot = std::string(value);
        }
        else if (argument == "--source")
        {
            dillen::host::StandaloneSourceLayerConfig source;
            if (!ParseSourceLayer(value, source))
            {
                std::cerr << "invalid Source Layer declaration\n";
                return 2;
            }
            config.sources.push_back(std::move(source));
        }
        else if (argument == "--root")
        {
            if (!ParseVersionedRuleset(value, config.rulesets.root))
            {
                std::cerr << "invalid Root Ruleset selection\n";
                return 2;
            }
        }
        else if (argument == "--extension")
        {
            dillen::authoring::SelectedRulesetVersion extension;
            if (!ParseVersionedRuleset(value, extension))
            {
                std::cerr << "invalid Extension Ruleset selection\n";
                return 2;
            }
            config.rulesets.extensions.push_back(std::move(extension));
        }
        else if (argument == "--commands")
        {
            commandFile = std::string(value);
        }
        else
        {
            std::cerr << "unknown option: " << argument << '\n';
            PrintUsage(std::cerr);
            return 2;
        }
    }

    dillen::host::StandaloneSession session;
    dillen::host::StandaloneSessionReport startReport;
    if (!session.Start(config, startReport))
    {
        std::cerr << "startup failed: " << startReport.message << '\n';
        for (const std::string& diagnostic : startReport.diagnostics)
        {
            std::cerr << diagnostic << '\n';
        }
        for (const dillen::world::InitialWorldBuildIssue& issue
            : startReport.worldIssues)
        {
            std::cerr << "world: " << issue.subject
                      << ": " << issue.message << '\n';
        }
        return 3;
    }

    dillen::host::CliInspector inspector(
        session.Runtime(), session.Catalog()
    );
    std::cout << "Project Dillen Standalone Host ready\n";
    inspector.PrintStatus(std::cout);
    if (!commandFile.empty())
    {
        std::ifstream commands(commandFile);
        if (!commands)
        {
            std::cerr << "command file could not be opened\n";
            return 4;
        }
        const dillen::host::CliRunReport report = inspector.Run(
            commands, std::cout, std::cerr, false
        );
        return report ? 0 : 5;
    }
    const dillen::host::CliRunReport report = inspector.Run(
        std::cin, std::cout, std::cerr, showPrompt
    );
    return report ? 0 : 5;
}
