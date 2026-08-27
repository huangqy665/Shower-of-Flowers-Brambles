#pragma once

#include <string_view>

#include "source_buffer.hpp"

namespace dillen::parser {

enum class TokenKind
{
    End,
    Identifier,
    String,
    Number,
    Date,
    LeftBrace,
    RightBrace,
    Equals,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    NotEqual,
    Invalid
};

enum class RelationOperator
{
    Assign,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    NotEqual
};

struct Token
{
    TokenKind kind = TokenKind::End;
    std::string_view text;
    SourceSpan span;
};

bool IsScalarToken(TokenKind kind) noexcept;
bool IsRelationToken(TokenKind kind) noexcept;
std::string_view TokenKindName(TokenKind kind) noexcept;

}
