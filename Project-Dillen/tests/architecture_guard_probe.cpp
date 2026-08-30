#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

// Architecture guard for Demo 0.2 (Kernel Contract Freeze, memo section 4.2:
// "architectural diagnostics can block illegal dependencies and missing
// contracts"). Until now "kernel headers contain no HOI3 types" and "world
// never includes runtime" were only true by manual grep. This probe walks the
// standalone source tree, resolves every quoted #include to an owning module,
// and fails the build if an include crosses a layer the module CMake targets
// do not permit, or names a non-standalone subsystem (HOI3 / oracle /
// compatibility). It links no engine library on purpose -- it reads source.

namespace fs = std::filesystem;

namespace
{

struct Module
{
    std::string name;
    std::string relRoot;              // relative to Project-Dillen/src
    std::set<std::string> mayInclude; // module names this one is allowed to reach
};

// The allowed edges mirror the PUBLIC_LINKS in each src/<module>/CMakeLists.txt.
// A module may always include its own headers; every other edge must be listed.
const std::vector<Module>& Modules()
{
    static const std::vector<Module> modules = {
        {"kernel", "kernel", {}},
        {"world", "world", {"kernel"}},
        {"runtime", "runtime", {"kernel", "world"}},
        {"persistence", "persistence", {"kernel", "world", "runtime"}},
        {"parser", "parser", {}},
        {"authoring", "parser/parsers/dillen", {"parser", "kernel"}},
        {"adapter", "adapter", {"kernel"}},
        {"host",
         "host",
         {"kernel", "world", "runtime", "persistence", "parser", "authoring"}},
    };
    return modules;
}

std::string LowerCopy(std::string text)
{
    std::transform(
        text.begin(),
        text.end(),
        text.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

// Returns the target of a quoted #include ("foo.hpp"), or "" when the line is
// not a quoted include (blank, code, comment, or <system> include).
std::string QuotedIncludeTarget(const std::string& line)
{
    std::size_t cursor = line.find_first_not_of(" \t");
    if (cursor == std::string::npos || line[cursor] != '#')
    {
        return {};
    }
    cursor = line.find_first_not_of(" \t", cursor + 1);
    if (cursor == std::string::npos || line.compare(cursor, 7, "include") != 0)
    {
        return {};
    }
    const std::size_t open = line.find('"', cursor + 7);
    if (open == std::string::npos)
    {
        return {}; // <system> include -- not our concern
    }
    const std::size_t close = line.find('"', open + 1);
    if (close == std::string::npos)
    {
        return {};
    }
    return line.substr(open + 1, close - open - 1);
}

bool IsBuildableSource(const fs::path& path)
{
    const std::string ext = path.extension().string();
    return ext == ".hpp" || ext == ".cpp";
}

} // namespace

int main()
{
    const fs::path src = fs::path("Project-Dillen") / "src";
    if (!fs::is_directory(src))
    {
        std::cerr << "architecture guard probe: cannot find " << src.string()
                  << " -- run from the repository root\n";
        return 1;
    }

    // Header basename -> owning module. Every module publishes its own directory
    // as an include root, so includes are flat and a basename must be unique.
    std::map<std::string, std::string> headerOwner;
    for (const Module& module : Modules())
    {
        const fs::path dir = src / module.relRoot;
        if (!fs::is_directory(dir))
        {
            std::cerr << "architecture guard probe: missing module directory "
                      << dir.string() << '\n';
            return 2;
        }
        for (const auto& entry : fs::directory_iterator(dir))
        {
            if (!entry.is_regular_file()
                || entry.path().extension() != ".hpp")
            {
                continue;
            }
            const std::string base = entry.path().filename().string();
            const auto [it, inserted] = headerOwner.emplace(base, module.name);
            if (!inserted && it->second != module.name)
            {
                std::cerr << "architecture guard probe: header basename '" << base
                          << "' exists in both '" << it->second << "' and '"
                          << module.name
                          << "' -- flat include roots make the include ambiguous\n";
                return 3;
            }
        }
    }

    std::map<std::string, std::set<std::string>> mayInclude;
    for (const Module& module : Modules())
    {
        mayInclude.emplace(module.name, module.mayInclude);
    }

    std::vector<std::string> violations;
    std::vector<std::string> warnings;
    std::size_t scannedFiles = 0;

    for (const Module& module : Modules())
    {
        const fs::path dir = src / module.relRoot;
        for (const auto& entry : fs::directory_iterator(dir))
        {
            if (!entry.is_regular_file() || !IsBuildableSource(entry.path()))
            {
                continue;
            }
            std::ifstream input(entry.path());
            if (!input)
            {
                violations.push_back("cannot read " + entry.path().string());
                continue;
            }
            ++scannedFiles;
            const std::string origin = module.name + "/"
                + entry.path().filename().string();

            std::string line;
            int lineNo = 0;
            while (std::getline(input, line))
            {
                ++lineNo;
                const std::string target = QuotedIncludeTarget(line);
                if (target.empty())
                {
                    continue;
                }
                const std::string where = origin + ":" + std::to_string(lineNo);
                const std::string lowered = LowerCopy(target);

                if (lowered.find("hoi3") != std::string::npos
                    || lowered.find("oracle") != std::string::npos
                    || lowered.find("compatibility") != std::string::npos)
                {
                    violations.push_back(
                        where + " includes non-standalone header \"" + target
                        + "\"");
                    continue;
                }

                // Same-directory include: always legal, no module hop.
                if (fs::exists(dir / target))
                {
                    continue;
                }

                const std::string base = fs::path(target).filename().string();
                const auto owner = headerOwner.find(base);
                if (owner == headerOwner.end())
                {
                    warnings.push_back(
                        where + " includes \"" + target
                        + "\" which resolves to no standalone module");
                    continue;
                }
                if (owner->second == module.name)
                {
                    continue;
                }
                if (mayInclude[module.name].count(owner->second) == 0)
                {
                    violations.push_back(
                        where + " reaches module '" + owner->second
                        + "', which is not an allowed dependency of '"
                        + module.name + "'");
                }
            }
        }
    }

    for (const std::string& warning : warnings)
    {
        std::cerr << "architecture guard probe: note: " << warning << '\n';
    }

    if (!violations.empty())
    {
        std::cerr << "architecture guard probe: " << violations.size()
                  << " layering violation(s):\n";
        for (const std::string& violation : violations)
        {
            std::cerr << "  " << violation << '\n';
        }
        return 4;
    }

    std::cout << "architecture guard probe: passed (" << scannedFiles
              << " sources, " << headerOwner.size() << " headers across "
              << Modules().size()
              << " standalone modules; no cross-layer or HOI3/oracle includes)\n";
    return 0;
}
