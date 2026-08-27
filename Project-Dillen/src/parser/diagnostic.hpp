#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "source_buffer.hpp"

namespace dillen::parser {

enum class DiagnosticSeverity
{
    Note,
    Warning,
    Error,
    Fatal
};

struct Diagnostic
{
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    std::string code;
    std::string message;
    SourceSpan span;
};

class DiagnosticBag
{
public:
    void Report(
        DiagnosticSeverity severity,
        std::string code,
        std::string message,
        SourceSpan span = {}
    );
    void Note(
        std::string code,
        std::string message,
        SourceSpan span = {}
    );
    void Warning(
        std::string code,
        std::string message,
        SourceSpan span = {}
    );
    void Error(
        std::string code,
        std::string message,
        SourceSpan span = {}
    );
    void Fatal(
        std::string code,
        std::string message,
        SourceSpan span = {}
    );

    bool HasErrors() const noexcept;
    std::size_t ErrorCount() const noexcept;
    const std::vector<Diagnostic>& All() const noexcept;
    void Clear();

private:
    std::vector<Diagnostic> diagnostics_;
    std::size_t errorCount_ = 0;
};

std::string FormatDiagnostic(
    const Diagnostic& diagnostic,
    std::string_view virtualPath = {}
);

}
