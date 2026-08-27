#include "parser_registry.hpp"

#include <algorithm>
#include <exception>
#include <utility>

namespace dillen::parser {

bool ParserRegistry::Register(ParserDescriptor descriptor)
{
    const bool hasTokenParser = static_cast<bool>(descriptor.parse);
    const bool hasSourceParser = static_cast<bool>(descriptor.parseSource);
    if (frozen_
        || descriptor.id == 0
        || descriptor.name.empty()
        || descriptor.outputType == 0
        || hasTokenParser == hasSourceParser
        || Contains(descriptor.id))
    {
        return false;
    }
    parsers_.push_back(std::move(descriptor));
    return true;
}

bool ParserRegistry::Unregister(ParserId id)
{
    if (frozen_)
    {
        return false;
    }
    const auto iterator = std::find_if(
        parsers_.begin(),
        parsers_.end(),
        [id](const ParserDescriptor& descriptor)
        {
            return descriptor.id == id;
        }
    );
    if (iterator == parsers_.end())
    {
        return false;
    }
    parsers_.erase(iterator);
    return true;
}

void ParserRegistry::Clear()
{
    if (!frozen_)
    {
        parsers_.clear();
    }
}

void ParserRegistry::Freeze() noexcept
{
    frozen_ = true;
}

bool ParserRegistry::IsFrozen() const noexcept
{
    return frozen_;
}

const ParserDescriptor* ParserRegistry::Find(ParserId id) const
{
    const auto iterator = std::find_if(
        parsers_.begin(),
        parsers_.end(),
        [id](const ParserDescriptor& descriptor)
        {
            return descriptor.id == id;
        }
    );
    return iterator == parsers_.end() ? nullptr : &(*iterator);
}

bool ParserRegistry::Contains(ParserId id) const
{
    return Find(id) != nullptr;
}

ParseResult ParserRegistry::Parse(
    const SourceBuffer& source,
    const TemplateMatch& fileTemplate,
    DiagnosticBag& diagnostics
) const
{
    ParseResult result;
    result.source = source.Id();
    result.fileTemplate = fileTemplate.fileTemplate;
    result.parser = fileTemplate.parser;
    result.diagnosticBegin = diagnostics.All().size();

    if (!frozen_)
    {
        diagnostics.Fatal(
            "parser_registry.not_frozen",
            "parser registry must be frozen before parsing"
        );
        result.diagnosticEnd = diagnostics.All().size();
        return result;
    }

    const ParserDescriptor* descriptor = Find(fileTemplate.parser);
    if (descriptor == nullptr)
    {
        diagnostics.Error(
            "parser_registry.parser_missing",
            "template references an unregistered parser"
        );
        result.diagnosticEnd = diagnostics.All().size();
        return result;
    }
    if (fileTemplate.dialect != 0
        && descriptor->inputDialect != 0
        && fileTemplate.dialect != descriptor->inputDialect)
    {
        diagnostics.Error(
            "parser_registry.dialect_mismatch",
            "template dialect does not match parser input dialect"
        );
        result.diagnosticEnd = diagnostics.All().size();
        return result;
    }

    result.artifact.type = descriptor->outputType;
    bool parsed = false;
    try
    {
        if (descriptor->parseSource)
        {
            parsed = descriptor->parseSource(
                source,
                diagnostics,
                result.artifact
            );
        }
        else
        {
            ParserCursor cursor(source, diagnostics);
            parsed = descriptor->parse(cursor, result.artifact);
            if (parsed
                && descriptor->requiresEndOfFile
                && !cursor.AtEnd())
            {
                diagnostics.Error(
                    "parser_registry.trailing_tokens",
                    "parser returned before consuming the complete source",
                    cursor.Peek().span
                );
                parsed = false;
            }
        }
    }
    catch (const std::exception& exception)
    {
        diagnostics.Fatal(
            "parser_registry.parser_exception",
            exception.what()
        );
    }
    catch (...)
    {
        diagnostics.Fatal(
            "parser_registry.parser_exception",
            "parser threw an unknown exception"
        );
    }

    if (!parsed
        && diagnostics.All().size() == result.diagnosticBegin)
    {
        diagnostics.Error(
            "parser_registry.parse_failed",
            "parser rejected the source without a diagnostic"
        );
    }

    result.diagnosticEnd = diagnostics.All().size();
    const bool emittedError = std::any_of(
        diagnostics.All().begin()
            + static_cast<std::ptrdiff_t>(result.diagnosticBegin),
        diagnostics.All().begin()
            + static_cast<std::ptrdiff_t>(result.diagnosticEnd),
        [](const Diagnostic& diagnostic)
        {
            return diagnostic.severity == DiagnosticSeverity::Error
                || diagnostic.severity == DiagnosticSeverity::Fatal;
        }
    );
    result.success = parsed && !emittedError;
    return result;
}

const std::vector<ParserDescriptor>& ParserRegistry::All() const noexcept
{
    return parsers_;
}

}
