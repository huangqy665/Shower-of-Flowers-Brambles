#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <string_view>

#include "frozen_runtime_catalog.hpp"
#include "kernel_runtime.hpp"
#include "runtime_persistence.hpp"

namespace dillen::host {

enum class CliCommandDisposition
{
    Continue,
    Exit
};

struct CliCommandResult
{
    bool recognized = true;
    bool success = true;
    CliCommandDisposition disposition = CliCommandDisposition::Continue;
};

struct CliRunReport
{
    std::size_t commands = 0;
    std::size_t failures = 0;
    bool exitRequested = false;

    explicit operator bool() const noexcept;
};

class CliInspector
{
public:
    CliInspector(
        runtime::KernelRuntime& runtime,
        const kernel::FrozenRuntimeCatalog& catalog
    );

    CliCommandResult ExecuteLine(
        std::string_view line,
        std::ostream& output,
        std::ostream& errors
    );
    CliRunReport Run(
        std::istream& input,
        std::ostream& output,
        std::ostream& errors,
        bool showPrompt = true
    );

    void PrintHelp(std::ostream& output) const;
    void PrintStatus(std::ostream& output) const;

private:
    runtime::KernelRuntime& runtime_;
    const kernel::FrozenRuntimeCatalog& catalog_;
    persistence::RuntimePersistenceService persistence_;
};

}
