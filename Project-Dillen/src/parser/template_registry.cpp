#include "template_registry.hpp"

#include <algorithm>
#include <utility>

#include "path_pattern.hpp"

namespace dillen::parser {

namespace {

std::size_t PatternSpecificity(std::string_view pattern)
{
    return static_cast<std::size_t>(std::count_if(
        pattern.begin(),
        pattern.end(),
        [](char character)
        {
            return character != '*' && character != '?';
        }
    ));
}

}

bool TemplateRegistry::Register(FileTemplate fileTemplate)
{
    if (frozen_
        || fileTemplate.id == 0
        || fileTemplate.parser == 0
        || fileTemplate.name.empty()
        || fileTemplate.pattern.empty()
        || Contains(fileTemplate.id))
    {
        return false;
    }
    templates_.push_back(std::move(fileTemplate));
    return true;
}

bool TemplateRegistry::Unregister(TemplateId id)
{
    if (frozen_)
    {
        return false;
    }
    const auto iterator = std::find_if(
        templates_.begin(),
        templates_.end(),
        [id](const FileTemplate& item)
        {
            return item.id == id;
        }
    );
    if (iterator == templates_.end())
    {
        return false;
    }
    templates_.erase(iterator);
    return true;
}

void TemplateRegistry::Clear()
{
    if (!frozen_)
    {
        templates_.clear();
    }
}

void TemplateRegistry::Freeze() noexcept
{
    frozen_ = true;
}

bool TemplateRegistry::IsFrozen() const noexcept
{
    return frozen_;
}

std::optional<TemplateMatch> TemplateRegistry::Match(
    std::string_view virtualPath,
    std::string_view probeData
) const
{
    const FileTemplate* best = nullptr;
    std::size_t bestSpecificity = 0;
    for (const FileTemplate& candidate : templates_)
    {
        if (!MatchPathPattern(candidate.pattern, virtualPath))
        {
            continue;
        }
        if (candidate.probe != nullptr
            && (probeData.empty()
                || !candidate.probe(virtualPath, probeData)))
        {
            continue;
        }

        const std::size_t specificity = PatternSpecificity(
            candidate.pattern
        );
        if (best == nullptr
            || candidate.priority > best->priority
            || (candidate.priority == best->priority
                && specificity > bestSpecificity)
            || (candidate.priority == best->priority
                && specificity == bestSpecificity
                && candidate.id < best->id))
        {
            best = &candidate;
            bestSpecificity = specificity;
        }
    }

    if (best == nullptr)
    {
        return std::nullopt;
    }
    return TemplateMatch{
        best->id,
        best->parser,
        best->dialect,
        best->priority,
        bestSpecificity
    };
}

const FileTemplate* TemplateRegistry::Find(TemplateId id) const
{
    const auto iterator = std::find_if(
        templates_.begin(),
        templates_.end(),
        [id](const FileTemplate& item)
        {
            return item.id == id;
        }
    );
    return iterator == templates_.end() ? nullptr : &(*iterator);
}

bool TemplateRegistry::Contains(TemplateId id) const
{
    return Find(id) != nullptr;
}

const std::vector<FileTemplate>& TemplateRegistry::All() const noexcept
{
    return templates_;
}

}
