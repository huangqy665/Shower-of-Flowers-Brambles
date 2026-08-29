#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "diagnostic.hpp"
#include "file_catalog.hpp"

namespace dillen::parser {

using ResolutionPassId = std::uint64_t;

enum class ResolutionPhase
{
    Declare,
    Resolve,
    Validate
};

using ResolutionPassFunction = std::function<bool(
    ParseWorkspace& workspace,
    DiagnosticBag& diagnostics
)>;

struct ResolutionPassDescriptor
{
    ResolutionPassId id = 0;
    std::string name;
    ResolutionPhase phase = ResolutionPhase::Declare;
    int priority = 0;
    ResolutionPassFunction run;
};

class Resolver
{
public:
    bool RegisterPass(ResolutionPassDescriptor pass);
    bool UnregisterPass(ResolutionPassId id);
    void ClearPasses();
    void Freeze();
    bool IsFrozen() const noexcept;
    bool Resolve(
        ParseWorkspace& workspace,
        DiagnosticBag& diagnostics
    ) const;
    const std::vector<ResolutionPassDescriptor>& Passes() const noexcept;

private:
    std::vector<ResolutionPassDescriptor> passes_;
    bool frozen_ = false;
};

}
