#include <iostream>
#include <string>
#include <vector>

#include "diagnostic.hpp"
#include "lexer.hpp"
#include "parser_cursor.hpp"
#include "source_buffer.hpp"
#include "token.hpp"

namespace
{

bool CheckLexer()
{
    const std::string text =
        "# header\r\n"
        "owner = CHI\n"
        "neutrality < 50\n"
        "start = 1936.1.1\n"
        "name = \"quoted value\"\n"
        "block = { 1 nested = { active = yes } }\n";
    dillen::parser::SourceBuffer source(
        1,
        "probe/test.txt",
        "probe/test.txt",
        text
    );
    dillen::parser::DiagnosticBag diagnostics;
    dillen::parser::Lexer lexer(source, diagnostics);
    std::vector<dillen::parser::Token> tokens;
    do
    {
        tokens.push_back(lexer.Next());
    }
    while (tokens.back().kind != dillen::parser::TokenKind::End);

    using dillen::parser::TokenKind;
    return !diagnostics.HasErrors()
        && tokens.size() == 25
        && tokens[0].kind == TokenKind::Identifier
        && tokens[0].text == "owner"
        && tokens[4].kind == TokenKind::Less
        && tokens[5].kind == TokenKind::Number
        && tokens[8].kind == TokenKind::Date
        && tokens[11].kind == TokenKind::String
        && tokens[11].text == "quoted value"
        && tokens[0].span.begin.line == 2
        && tokens[0].span.begin.column == 1;
}

bool CheckCursor()
{
    const std::string text =
        "owner = CHI\n"
        "neutrality <= 50\n"
        "start = 1936.1.1\n"
        "enabled = yes\n"
        "ignored = { one two nested = { value = 3 } }\n";
    dillen::parser::SourceBuffer source(
        2,
        "probe/cursor.txt",
        "probe/cursor.txt",
        text
    );
    dillen::parser::DiagnosticBag diagnostics;
    dillen::parser::ParserCursor cursor(source, diagnostics);
    dillen::parser::Token token;
    dillen::parser::Token value;
    dillen::parser::RelationOperator relation;
    std::int64_t integer = 0;
    dillen::parser::ClausewitzDate date;
    bool enabled = false;

    if (!cursor.ReadKey(token)
        || token.text != "owner"
        || !cursor.ReadRelation(relation)
        || relation != dillen::parser::RelationOperator::Assign
        || !cursor.ReadScalar(value)
        || value.text != "CHI"
        || !cursor.ReadKey(token)
        || token.text != "neutrality"
        || !cursor.ReadRelation(relation)
        || relation != dillen::parser::RelationOperator::LessEqual
        || !cursor.ReadInt64(integer)
        || integer != 50
        || !cursor.ReadKey(token)
        || !cursor.ReadRelation(relation)
        || !cursor.ReadDate(date)
        || date.year != 1936
        || date.month != 1
        || date.day != 1
        || !cursor.ReadKey(token)
        || !cursor.ReadRelation(relation)
        || !cursor.ReadBool(enabled)
        || !enabled
        || !cursor.ReadKey(token)
        || token.text != "ignored"
        || !cursor.ReadRelation(relation)
        || !cursor.SkipBlock()
        || !cursor.AtEnd())
    {
        return false;
    }
    return !diagnostics.HasErrors();
}

bool CheckDiagnostics()
{
    dillen::parser::SourceBuffer source(
        3,
        "probe/bad.txt",
        "probe/bad.txt",
        "name = \"unterminated"
    );
    dillen::parser::DiagnosticBag diagnostics;
    dillen::parser::Lexer lexer(source, diagnostics);
    while (lexer.Next().kind != dillen::parser::TokenKind::End)
    {
    }
    return diagnostics.ErrorCount() == 1
        && diagnostics.All()[0].code == "lexer.unterminated_string"
        && diagnostics.All()[0].span.begin.line == 1;
}

}

int main()
{
    if (!CheckLexer())
    {
        std::cerr << "Parser lexer probe failed\n";
        return 1;
    }
    if (!CheckCursor())
    {
        std::cerr << "Parser cursor probe failed\n";
        return 2;
    }
    if (!CheckDiagnostics())
    {
        std::cerr << "Parser diagnostic probe failed\n";
        return 3;
    }
    std::cout << "Parser syntax probe: passed\n";
    return 0;
}
