#include "diagnostic.hpp"

#include <sstream>
#include <utility>

namespace dillen::parser {

void DiagnosticBag::Report(
    DiagnosticSeverity severity,
    std::string code,
    std::string message,
    SourceSpan span
)
{
    if (severity == DiagnosticSeverity::Error
        || severity == DiagnosticSeverity::Fatal)
    {
        ++errorCount_;
    }
    diagnostics_.push_back({
        severity,
        std::move(code),
        std::move(message),
        span
    });
}

void DiagnosticBag::Note(
    std::string code,
    std::string message,
    SourceSpan span
)
{
    Report(
        DiagnosticSeverity::Note,
        std::move(code),
        std::move(message),
        span
    );
}

void DiagnosticBag::Warning(
    std::string code,
    std::string message,
    SourceSpan span
)
{
    Report(
        DiagnosticSeverity::Warning,
        std::move(code),
        std::move(message),
        span
    );
}

void DiagnosticBag::Error(
    std::string code,
    std::string message,
    SourceSpan span
)
{
    Report(
        DiagnosticSeverity::Error,
        std::move(code),
        std::move(message),
        span
    );
}

void DiagnosticBag::Fatal(
    std::string code,
    std::string message,
    SourceSpan span
)
{
    Report(
        DiagnosticSeverity::Fatal,
        std::move(code),
        std::move(message),
        span
    );
}

bool DiagnosticBag::HasErrors() const noexcept
{
    return errorCount_ != 0;
}

std::size_t DiagnosticBag::ErrorCount() const noexcept
{
    return errorCount_;
}

const std::vector<Diagnostic>& DiagnosticBag::All() const noexcept
{
    return diagnostics_;
}

void DiagnosticBag::Clear()
{
    diagnostics_.clear();
    errorCount_ = 0;
}

std::string FormatDiagnostic(
    const Diagnostic& diagnostic,
    std::string_view virtualPath
)
{
    std::ostringstream output;
    if (!virtualPath.empty())
    {
        output << virtualPath;
    }
    if (diagnostic.span.IsValid())
    {
        if (!virtualPath.empty())
        {
            output << ':';
        }
        output << diagnostic.span.begin.line
               << ':'
               << diagnostic.span.begin.column;
    }
    if (!virtualPath.empty() || diagnostic.span.IsValid())
    {
        output << ": ";
    }
    output << diagnostic.code << ": " << diagnostic.message;
    return output.str();
}

}
