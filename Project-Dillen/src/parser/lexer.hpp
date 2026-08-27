#pragma once

#include <cstddef>
#include <cstdint>

#include "diagnostic.hpp"
#include "source_buffer.hpp"
#include "token.hpp"

namespace dillen::parser {

class Lexer
{
public:
    Lexer(
        const SourceBuffer& source,
        DiagnosticBag& diagnostics
    );

    Token Next();

private:
    void SkipTrivia();
    Token ReadString();
    Token ReadAtom();
    Token ReadOperator();
    Token MakeToken(
        TokenKind kind,
        std::size_t beginOffset,
        std::uint32_t beginLine,
        std::uint32_t beginColumn,
        std::size_t textOffset,
        std::size_t textLength
    ) const;
    void Advance();
    bool AtEnd() const noexcept;
    char Current() const noexcept;
    char Peek(std::size_t distance = 1) const noexcept;

    const SourceBuffer& source_;
    DiagnosticBag& diagnostics_;
    std::string_view bytes_;
    std::size_t position_ = 0;
    std::uint32_t line_ = 1;
    std::uint32_t column_ = 1;
};

}
