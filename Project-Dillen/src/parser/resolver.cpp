#include "resolver.hpp"

#include <algorithm>
#include <exception>
#include <utility>

namespace dillen::parser {

namespace {

int PhaseOrder(ResolutionPhase phase)
{
    switch (phase)
    {
    case ResolutionPhase::Declare: return 0;
    case ResolutionPhase::Resolve: return 1;
    case ResolutionPhase::Validate: return 2;
    }
    return 3;
}

}

bool Resolver::RegisterPass(ResolutionPassDescriptor pass)
{
    if (frozen_
        || pass.id == 0
        || pass.name.empty()
        || !pass.run
        || std::any_of(
            passes_.begin(),
            passes_.end(),
            [&pass](const ResolutionPassDescriptor& item)
            {
                return item.id == pass.id;
            }))
    {
        return false;
    }
    passes_.push_back(std::move(pass));
    return true;
}

bool Resolver::UnregisterPass(ResolutionPassId id)
{
    if (frozen_)
    {
        return false;
    }
    const auto iterator = std::find_if(
        passes_.begin(),
        passes_.end(),
        [id](const ResolutionPassDescriptor& pass)
        {
            return pass.id == id;
        }
    );
    if (iterator == passes_.end())
    {
        return false;
    }
    passes_.erase(iterator);
    return true;
}

void Resolver::ClearPasses()
{
    if (!frozen_)
    {
        passes_.clear();
    }
}

void Resolver::Freeze()
{
    if (frozen_)
    {
        return;
    }
    std::sort(
        passes_.begin(),
        passes_.end(),
        [](const ResolutionPassDescriptor& first,
           const ResolutionPassDescriptor& second)
        {
            const int firstPhase = PhaseOrder(first.phase);
            const int secondPhase = PhaseOrder(second.phase);
            if (firstPhase != secondPhase)
            {
                return firstPhase < secondPhase;
            }
            if (first.priority != second.priority)
            {
                return first.priority < second.priority;
            }
            return first.id < second.id;
        }
    );
    frozen_ = true;
}

bool Resolver::IsFrozen() const noexcept
{
    return frozen_;
}

bool Resolver::Resolve(
    ParseWorkspace& workspace,
    DiagnosticBag& diagnostics
) const
{
    if (!frozen_)
    {
        diagnostics.Fatal(
            "resolver.not_frozen",
            "resolver pass registry must be frozen before resolution"
        );
        return false;
    }

    for (const ResolutionPassDescriptor& pass : passes_)
    {
        bool succeeded = false;
        try
        {
            succeeded = pass.run(workspace, diagnostics);
        }
        catch (const std::exception& exception)
        {
            diagnostics.Fatal(
                "resolver.pass_exception",
                pass.name + ": " + exception.what()
            );
        }
        catch (...)
        {
            diagnostics.Fatal(
                "resolver.pass_exception",
                pass.name + ": unknown exception"
            );
        }
        if (!succeeded)
        {
            if (!diagnostics.HasErrors())
            {
                diagnostics.Error(
                    "resolver.pass_failed",
                    pass.name + " failed without a diagnostic"
                );
            }
            return false;
        }
    }
    return !diagnostics.HasErrors();
}

const std::vector<ResolutionPassDescriptor>& Resolver::Passes() const noexcept
{
    return passes_;
}

}
