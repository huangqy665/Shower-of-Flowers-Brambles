#include "parser_cursor.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>

namespace dillen::parser {

namespace {

bool EqualsIgnoreCase(
    std::string_view first,
    std::string_view second
)
{
    return first.size() == second.size()
        && std::equal(
            first.begin(),
            first.end(),
            second.begin(),
            [](char left, char right)
            {
                return std::tolower(
                    static_cast<unsigned char>(left)
                ) == std::tolower(
                    static_cast<unsigned char>(right)
                );
            }
        );
}

bool ParseDate(
    std::string_view text,
    ClausewitzDate& output
)
{
    int values[3]{};
    std::size_t component = 0;
    for (char character : text)
    {
        if (character == '.')
        {
            ++component;
            continue;
        }
        if (component >= 3
            || !std::isdigit(static_cast<unsigned char>(character)))
        {
            return false;
        }
        values[component] = values[component] * 10 + (character - '0');
    }
    if (component != 2)
    {
        return false;
    }
    output = {values[0], values[1], values[2]};
    return output.month >= 1
        && output.month <= 12
        && output.day >= 1
        && output.day <= 31;
}

}

ParserCursor::ParserCursor(
    const SourceBuffer& source,
    DiagnosticBag& diagnostics
)
    : lexer_(source, diagnostics),
      diagnostics_(diagnostics)
{
}

const Token& ParserCursor::Peek()
{
    if (!lookahead_)
    {
        lookahead_ = lexer_.Next();
    }
    return *lookahead_;
}

Token ParserCursor::Consume()
{
    Token token = Peek();
    lookahead_.reset();
    return token;
}

bool ParserCursor::ConsumeIf(
    TokenKind kind,
    Token* consumed
)
{
    if (Peek().kind != kind)
    {
        return false;
    }
    Token token = Consume();
    if (consumed != nullptr)
    {
        *consumed = token;
    }
    return true;
}

bool ParserCursor::Expect(
    TokenKind kind,
    Token* consumed,
    std::string_view context
)
{
    if (Peek().kind != kind)
    {
        Unexpected(Peek(), TokenKindName(kind), context);
        return false;
    }
    Token token = Consume();
    if (consumed != nullptr)
    {
        *consumed = token;
    }
    return true;
}

bool ParserCursor::ReadKey(Token& output)
{
    if (!IsScalarToken(Peek().kind))
    {
        Unexpected(Peek(), "property key", {});
        return false;
    }
    output = Consume();
    return true;
}

bool ParserCursor::ReadScalar(Token& output)
{
    if (!IsScalarToken(Peek().kind))
    {
        Unexpected(Peek(), "scalar value", {});
        return false;
    }
    output = Consume();
    return true;
}

bool ParserCursor::ReadRelation(RelationOperator& output)
{
    const Token token = Peek();
    switch (token.kind)
    {
    case TokenKind::Equals: output = RelationOperator::Assign; break;
    case TokenKind::Less: output = RelationOperator::Less; break;
    case TokenKind::LessEqual: output = RelationOperator::LessEqual; break;
    case TokenKind::Greater: output = RelationOperator::Greater; break;
    case TokenKind::GreaterEqual: output = RelationOperator::GreaterEqual; break;
    case TokenKind::NotEqual: output = RelationOperator::NotEqual; break;
    default:
        Unexpected(token, "relation operator", {});
        return false;
    }
    Consume();
    return true;
}

bool ParserCursor::ReadInt64(
    std::int64_t& output,
    Token* token
)
{
    Token value;
    if (!ReadScalar(value))
    {
        return false;
    }
    std::string text(value.text);
    char* end = nullptr;
    const long long parsed = std::strtoll(text.c_str(), &end, 10);
    if (end != text.c_str() + text.size())
    {
        diagnostics_.Error(
            "parser.expected_integer",
            "expected an integer value",
            value.span
        );
        return false;
    }
    output = static_cast<std::int64_t>(parsed);
    if (token != nullptr)
    {
        *token = value;
    }
    return true;
}

bool ParserCursor::ReadDouble(
    double& output,
    Token* token
)
{
    Token value;
    if (!ReadScalar(value))
    {
        return false;
    }
    std::string text(value.text);
    char* end = nullptr;
    const double parsed = std::strtod(text.c_str(), &end);
    if (end != text.c_str() + text.size())
    {
        diagnostics_.Error(
            "parser.expected_number",
            "expected a numeric value",
            value.span
        );
        return false;
    }
    output = parsed;
    if (token != nullptr)
    {
        *token = value;
    }
    return true;
}

bool ParserCursor::ReadBool(
    bool& output,
    Token* token
)
{
    Token value;
    if (!ReadScalar(value))
    {
        return false;
    }
    if (EqualsIgnoreCase(value.text, "yes")
        || EqualsIgnoreCase(value.text, "true"))
    {
        output = true;
    }
    else if (EqualsIgnoreCase(value.text, "no")
        || EqualsIgnoreCase(value.text, "false"))
    {
        output = false;
    }
    else
    {
        diagnostics_.Error(
            "parser.expected_boolean",
            "expected yes/no or true/false",
            value.span
        );
        return false;
    }
    if (token != nullptr)
    {
        *token = value;
    }
    return true;
}

bool ParserCursor::ReadDate(
    ClausewitzDate& output,
    Token* token
)
{
    Token value;
    if (!ReadScalar(value))
    {
        return false;
    }
    if (!ParseDate(value.text, output))
    {
        diagnostics_.Error(
            "parser.expected_date",
            "expected a date in year.month.day form",
            value.span
        );
        return false;
    }
    if (token != nullptr)
    {
        *token = value;
    }
    return true;
}

bool ParserCursor::SkipBlock()
{
    if (!Expect(TokenKind::LeftBrace, nullptr, "while skipping block"))
    {
        return false;
    }
    std::size_t depth = 1;
    while (depth != 0)
    {
        const Token token = Consume();
        if (token.kind == TokenKind::End)
        {
            diagnostics_.Error(
                "parser.unterminated_block",
                "unexpected end of file while skipping block",
                token.span
            );
            return false;
        }
        if (token.kind == TokenKind::LeftBrace)
        {
            ++depth;
        }
        else if (token.kind == TokenKind::RightBrace)
        {
            --depth;
        }
    }
    return true;
}

bool ParserCursor::AtEnd()
{
    return Peek().kind == TokenKind::End;
}

DiagnosticBag& ParserCursor::Diagnostics() noexcept
{
    return diagnostics_;
}

void ParserCursor::Unexpected(
    const Token& token,
    std::string_view expected,
    std::string_view context
)
{
    std::string message = "expected ";
    message.append(expected);
    message.append(", found ");
    message.append(TokenKindName(token.kind));
    if (!context.empty())
    {
        message.push_back(' ');
        message.append(context);
    }
    diagnostics_.Error(
        "parser.unexpected_token",
        std::move(message),
        token.span
    );
}

}
