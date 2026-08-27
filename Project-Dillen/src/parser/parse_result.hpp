#pragma once

#include <any>
#include <cstdint>

#include "source_buffer.hpp"
#include "template.hpp"

namespace dillen::parser {

using DefinitionTypeId = std::uint64_t;

struct ParseArtifact
{
    DefinitionTypeId type = 0;
    std::any value;

    template <typename T>
    const T* As() const noexcept
    {
        return std::any_cast<T>(&value);
    }

    template <typename T>
    T* As() noexcept
    {
        return std::any_cast<T>(&value);
    }
};

struct ParseResult
{
    bool success = false;
    SourceId source = kInvalidSourceId;
    TemplateId fileTemplate = 0;
    ParserId parser = 0;
    ParseArtifact artifact;
    std::size_t diagnosticBegin = 0;
    std::size_t diagnosticEnd = 0;
};

}
