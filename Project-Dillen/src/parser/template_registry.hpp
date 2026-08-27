#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "template.hpp"

namespace dillen::parser {

class TemplateRegistry
{
public:
    bool Register(FileTemplate fileTemplate);
    bool Unregister(TemplateId id);
    void Clear();
    void Freeze() noexcept;
    bool IsFrozen() const noexcept;
    std::optional<TemplateMatch> Match(
        std::string_view virtualPath,
        std::string_view probeData = {}
    ) const;
    const FileTemplate* Find(TemplateId id) const;
    bool Contains(TemplateId id) const;
    const std::vector<FileTemplate>& All() const noexcept;

private:
    std::vector<FileTemplate> templates_;
    bool frozen_ = false;
};

}
