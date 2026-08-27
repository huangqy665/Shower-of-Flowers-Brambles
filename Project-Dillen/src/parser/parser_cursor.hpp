#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "diagnostic.hpp"
#include "lexer.hpp"
#include "token.hpp"

namespace dillen::parser {

struct ClausewitzDate
{
    int year = 0;
    int month = 0;
    int day = 0;
};

class ParserCursor
{
public:
    ParserCursor(
        const SourceBuffer& source,
        DiagnosticBag& diagnostics
    );

    const Token& Peek();
    Token Consume();
    bool ConsumeIf(TokenKind kind, Token* consumed = nullptr);
    bool Expect(
        TokenKind kind,
        Token* consumed = nullptr,
        std::string_view context = {}
    );
    bool ReadKey(Token& output);
    bool ReadScalar(Token& output);
    bool ReadRelation(RelationOperator& output);
    bool ReadInt64(std::int64_t& output, Token* token = nullptr);
    bool ReadDouble(double& output, Token* token = nullptr);
    bool ReadBool(bool& output, Token* token = nullptr);
    bool ReadDate(ClausewitzDate& output, Token* token = nullptr);
    bool SkipBlock();
    bool AtEnd();
    DiagnosticBag& Diagnostics() noexcept;

private:
    void Unexpected(
        const Token& token,
        std::string_view expected,
        std::string_view context
    );

    Lexer lexer_;
    DiagnosticBag& diagnostics_;
    std::optional<Token> lookahead_;
};

}
