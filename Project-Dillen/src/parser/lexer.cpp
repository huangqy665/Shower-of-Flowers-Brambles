#include "lexer.hpp"

#include <cctype>
#include <cstdlib>
#include <string>

namespace dillen::parser {

namespace {

bool IsWhitespace(char character)
{
    return std::isspace(
        static_cast<unsigned char>(character)
    ) != 0;
}

bool IsAtomDelimiter(
    char character,
    char next
)
{
    return IsWhitespace(character)
        || character == '{'
        || character == '}'
        || character == '='
        || character == '<'
        || character == '>'
        || character == '!'
        || character == '#'
        || (character == '/' && next == '/');
}

bool IsNumber(std::string_view text)
{
    if (text.empty())
    {
        return false;
    }
    std::string value(text);
    char* end = nullptr;
    std::strtod(value.c_str(), &end);
    return end == value.c_str() + value.size();
}

bool IsDate(std::string_view text)
{
    int componentCount = 0;
    bool hasDigit = false;
    for (char character : text)
    {
        if (std::isdigit(static_cast<unsigned char>(character)))
        {
            hasDigit = true;
            continue;
        }
        if (character != '.' || !hasDigit || componentCount >= 2)
        {
            return false;
        }
        ++componentCount;
        hasDigit = false;
    }
    return componentCount == 2 && hasDigit;
}

}

Lexer::Lexer(
    const SourceBuffer& source,
    DiagnosticBag& diagnostics
)
    : source_(source),
      diagnostics_(diagnostics),
      bytes_(source.Bytes())
{
    if (source.Encoding() == SourceEncoding::Utf8Bom
        && bytes_.size() >= 3)
    {
        position_ = 3;
    }
}

Token Lexer::Next()
{
    SkipTrivia();
    if (AtEnd())
    {
        const SourceLocation location{
            source_.Id(),
            position_,
            line_,
            column_
        };
        return {TokenKind::End, {}, {location, location}};
    }

    const std::size_t beginOffset = position_;
    const std::uint32_t beginLine = line_;
    const std::uint32_t beginColumn = column_;
    switch (Current())
    {
    case '{':
        Advance();
        return MakeToken(
            TokenKind::LeftBrace,
            beginOffset,
            beginLine,
            beginColumn,
            beginOffset,
            1
        );
    case '}':
        Advance();
        return MakeToken(
            TokenKind::RightBrace,
            beginOffset,
            beginLine,
            beginColumn,
            beginOffset,
            1
        );
    case '"':
        return ReadString();
    case '=':
    case '<':
    case '>':
    case '!':
        return ReadOperator();
    default:
        return ReadAtom();
    }
}

void Lexer::SkipTrivia()
{
    while (!AtEnd())
    {
        if (IsWhitespace(Current()))
        {
            Advance();
            continue;
        }
        if (Current() == '#')
        {
            while (!AtEnd()
                && Current() != '\r'
                && Current() != '\n')
            {
                Advance();
            }
            continue;
        }
        if (Current() == '/' && Peek() == '/')
        {
            Advance();
            Advance();
            while (!AtEnd()
                && Current() != '\r'
                && Current() != '\n')
            {
                Advance();
            }
            continue;
        }
        break;
    }
}

Token Lexer::ReadString()
{
    const std::size_t beginOffset = position_;
    const std::uint32_t beginLine = line_;
    const std::uint32_t beginColumn = column_;
    Advance();
    const std::size_t textOffset = position_;
    while (!AtEnd())
    {
        if (Current() == '"')
        {
            const std::size_t textLength = position_ - textOffset;
            Advance();
            return MakeToken(
                TokenKind::String,
                beginOffset,
                beginLine,
                beginColumn,
                textOffset,
                textLength
            );
        }
        if (Current() == '\\' && Peek() != '\0')
        {
            Advance();
            Advance();
            continue;
        }
        Advance();
    }

    Token token = MakeToken(
        TokenKind::Invalid,
        beginOffset,
        beginLine,
        beginColumn,
        textOffset,
        position_ - textOffset
    );
    diagnostics_.Error(
        "lexer.unterminated_string",
        "unterminated quoted string",
        token.span
    );
    return token;
}

Token Lexer::ReadAtom()
{
    const std::size_t beginOffset = position_;
    const std::uint32_t beginLine = line_;
    const std::uint32_t beginColumn = column_;
    while (!AtEnd()
        && !IsAtomDelimiter(Current(), Peek()))
    {
        Advance();
    }

    if (position_ == beginOffset)
    {
        Advance();
        Token token = MakeToken(
            TokenKind::Invalid,
            beginOffset,
            beginLine,
            beginColumn,
            beginOffset,
            position_ - beginOffset
        );
        diagnostics_.Error(
            "lexer.invalid_character",
            "invalid character in Clausewitz input",
            token.span
        );
        return token;
    }

    const std::string_view text = bytes_.substr(
        beginOffset,
        position_ - beginOffset
    );
    TokenKind kind = TokenKind::Identifier;
    if (IsDate(text))
    {
        kind = TokenKind::Date;
    }
    else if (IsNumber(text))
    {
        kind = TokenKind::Number;
    }
    return MakeToken(
        kind,
        beginOffset,
        beginLine,
        beginColumn,
        beginOffset,
        position_ - beginOffset
    );
}

Token Lexer::ReadOperator()
{
    const std::size_t beginOffset = position_;
    const std::uint32_t beginLine = line_;
    const std::uint32_t beginColumn = column_;
    const char first = Current();
    Advance();

    TokenKind kind = TokenKind::Invalid;
    if (first == '=')
    {
        kind = TokenKind::Equals;
    }
    else if (first == '<')
    {
        if (Current() == '=')
        {
            Advance();
            kind = TokenKind::LessEqual;
        }
        else
        {
            kind = TokenKind::Less;
        }
    }
    else if (first == '>')
    {
        if (Current() == '=')
        {
            Advance();
            kind = TokenKind::GreaterEqual;
        }
        else
        {
            kind = TokenKind::Greater;
        }
    }
    else if (first == '!' && Current() == '=')
    {
        Advance();
        kind = TokenKind::NotEqual;
    }

    Token token = MakeToken(
        kind,
        beginOffset,
        beginLine,
        beginColumn,
        beginOffset,
        position_ - beginOffset
    );
    if (kind == TokenKind::Invalid)
    {
        diagnostics_.Error(
            "lexer.invalid_operator",
            "invalid relation operator",
            token.span
        );
    }
    return token;
}

Token Lexer::MakeToken(
    TokenKind kind,
    std::size_t beginOffset,
    std::uint32_t beginLine,
    std::uint32_t beginColumn,
    std::size_t textOffset,
    std::size_t textLength
) const
{
    return {
        kind,
        bytes_.substr(textOffset, textLength),
        {
            {
                source_.Id(),
                beginOffset,
                beginLine,
                beginColumn
            },
            {
                source_.Id(),
                position_,
                line_,
                column_
            }
        }
    };
}

void Lexer::Advance()
{
    if (AtEnd())
    {
        return;
    }
    if (bytes_[position_] == '\r')
    {
        ++position_;
        if (!AtEnd() && bytes_[position_] == '\n')
        {
            ++position_;
        }
        ++line_;
        column_ = 1;
        return;
    }
    if (bytes_[position_] == '\n')
    {
        ++position_;
        ++line_;
        column_ = 1;
        return;
    }
    ++position_;
    ++column_;
}

bool Lexer::AtEnd() const noexcept
{
    return position_ >= bytes_.size();
}

char Lexer::Current() const noexcept
{
    return AtEnd() ? '\0' : bytes_[position_];
}

char Lexer::Peek(std::size_t distance) const noexcept
{
    const std::size_t index = position_ + distance;
    return index >= bytes_.size() ? '\0' : bytes_[index];
}

}
