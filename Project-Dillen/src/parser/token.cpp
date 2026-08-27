#include "token.hpp"

namespace dillen::parser {

bool IsScalarToken(TokenKind kind) noexcept
{
    return kind == TokenKind::Identifier
        || kind == TokenKind::String
        || kind == TokenKind::Number
        || kind == TokenKind::Date;
}

bool IsRelationToken(TokenKind kind) noexcept
{
    return kind == TokenKind::Equals
        || kind == TokenKind::Less
        || kind == TokenKind::LessEqual
        || kind == TokenKind::Greater
        || kind == TokenKind::GreaterEqual
        || kind == TokenKind::NotEqual;
}

std::string_view TokenKindName(TokenKind kind) noexcept
{
    switch (kind)
    {
    case TokenKind::End: return "end of file";
    case TokenKind::Identifier: return "identifier";
    case TokenKind::String: return "string";
    case TokenKind::Number: return "number";
    case TokenKind::Date: return "date";
    case TokenKind::LeftBrace: return "'{'";
    case TokenKind::RightBrace: return "'}'";
    case TokenKind::Equals: return "'='";
    case TokenKind::Less: return "'<'";
    case TokenKind::LessEqual: return "'<='";
    case TokenKind::Greater: return "'>'";
    case TokenKind::GreaterEqual: return "'>='";
    case TokenKind::NotEqual: return "'!='";
    case TokenKind::Invalid: return "invalid token";
    }
    return "unknown token";
}

}
