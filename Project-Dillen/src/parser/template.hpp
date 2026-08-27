#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace dillen::parser {

using TemplateId = std::uint64_t;
using ParserId = std::uint64_t;
using DialectId = std::uint64_t;

using ProbeFunction = bool (*)(
    std::string_view virtualPath,
    std::string_view probeData
);

struct FileTemplate
{
    TemplateId id = 0;
    std::string name;
    std::string pattern;
    ParserId parser = 0;
    DialectId dialect = 0;
    int priority = 0;
    ProbeFunction probe = nullptr;
};

struct TemplateMatch
{
    TemplateId fileTemplate = 0;
    ParserId parser = 0;
    DialectId dialect = 0;
    int priority = 0;
    std::size_t specificity = 0;
};

}
