#include "authoring_parser.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <functional>
#include <initializer_list>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace dillen::authoring {

namespace {

using parser::ParserCursor;
using parser::RelationOperator;
using parser::SourceSpan;
using parser::Token;
using parser::TokenKind;

struct SyntaxNode;

struct SyntaxNode
{
    bool block = false;
    Token scalar;
    SourceSpan span;
    std::vector<SyntaxNode> values;
    std::vector<Token> keys;
    std::vector<Token> items;
};

struct ParsedRoot
{
    Token keyword;
    SyntaxNode body;
};

bool ParseNode(ParserCursor& cursor, SyntaxNode& output);

bool ParseBlock(ParserCursor& cursor, SyntaxNode& output)
{
    Token open;
    if (!cursor.Expect(TokenKind::LeftBrace, &open, "for a block"))
    {
        return false;
    }
    output.block = true;
    output.span.begin = open.span.begin;
    while (!cursor.ConsumeIf(TokenKind::RightBrace, &open))
    {
        if (cursor.AtEnd())
        {
            cursor.Diagnostics().Error(
                "dillen.authoring.unterminated_block",
                "unexpected end of file inside authoring block",
                output.span
            );
            return false;
        }
        Token key;
        if (!cursor.ReadKey(key))
        {
            return false;
        }
        if (!parser::IsRelationToken(cursor.Peek().kind))
        {
            output.items.push_back(key);
            continue;
        }
        RelationOperator relation;
        if (!cursor.ReadRelation(relation))
        {
            return false;
        }
        if (relation != RelationOperator::Assign)
        {
            cursor.Diagnostics().Error(
                "dillen.authoring.assignment_required",
                "authoring properties must use '='",
                key.span
            );
            return false;
        }
        SyntaxNode value;
        if (!ParseNode(cursor, value))
        {
            return false;
        }
        output.keys.push_back(key);
        output.values.push_back(std::move(value));
    }
    output.span.end = open.span.end;
    return true;
}

bool ParseNode(ParserCursor& cursor, SyntaxNode& output)
{
    if (cursor.Peek().kind == TokenKind::LeftBrace)
    {
        return ParseBlock(cursor, output);
    }
    Token scalar;
    if (!cursor.ReadScalar(scalar))
    {
        return false;
    }
    output.scalar = scalar;
    output.span = scalar.span;
    return true;
}

bool ParseRoot(ParserCursor& cursor, ParsedRoot& output)
{
    if (!cursor.ReadKey(output.keyword))
    {
        return false;
    }
    RelationOperator relation;
    if (!cursor.ReadRelation(relation)
        || relation != RelationOperator::Assign)
    {
        cursor.Diagnostics().Error(
            "dillen.authoring.root_assignment_required",
            "authoring document root must use '='",
            output.keyword.span
        );
        return false;
    }
    return ParseBlock(cursor, output.body);
}

bool IsKeyAllowed(
    std::string_view key,
    std::initializer_list<std::string_view> allowed
)
{
    return std::find(allowed.begin(), allowed.end(), key)
        != allowed.end();
}

bool RejectUnknown(
    const SyntaxNode& block,
    std::initializer_list<std::string_view> allowed,
    ParserCursor& cursor,
    std::string_view context
)
{
    for (const Token& key : block.keys)
    {
        if (!IsKeyAllowed(key.text, allowed))
        {
            cursor.Diagnostics().Error(
                "dillen.authoring.unknown_property",
                "unknown " + std::string(context)
                    + " property: " + std::string(key.text),
                key.span
            );
            return false;
        }
    }
    if (!block.items.empty())
    {
        cursor.Diagnostics().Error(
            "dillen.authoring.unexpected_bare_value",
            "bare values are not allowed in " + std::string(context),
            block.items.front().span
        );
        return false;
    }
    return true;
}

std::vector<std::size_t> FindAll(
    const SyntaxNode& block,
    std::string_view key
)
{
    std::vector<std::size_t> matches;
    for (std::size_t index = 0; index < block.keys.size(); ++index)
    {
        if (block.keys[index].text == key)
        {
            matches.push_back(index);
        }
    }
    return matches;
}

const SyntaxNode* FindUnique(
    const SyntaxNode& block,
    std::string_view key,
    ParserCursor& cursor,
    bool required
)
{
    const std::vector<std::size_t> matches = FindAll(block, key);
    if (matches.size() > 1)
    {
        cursor.Diagnostics().Error(
            "dillen.authoring.duplicate_property",
            "property appears more than once: " + std::string(key),
            block.keys[matches[1]].span
        );
        return nullptr;
    }
    if (matches.empty())
    {
        if (required)
        {
            cursor.Diagnostics().Error(
                "dillen.authoring.required_property_missing",
                "required property is missing: " + std::string(key),
                block.span
            );
        }
        return nullptr;
    }
    return &block.values[matches.front()];
}

bool RequireScalar(
    const SyntaxNode* node,
    ParserCursor& cursor,
    std::string_view property,
    Token& output
)
{
    if (node == nullptr)
    {
        return false;
    }
    if (node->block)
    {
        cursor.Diagnostics().Error(
            "dillen.authoring.scalar_required",
            "property requires a scalar value: " + std::string(property),
            node->span
        );
        return false;
    }
    output = node->scalar;
    return true;
}

bool ReadStringProperty(
    const SyntaxNode& block,
    std::string_view key,
    ParserCursor& cursor,
    std::string& output,
    bool required = true
)
{
    const SyntaxNode* node = FindUnique(block, key, cursor, required);
    if (node == nullptr)
    {
        return !required && !cursor.Diagnostics().HasErrors();
    }
    Token token;
    if (!RequireScalar(node, cursor, key, token))
    {
        return false;
    }
    output = std::string(token.text);
    return true;
}

bool ParseUnsigned(
    const Token& token,
    std::uint64_t maximum,
    std::uint64_t& output,
    ParserCursor& cursor
)
{
    std::string text(token.text);
    errno = 0;
    char* end = nullptr;
    const unsigned long long value = std::strtoull(
        text.c_str(),
        &end,
        0
    );
    if (errno == ERANGE
        || end != text.c_str() + text.size()
        || value > maximum)
    {
        cursor.Diagnostics().Error(
            "dillen.authoring.unsigned_integer_required",
            "expected a non-negative integer in range",
            token.span
        );
        return false;
    }
    output = static_cast<std::uint64_t>(value);
    return true;
}

bool ReadUInt32Property(
    const SyntaxNode& block,
    std::string_view key,
    ParserCursor& cursor,
    std::uint32_t& output,
    bool required = true
)
{
    const SyntaxNode* node = FindUnique(block, key, cursor, required);
    if (node == nullptr)
    {
        return !required && !cursor.Diagnostics().HasErrors();
    }
    Token token;
    std::uint64_t value = 0;
    if (!RequireScalar(node, cursor, key, token)
        || !ParseUnsigned(
            token,
            std::numeric_limits<std::uint32_t>::max(),
            value,
            cursor))
    {
        return false;
    }
    output = static_cast<std::uint32_t>(value);
    return true;
}

bool ReadInt32Property(
    const SyntaxNode& block,
    std::string_view key,
    ParserCursor& cursor,
    std::int32_t& output,
    bool required = true
)
{
    const SyntaxNode* node = FindUnique(block, key, cursor, required);
    if (node == nullptr)
    {
        return !required && !cursor.Diagnostics().HasErrors();
    }
    Token token;
    if (!RequireScalar(node, cursor, key, token))
    {
        return false;
    }
    std::string text(token.text);
    errno = 0;
    char* end = nullptr;
    const long long value = std::strtoll(text.c_str(), &end, 10);
    if (errno == ERANGE
        || end != text.c_str() + text.size()
        || value < std::numeric_limits<std::int32_t>::min()
        || value > std::numeric_limits<std::int32_t>::max())
    {
        cursor.Diagnostics().Error(
            "dillen.authoring.signed_integer_required",
            "expected a signed 32-bit integer",
            token.span
        );
        return false;
    }
    output = static_cast<std::int32_t>(value);
    return true;
}

bool ParseBooleanToken(
    const Token& token,
    bool& output,
    ParserCursor& cursor
)
{
    if (token.text == "yes" || token.text == "true")
    {
        output = true;
        return true;
    }
    if (token.text == "no" || token.text == "false")
    {
        output = false;
        return true;
    }
    cursor.Diagnostics().Error(
        "dillen.authoring.boolean_required",
        "expected yes/no or true/false",
        token.span
    );
    return false;
}

bool ReadBoolProperty(
    const SyntaxNode& block,
    std::string_view key,
    ParserCursor& cursor,
    bool& output,
    bool required = true
)
{
    const SyntaxNode* node = FindUnique(block, key, cursor, required);
    if (node == nullptr)
    {
        return !required && !cursor.Diagnostics().HasErrors();
    }
    Token token;
    return RequireScalar(node, cursor, key, token)
        && ParseBooleanToken(token, output, cursor);
}

bool ParseDoubleToken(
    const Token& token,
    double& output,
    ParserCursor& cursor
)
{
    std::string text(token.text);
    errno = 0;
    char* end = nullptr;
    output = std::strtod(text.c_str(), &end);
    if (errno == ERANGE || end != text.c_str() + text.size())
    {
        cursor.Diagnostics().Error(
            "dillen.authoring.number_required",
            "expected a numeric value",
            token.span
        );
        return false;
    }
    return true;
}

std::optional<kernel::MechanismValueKind> ParseValueKind(
    const Token& token,
    ParserCursor& cursor
)
{
    if (token.text == "null") return kernel::MechanismValueKind::Null;
    if (token.text == "boolean") return kernel::MechanismValueKind::Boolean;
    if (token.text == "integer") return kernel::MechanismValueKind::Integer;
    if (token.text == "decimal") return kernel::MechanismValueKind::Decimal;
    if (token.text == "string") return kernel::MechanismValueKind::String;
    if (token.text == "reference") return kernel::MechanismValueKind::Reference;
    if (token.text == "list") return kernel::MechanismValueKind::List;
    if (token.text == "object") return kernel::MechanismValueKind::Object;
    cursor.Diagnostics().Error(
        "dillen.authoring.value_kind_unknown",
        "unknown mechanism value kind: " + std::string(token.text),
        token.span
    );
    return std::nullopt;
}

std::optional<kernel::MechanismReferenceKind> ParseReferenceKind(
    const Token& token,
    ParserCursor& cursor
)
{
    if (token.text == "entity") return kernel::MechanismReferenceKind::Entity;
    if (token.text == "mechanism_definition")
        return kernel::MechanismReferenceKind::MechanismDefinition;
    if (token.text == "mechanism_instance")
        return kernel::MechanismReferenceKind::MechanismInstance;
    if (token.text == "resource")
        return kernel::MechanismReferenceKind::Resource;
    if (token.text == "custom") return kernel::MechanismReferenceKind::Custom;
    cursor.Diagnostics().Error(
        "dillen.authoring.reference_kind_unknown",
        "unknown mechanism reference kind: " + std::string(token.text),
        token.span
    );
    return std::nullopt;
}

bool ParseTypedScalar(
    const Token& token,
    kernel::MechanismValueKind kind,
    kernel::MechanismValue& output,
    ParserCursor& cursor
)
{
    switch (kind)
    {
    case kernel::MechanismValueKind::Null:
        if (token.text == "null")
        {
            output = {};
            return true;
        }
        break;
    case kernel::MechanismValueKind::Boolean:
    {
        bool value = false;
        if (ParseBooleanToken(token, value, cursor))
        {
            output = kernel::MechanismValue(value);
            return true;
        }
        return false;
    }
    case kernel::MechanismValueKind::Integer:
    {
        std::string text(token.text);
        errno = 0;
        char* end = nullptr;
        const long long value = std::strtoll(text.c_str(), &end, 10);
        if (errno != ERANGE && end == text.c_str() + text.size())
        {
            output = kernel::MechanismValue(
                static_cast<std::int64_t>(value)
            );
            return true;
        }
        break;
    }
    case kernel::MechanismValueKind::Decimal:
    {
        double value = 0.0;
        if (ParseDoubleToken(token, value, cursor))
        {
            output = kernel::MechanismValue(value);
            return true;
        }
        return false;
    }
    case kernel::MechanismValueKind::String:
        output = kernel::MechanismValue(std::string(token.text));
        return true;
    case kernel::MechanismValueKind::Reference:
    case kernel::MechanismValueKind::List:
    case kernel::MechanismValueKind::Object:
        cursor.Diagnostics().Error(
            "dillen.authoring.complex_value_not_scalar",
            "complex values require a future structured authoring form",
            token.span
        );
        return false;
    }
    cursor.Diagnostics().Error(
        "dillen.authoring.typed_value_invalid",
        "scalar value does not match the declared field kind",
        token.span
    );
    return false;
}

bool InferScalarValue(
    const Token& token,
    kernel::MechanismValue& output,
    ParserCursor& cursor
)
{
    if (token.kind == TokenKind::String)
    {
        output = kernel::MechanismValue(std::string(token.text));
        return true;
    }
    if (token.text == "yes" || token.text == "true"
        || token.text == "no" || token.text == "false")
    {
        bool value = false;
        return ParseBooleanToken(token, value, cursor)
            && (output = kernel::MechanismValue(value), true);
    }
    if (token.text == "null")
    {
        output = {};
        return true;
    }
    if (token.kind == TokenKind::Number)
    {
        if (token.text.find('.') != std::string_view::npos
            || token.text.find('e') != std::string_view::npos
            || token.text.find('E') != std::string_view::npos)
        {
            return ParseTypedScalar(
                token,
                kernel::MechanismValueKind::Decimal,
                output,
                cursor
            );
        }
        return ParseTypedScalar(
            token,
            kernel::MechanismValueKind::Integer,
            output,
            cursor
        );
    }
    output = kernel::MechanismValue(std::string(token.text));
    return true;
}

bool ParseFieldSchema(
    const SyntaxNode& node,
    kernel::MechanismFieldSchema& output,
    ParserCursor& cursor
)
{
    if (!node.block
        || !RejectUnknown(
            node,
            {
                "name", "kind", "required", "default",
                "minimum_number", "maximum_number",
                "minimum_size", "maximum_size",
                "list_element_kind", "reference_kind",
                "reference_type"
            },
            cursor,
            "field schema"))
    {
        return false;
    }
    std::string kindName;
    if (!ReadStringProperty(node, "name", cursor, output.name)
        || !ReadStringProperty(node, "kind", cursor, kindName))
    {
        return false;
    }
    Token kindToken;
    kindToken.text = kindName;
    kindToken.span = node.span;
    const auto kind = ParseValueKind(kindToken, cursor);
    if (!kind)
    {
        return false;
    }
    output.kind = *kind;
    if (FindUnique(node, "required", cursor, false) != nullptr
        && !ReadBoolProperty(
            node,
            "required",
            cursor,
            output.required))
    {
        return false;
    }
    const SyntaxNode* defaultNode = FindUnique(
        node,
        "default",
        cursor,
        false
    );
    if (defaultNode != nullptr)
    {
        Token token;
        kernel::MechanismValue value;
        if (!RequireScalar(defaultNode, cursor, "default", token)
            || !ParseTypedScalar(token, output.kind, value, cursor))
        {
            return false;
        }
        output.defaultValue = std::move(value);
    }
    const auto readOptionalDouble = [&](std::string_view key,
                                        std::optional<double>& target)
    {
        const SyntaxNode* value = FindUnique(node, key, cursor, false);
        if (value == nullptr)
        {
            return !cursor.Diagnostics().HasErrors();
        }
        Token token;
        double parsed = 0.0;
        if (!RequireScalar(value, cursor, key, token)
            || !ParseDoubleToken(token, parsed, cursor))
        {
            return false;
        }
        target = parsed;
        return true;
    };
    if (!readOptionalDouble("minimum_number", output.minimumNumber)
        || !readOptionalDouble("maximum_number", output.maximumNumber))
    {
        return false;
    }
    const auto readOptionalSize = [&](std::string_view key,
                                      std::optional<std::size_t>& target)
    {
        const SyntaxNode* value = FindUnique(node, key, cursor, false);
        if (value == nullptr)
        {
            return !cursor.Diagnostics().HasErrors();
        }
        Token token;
        std::uint64_t parsed = 0;
        if (!RequireScalar(value, cursor, key, token)
            || !ParseUnsigned(
                token,
                std::numeric_limits<std::size_t>::max(),
                parsed,
                cursor))
        {
            return false;
        }
        target = static_cast<std::size_t>(parsed);
        return true;
    };
    if (!readOptionalSize("minimum_size", output.minimumSize)
        || !readOptionalSize("maximum_size", output.maximumSize))
    {
        return false;
    }
    const SyntaxNode* elementKind = FindUnique(
        node,
        "list_element_kind",
        cursor,
        false
    );
    if (elementKind != nullptr)
    {
        Token token;
        if (!RequireScalar(
                elementKind,
                cursor,
                "list_element_kind",
                token))
        {
            return false;
        }
        output.listElementKind = ParseValueKind(token, cursor);
        if (!output.listElementKind)
        {
            return false;
        }
    }
    const SyntaxNode* referenceKind = FindUnique(
        node,
        "reference_kind",
        cursor,
        false
    );
    if (referenceKind != nullptr)
    {
        Token token;
        if (!RequireScalar(
                referenceKind,
                cursor,
                "reference_kind",
                token))
        {
            return false;
        }
        output.referenceKind = ParseReferenceKind(token, cursor);
        if (!output.referenceKind)
        {
            return false;
        }
    }
    const SyntaxNode* referenceType = FindUnique(
        node,
        "reference_type",
        cursor,
        false
    );
    if (referenceType != nullptr)
    {
        Token token;
        std::uint64_t value = 0;
        if (!RequireScalar(
                referenceType,
                cursor,
                "reference_type",
                token)
            || !ParseUnsigned(
                token,
                std::numeric_limits<std::uint64_t>::max(),
                value,
                cursor))
        {
            return false;
        }
        output.referenceType = value;
    }
    return true;
}

bool ParseRoleSchema(
    const SyntaxNode& node,
    kernel::MechanismRoleSchema& output,
    ParserCursor& cursor
)
{
    if (!node.block
        || !RejectUnknown(
            node,
            {
                "name", "reference_kind", "reference_type",
                "minimum_count", "maximum_count"
            },
            cursor,
            "role schema"))
    {
        return false;
    }
    std::string referenceKind;
    if (!ReadStringProperty(node, "name", cursor, output.name)
        || !ReadStringProperty(
            node,
            "reference_kind",
            cursor,
            referenceKind))
    {
        return false;
    }
    Token kindToken;
    kindToken.text = referenceKind;
    kindToken.span = node.span;
    const auto parsedKind = ParseReferenceKind(kindToken, cursor);
    if (!parsedKind)
    {
        return false;
    }
    output.referenceKind = *parsedKind;
    std::uint32_t minimum = 0;
    if (FindUnique(node, "minimum_count", cursor, false) != nullptr)
    {
        if (!ReadUInt32Property(
                node,
                "minimum_count",
                cursor,
                minimum))
        {
            return false;
        }
        output.minimumCount = minimum;
    }
    if (FindUnique(node, "maximum_count", cursor, false) != nullptr)
    {
        std::uint32_t maximum = 0;
        if (!ReadUInt32Property(
                node,
                "maximum_count",
                cursor,
                maximum))
        {
            return false;
        }
        output.maximumCount = maximum;
    }
    const SyntaxNode* referenceType = FindUnique(
        node,
        "reference_type",
        cursor,
        false
    );
    if (referenceType != nullptr)
    {
        Token token;
        std::uint64_t value = 0;
        if (!RequireScalar(
                referenceType,
                cursor,
                "reference_type",
                token)
            || !ParseUnsigned(
                token,
                std::numeric_limits<std::uint64_t>::max(),
                value,
                cursor))
        {
            return false;
        }
        output.referenceType = value;
    }
    return true;
}

bool ParseScalarFieldMap(
    const SyntaxNode& node,
    std::map<std::string, kernel::MechanismValue>& output,
    ParserCursor& cursor
)
{
    if (!node.block || !node.items.empty())
    {
        cursor.Diagnostics().Error(
            "dillen.authoring.field_map_required",
            "fields must be a property block",
            node.span
        );
        return false;
    }
    for (std::size_t index = 0; index < node.keys.size(); ++index)
    {
        const SyntaxNode& valueNode = node.values[index];
        if (valueNode.block)
        {
            cursor.Diagnostics().Error(
                "dillen.authoring.scalar_field_required",
                "initial field values are scalar in the current authoring format",
                valueNode.span
            );
            return false;
        }
        kernel::MechanismValue value;
        if (!InferScalarValue(valueNode.scalar, value, cursor)
            || !output.emplace(
                std::string(node.keys[index].text),
                std::move(value)).second)
        {
            cursor.Diagnostics().Error(
                "dillen.authoring.duplicate_field",
                "initial field appears more than once",
                node.keys[index].span
            );
            return false;
        }
    }
    return true;
}

std::optional<kernel::AlgorithmBackend> ParseBackend(
    const Token& token,
    ParserCursor& cursor
)
{
    if (token.text == "declarative") return kernel::AlgorithmBackend::Declarative;
    if (token.text == "script") return kernel::AlgorithmBackend::Script;
    if (token.text == "native") return kernel::AlgorithmBackend::Native;
    cursor.Diagnostics().Error(
        "dillen.authoring.algorithm_backend_unknown",
        "unknown algorithm backend: " + std::string(token.text),
        token.span
    );
    return std::nullopt;
}

std::optional<kernel::AlgorithmFailurePolicy> ParseFailurePolicy(
    const Token& token,
    ParserCursor& cursor
)
{
    if (token.text == "isolate_instance")
        return kernel::AlgorithmFailurePolicy::IsolateInstance;
    if (token.text == "pause_instance")
        return kernel::AlgorithmFailurePolicy::PauseInstance;
    if (token.text == "fail_instance")
        return kernel::AlgorithmFailurePolicy::FailInstance;
    cursor.Diagnostics().Error(
        "dillen.authoring.algorithm_failure_policy_unknown",
        "unknown algorithm failure policy: " + std::string(token.text),
        token.span
    );
    return std::nullopt;
}

bool ParseAlgorithmExecutionPolicy(
    const SyntaxNode& node,
    kernel::AlgorithmExecutionPolicy& output,
    ParserCursor& cursor
)
{
    if (!node.block
        || !node.items.empty()
        || !RejectUnknown(
            node,
            {
                "instruction_budget",
                "script_slice_instruction_budget",
                "script_memory_limit_bytes",
                "wall_clock_warning_microseconds",
                "timeout_microseconds", "failure_policy"
            },
            cursor,
            "algorithm execution policy"))
    {
        return false;
    }
    if (FindUnique(
            node,
            "instruction_budget",
            cursor,
            false) != nullptr
        && !ReadUInt32Property(
            node,
            "instruction_budget",
            cursor,
            output.instructionBudget))
    {
        return false;
    }
    if (FindUnique(
            node,
            "script_slice_instruction_budget",
            cursor,
            false) != nullptr
        && !ReadUInt32Property(
            node,
            "script_slice_instruction_budget",
            cursor,
            output.scriptSliceInstructionBudget))
    {
        return false;
    }
    if (FindUnique(
            node,
            "script_memory_limit_bytes",
            cursor,
            false) != nullptr
        && !ReadUInt32Property(
            node,
            "script_memory_limit_bytes",
            cursor,
            output.scriptMemoryLimitBytes))
    {
        return false;
    }
    const SyntaxNode* wallClockWarning = FindUnique(
        node,
        "wall_clock_warning_microseconds",
        cursor,
        false
    );
    const SyntaxNode* legacyTimeout = FindUnique(
        node,
        "timeout_microseconds",
        cursor,
        false
    );
    if (wallClockWarning != nullptr && legacyTimeout != nullptr)
    {
        cursor.Diagnostics().Error(
            "dillen.authoring.algorithm_wall_clock_policy_conflict",
            "wall_clock_warning_microseconds and the legacy "
                "timeout_microseconds alias are mutually exclusive",
            node.span
        );
        return false;
    }
    if (wallClockWarning != nullptr
        && !ReadUInt32Property(
            node,
            "wall_clock_warning_microseconds",
            cursor,
            output.wallClockWarningMicroseconds))
    {
        return false;
    }
    if (legacyTimeout != nullptr
        && !ReadUInt32Property(
            node,
            "timeout_microseconds",
            cursor,
            output.wallClockWarningMicroseconds))
    {
        return false;
    }
    const SyntaxNode* failurePolicy = FindUnique(
        node,
        "failure_policy",
        cursor,
        false
    );
    if (failurePolicy != nullptr)
    {
        Token token;
        if (!RequireScalar(
                failurePolicy,
                cursor,
                "failure_policy",
                token))
        {
            return false;
        }
        const auto parsed = ParseFailurePolicy(token, cursor);
        if (!parsed)
        {
            return false;
        }
        output.failurePolicy = *parsed;
    }
    if (!kernel::IsValidAlgorithmExecutionPolicy(output))
    {
        cursor.Diagnostics().Error(
            "dillen.authoring.algorithm_execution_policy_invalid",
            "algorithm instruction budget must be greater than zero",
            node.span
        );
        return false;
    }
    return true;
}

std::optional<kernel::AlgorithmEntryPoint> ParseEntryPoint(
    const Token& token,
    ParserCursor& cursor
)
{
    if (token.text == "create") return kernel::AlgorithmEntryPoint::Create;
    if (token.text == "tick") return kernel::AlgorithmEntryPoint::Tick;
    if (token.text == "event") return kernel::AlgorithmEntryPoint::Event;
    if (token.text == "command") return kernel::AlgorithmEntryPoint::Command;
    if (token.text == "destroy") return kernel::AlgorithmEntryPoint::Destroy;
    cursor.Diagnostics().Error(
        "dillen.authoring.algorithm_entry_unknown",
        "unknown algorithm entry point: " + std::string(token.text),
        token.span
    );
    return std::nullopt;
}

std::optional<kernel::MechanismLifecycleState> ParseLifecycleState(
    const Token& token,
    ParserCursor& cursor
)
{
    if (token.text == "created")
        return kernel::MechanismLifecycleState::Created;
    if (token.text == "active")
        return kernel::MechanismLifecycleState::Active;
    if (token.text == "paused")
        return kernel::MechanismLifecycleState::Paused;
    if (token.text == "completed")
        return kernel::MechanismLifecycleState::Completed;
    if (token.text == "failed")
        return kernel::MechanismLifecycleState::Failed;
    cursor.Diagnostics().Error(
        "dillen.authoring.lifecycle_state_unknown",
        "unknown mechanism lifecycle state: " + std::string(token.text),
        token.span
    );
    return std::nullopt;
}

bool ParseAlgorithmFieldInstruction(
    const SyntaxNode& node,
    kernel::AlgorithmInstructionKind kind,
    kernel::AlgorithmInstructionDefinition& output,
    ParserCursor& cursor
)
{
    if (!node.block
        || !RejectUnknown(
            node,
            {"field", "value", "when"},
            cursor,
            "algorithm field instruction"))
    {
        return false;
    }
    std::string field;
    const SyntaxNode* valueNode = FindUnique(
        node,
        "value",
        cursor,
        true
    );
    Token valueToken;
    kernel::MechanismValue value;
    if (!ReadStringProperty(node, "field", cursor, field)
        || !RequireScalar(valueNode, cursor, "value", valueToken)
        || !InferScalarValue(valueToken, value, cursor))
    {
        return false;
    }
    output = kind == kernel::AlgorithmInstructionKind::SetField
        ? kernel::AlgorithmInstructionDefinition::SetField(
            std::move(field),
            std::move(value)
        )
        : kernel::AlgorithmInstructionDefinition::AddField(
            std::move(field),
            std::move(value)
        );
    const SyntaxNode* when = FindUnique(node, "when", cursor, false);
    if (when != nullptr)
    {
        if (!when->block || !when->items.empty())
        {
            return false;
        }
        for (std::size_t index = 0; index < when->keys.size(); ++index)
        {
            const Token& name = when->keys[index];
            const SyntaxNode& conditionNode = when->values[index];
            kernel::AlgorithmConditionDefinition condition;
            if (name.text == "field_equals")
            {
                if (!conditionNode.block
                    || !RejectUnknown(
                        conditionNode,
                        {"field", "value"},
                        cursor,
                        "field condition"))
                {
                    return false;
                }
                const SyntaxNode* conditionValue = FindUnique(
                    conditionNode,
                    "value",
                    cursor,
                    true
                );
                Token token;
                condition.kind =
                    kernel::AlgorithmConditionKind::SelfFieldEquals;
                if (!ReadStringProperty(
                        conditionNode,
                        "field",
                        cursor,
                        condition.field)
                    || !RequireScalar(
                        conditionValue,
                        cursor,
                        "value",
                        token)
                    || !InferScalarValue(token, condition.value, cursor))
                {
                    return false;
                }
            }
            else if (name.text == "query_at_least")
            {
                if (!conditionNode.block
                    || !RejectUnknown(
                        conditionNode,
                        {"kind", "type", "count"},
                        cursor,
                        "query condition"))
                {
                    return false;
                }
                std::string queryKindName;
                std::string type;
                std::uint32_t count = 0;
                condition.kind =
                    kernel::AlgorithmConditionKind::QueryCountAtLeast;
                if (!ReadStringProperty(
                        conditionNode,
                        "kind",
                        cursor,
                        queryKindName)
                    || !ReadStringProperty(
                        conditionNode,
                        "type",
                        cursor,
                        type)
                    || !ReadUInt32Property(
                        conditionNode,
                        "count",
                        cursor,
                        count))
                {
                    return false;
                }
                condition.minimumCount = count;
                if (queryKindName == "entity")
                {
                    condition.queryKind = kernel::AlgorithmQueryKind::EntityType;
                    condition.queryType =
                        kernel::StableEntityTypeId(type).value;
                }
                else if (queryKindName == "component")
                {
                    condition.queryKind =
                        kernel::AlgorithmQueryKind::ComponentType;
                    condition.queryType =
                        kernel::StableComponentTypeId(type).value;
                }
                else if (queryKindName == "relation")
                {
                    condition.queryKind =
                        kernel::AlgorithmQueryKind::RelationType;
                    condition.queryType =
                        kernel::StableRelationTypeId(type).value;
                }
                else if (queryKindName == "mechanism")
                {
                    condition.queryKind =
                        kernel::AlgorithmQueryKind::MechanismType;
                    condition.queryType =
                        kernel::StableMechanismTypeId(type).value;
                }
                else
                {
                    cursor.Diagnostics().Error(
                        "dillen.authoring.query_kind_unknown",
                        "unknown declarative Query kind: " + queryKindName,
                        conditionNode.span
                    );
                    return false;
                }
            }
            else if (name.text == "scheduled_event")
            {
                Token token;
                if (!RequireScalar(
                        &conditionNode,
                        cursor,
                        "scheduled_event",
                        token))
                {
                    return false;
                }
                condition.kind = kernel::AlgorithmConditionKind::
                    ScheduledEventTypeEquals;
                condition.eventType = kernel::StableAlgorithmEventTypeId(
                    token.text
                );
            }
            else if (name.text == "rng_modulo")
            {
                if (!conditionNode.block
                    || !RejectUnknown(
                        conditionNode,
                        {"stream", "offset", "modulo", "equals"},
                        cursor,
                        "RNG condition"))
                {
                    return false;
                }
                std::string stream;
                std::uint32_t offset = 0;
                std::uint32_t modulo = 0;
                std::uint32_t equals = 0;
                condition.kind =
                    kernel::AlgorithmConditionKind::RngModuloEquals;
                if (!ReadStringProperty(
                        conditionNode,
                        "stream",
                        cursor,
                        stream)
                    || !ReadUInt32Property(
                        conditionNode,
                        "modulo",
                        cursor,
                        modulo)
                    || !ReadUInt32Property(
                        conditionNode,
                        "equals",
                        cursor,
                        equals))
                {
                    return false;
                }
                if (FindUnique(
                        conditionNode,
                        "offset",
                        cursor,
                        false) != nullptr
                    && !ReadUInt32Property(
                        conditionNode,
                        "offset",
                        cursor,
                        offset))
                {
                    return false;
                }
                condition.rngStream = kernel::StableRngStreamId(stream);
                condition.rngOffset = offset;
                condition.rngModulo = modulo;
                condition.rngEquals = equals;
            }
            else
            {
                cursor.Diagnostics().Error(
                    "dillen.authoring.algorithm_condition_unknown",
                    "unknown declarative condition: "
                        + std::string(name.text),
                    name.span
                );
                return false;
            }
            output.conditions.push_back(std::move(condition));
        }
    }
    return true;
}

bool ParseGenericAlgorithmInstruction(
    std::string_view name,
    const SyntaxNode& node,
    kernel::AlgorithmInstructionDefinition& output,
    ParserCursor& cursor
)
{
    if (!node.block || !node.items.empty())
    {
        return false;
    }
    const auto scalarValue = [&node, &cursor](
        std::string_view property,
        kernel::MechanismValue& value)
    {
        const SyntaxNode* valueNode = FindUnique(
            node,
            property,
            cursor,
            true
        );
        Token token;
        return RequireScalar(valueNode, cursor, property, token)
            && InferScalarValue(token, value, cursor);
    };
    if (name == "create_entity")
    {
        if (!RejectUnknown(
                node,
                {"entity_type", "definition"},
                cursor,
                "create entity instruction")) return false;
        std::string type;
        std::string definition;
        output.kind = kernel::AlgorithmInstructionKind::CreateEntity;
        if (!ReadStringProperty(node, "entity_type", cursor, type)
            || !ReadStringProperty(
                node,
                "definition",
                cursor,
                definition)) return false;
        output.entityDefinition = kernel::StableEntityDefinitionId(
            kernel::StableEntityTypeId(type),
            definition
        );
        return true;
    }
    if (name == "set_component_field")
    {
        if (!RejectUnknown(
                node,
                {
                    "owner_entity_type", "owner_definition",
                    "component", "field", "value"
                },
                cursor,
                "set component field instruction")) return false;
        std::string ownerType;
        std::string ownerDefinition;
        std::string component;
        output.kind = kernel::AlgorithmInstructionKind::SetComponentField;
        if (!ReadStringProperty(
                node,
                "owner_entity_type",
                cursor,
                ownerType)
            || !ReadStringProperty(
                node,
                "owner_definition",
                cursor,
                ownerDefinition)
            || !ReadStringProperty(node, "component", cursor, component)
            || !ReadStringProperty(
                node,
                "field",
                cursor,
                output.componentField)
            || !scalarValue("value", output.operand)) return false;
        output.entity = kernel::StableEntityId(
            kernel::StableEntityDefinitionId(
                kernel::StableEntityTypeId(ownerType),
                ownerDefinition
            )
        );
        output.component = kernel::StableComponentTypeId(component);
        return true;
    }
    if (name == "add_relation")
    {
        if (!RejectUnknown(
                node,
                {
                    "relation", "source_entity_type",
                    "source_definition", "target_entity_type",
                    "target_definition"
                },
                cursor,
                "add relation instruction")) return false;
        std::string relation;
        std::string sourceType;
        std::string sourceDefinition;
        std::string targetType;
        std::string targetDefinition;
        output.kind = kernel::AlgorithmInstructionKind::AddRelation;
        if (!ReadStringProperty(node, "relation", cursor, relation)
            || !ReadStringProperty(
                node,
                "source_entity_type",
                cursor,
                sourceType)
            || !ReadStringProperty(
                node,
                "source_definition",
                cursor,
                sourceDefinition)
            || !ReadStringProperty(
                node,
                "target_entity_type",
                cursor,
                targetType)
            || !ReadStringProperty(
                node,
                "target_definition",
                cursor,
                targetDefinition)) return false;
        output.relationType = kernel::StableRelationTypeId(relation);
        output.sourceEntity = kernel::StableEntityId(
            kernel::StableEntityDefinitionId(
                kernel::StableEntityTypeId(sourceType),
                sourceDefinition
            )
        );
        output.targetEntity = kernel::StableEntityId(
            kernel::StableEntityDefinitionId(
                kernel::StableEntityTypeId(targetType),
                targetDefinition
            )
        );
        return true;
    }
    if (name == "spawn_mechanism")
    {
        if (!RejectUnknown(
                node,
                {"mechanism", "definition", "spawn"},
                cursor,
                "spawn mechanism instruction")) return false;
        std::string mechanism;
        std::string definition;
        std::string spawn;
        output.kind = kernel::AlgorithmInstructionKind::SpawnMechanism;
        if (!ReadStringProperty(node, "mechanism", cursor, mechanism)
            || !ReadStringProperty(node, "definition", cursor, definition)
            || !ReadStringProperty(node, "spawn", cursor, spawn))
        {
            return false;
        }
        output.spawn = kernel::StableMechanismSpawnDefinitionId(
            kernel::StableMechanismDefinitionId(
                kernel::StableMechanismTypeId(mechanism),
                definition
            ),
            spawn
        );
        return true;
    }
    if (name == "schedule_event")
    {
        if (!RejectUnknown(
                node,
                {"type", "delay", "priority", "payload"},
                cursor,
                "schedule event instruction")) return false;
        std::string eventType;
        std::uint32_t delay = 0;
        output.kind = kernel::AlgorithmInstructionKind::ScheduleEvent;
        if (!ReadStringProperty(node, "type", cursor, eventType)
            || !ReadUInt32Property(node, "delay", cursor, delay)
            || !scalarValue("payload", output.payload)) return false;
        if (FindUnique(node, "priority", cursor, false) != nullptr
            && !ReadInt32Property(
                node,
                "priority",
                cursor,
                output.priority)) return false;
        output.eventType = kernel::StableAlgorithmEventTypeId(eventType);
        output.dueTickOffset = delay;
        return true;
    }
    if (name == "create_rng" || name == "advance_rng")
    {
        const bool create = name == "create_rng";
        if (!RejectUnknown(
                node,
                create
                    ? std::initializer_list<std::string_view>{"stream", "seed"}
                    : std::initializer_list<std::string_view>{"stream", "count"},
                cursor,
                create ? "create RNG instruction" : "advance RNG instruction"))
        {
            return false;
        }
        std::string stream;
        std::uint32_t value = 0;
        output.kind = create
            ? kernel::AlgorithmInstructionKind::CreateRngStream
            : kernel::AlgorithmInstructionKind::AdvanceRngStream;
        if (!ReadStringProperty(node, "stream", cursor, stream)
            || !ReadUInt32Property(
                node,
                create ? "seed" : "count",
                cursor,
                value)) return false;
        output.rngStream = kernel::StableRngStreamId(stream);
        if (create) output.rngSeed = value;
        else output.rngCount = value;
        return true;
    }
    return false;
}

bool ParseAlgorithmProgram(
    const SyntaxNode& node,
    kernel::AlgorithmProgramDefinition& output,
    ParserCursor& cursor
)
{
    if (!node.block || !node.items.empty())
    {
        cursor.Diagnostics().Error(
            "dillen.authoring.algorithm_program_block_required",
            "algorithm program must be a property block",
            node.span
        );
        return false;
    }
    for (std::size_t stageIndex = 0;
        stageIndex < node.keys.size();
        ++stageIndex)
    {
        const auto entryPoint = ParseEntryPoint(
            node.keys[stageIndex],
            cursor
        );
        const SyntaxNode& stageNode = node.values[stageIndex];
        if (!entryPoint)
        {
            return false;
        }
        if (!stageNode.block || !stageNode.items.empty())
        {
            cursor.Diagnostics().Error(
                "dillen.authoring.algorithm_stage_block_required",
                "algorithm stage must be an instruction block",
                stageNode.span
            );
            return false;
        }
        std::vector<kernel::AlgorithmInstructionDefinition> instructions;
        for (std::size_t instructionIndex = 0;
            instructionIndex < stageNode.keys.size();
            ++instructionIndex)
        {
            const Token& instructionName =
                stageNode.keys[instructionIndex];
            const SyntaxNode& instructionNode =
                stageNode.values[instructionIndex];
            kernel::AlgorithmInstructionDefinition instruction;
            if (instructionName.text == "set_field")
            {
                if (!ParseAlgorithmFieldInstruction(
                        instructionNode,
                        kernel::AlgorithmInstructionKind::SetField,
                        instruction,
                        cursor))
                {
                    return false;
                }
            }
            else if (instructionName.text == "add_field")
            {
                if (!ParseAlgorithmFieldInstruction(
                        instructionNode,
                        kernel::AlgorithmInstructionKind::AddField,
                        instruction,
                        cursor))
                {
                    return false;
                }
            }
            else if (instructionName.text == "transition_lifecycle")
            {
                Token lifecycleToken;
                if (!RequireScalar(
                        &instructionNode,
                        cursor,
                        "transition_lifecycle",
                        lifecycleToken))
                {
                    return false;
                }
                const auto lifecycle = ParseLifecycleState(
                    lifecycleToken,
                    cursor
                );
                if (!lifecycle)
                {
                    return false;
                }
                instruction = kernel::AlgorithmInstructionDefinition::
                    TransitionLifecycle(*lifecycle);
            }
            else if (instructionName.text == "create_entity"
                || instructionName.text == "set_component_field"
                || instructionName.text == "add_relation"
                || instructionName.text == "spawn_mechanism"
                || instructionName.text == "schedule_event"
                || instructionName.text == "create_rng"
                || instructionName.text == "advance_rng")
            {
                if (!ParseGenericAlgorithmInstruction(
                        instructionName.text,
                        instructionNode,
                        instruction,
                        cursor))
                {
                    return false;
                }
            }
            else
            {
                cursor.Diagnostics().Error(
                    "dillen.authoring.algorithm_instruction_unknown",
                    "unknown declarative algorithm instruction: "
                        + std::string(instructionName.text),
                    instructionName.span
                );
                return false;
            }
            instructions.push_back(std::move(instruction));
        }
        if (!output.stages.emplace(
                *entryPoint,
                std::move(instructions)).second)
        {
            cursor.Diagnostics().Error(
                "dillen.authoring.algorithm_stage_duplicate",
                "algorithm program stage appears more than once",
                node.keys[stageIndex].span
            );
            return false;
        }
    }
    return true;
}

bool ParseControlledScriptInstruction(
    std::string_view name,
    const SyntaxNode& node,
    kernel::ControlledScriptInstructionDefinition& output,
    ParserCursor& cursor
)
{
    const auto scalarValue = [&node, &cursor](
        std::string_view property,
        kernel::MechanismValue& value)
    {
        const SyntaxNode* propertyNode = FindUnique(
            node,
            property,
            cursor,
            true
        );
        Token token;
        return propertyNode != nullptr
            && RequireScalar(propertyNode, cursor, property, token)
            && InferScalarValue(token, value, cursor);
    };
    if (name == "set_state" || name == "add_state")
    {
        if (!RejectUnknown(node, {"state", "value"}, cursor, name))
            return false;
        output.kind = name == "set_state"
            ? kernel::ControlledScriptInstructionKind::SetState
            : kernel::ControlledScriptInstructionKind::AddState;
        return ReadStringProperty(node, "state", cursor, output.state)
            && scalarValue("value", output.operand);
    }
    if (name == "set_field" || name == "add_field")
    {
        if (!RejectUnknown(node, {"field", "value"}, cursor, name))
            return false;
        output.kind = name == "set_field"
            ? kernel::ControlledScriptInstructionKind::SetField
            : kernel::ControlledScriptInstructionKind::AddField;
        return ReadStringProperty(node, "field", cursor, output.field)
            && scalarValue("value", output.operand);
    }
    if (name == "transition_lifecycle")
    {
        Token token;
        if (!RequireScalar(
                &node,
                cursor,
                "transition_lifecycle",
                token)) return false;
        const auto lifecycle = ParseLifecycleState(token, cursor);
        if (!lifecycle) return false;
        output.kind = kernel::ControlledScriptInstructionKind::
            TransitionLifecycle;
        output.lifecycle = *lifecycle;
        return true;
    }
    if (name == "jump")
    {
        Token token;
        std::uint64_t target = 0;
        if (!RequireScalar(&node, cursor, "jump", token)
            || !ParseUnsigned(
                token,
                std::numeric_limits<std::uint32_t>::max(),
                target,
                cursor)) return false;
        output.kind = kernel::ControlledScriptInstructionKind::Jump;
        output.targetInstruction = static_cast<std::uint32_t>(target);
        return true;
    }
    if (name == "jump_if_state_equals")
    {
        if (!RejectUnknown(
                node,
                {"state", "value", "target_instruction"},
                cursor,
                name)) return false;
        output.kind = kernel::ControlledScriptInstructionKind::
            JumpIfStateEquals;
        return ReadStringProperty(node, "state", cursor, output.state)
            && scalarValue("value", output.operand)
            && ReadUInt32Property(
                node,
                "target_instruction",
                cursor,
                output.targetInstruction);
    }
    if (name == "yield" || name == "halt")
    {
        Token token;
        bool enabled = false;
        if (!RequireScalar(&node, cursor, name, token)
            || !ParseBooleanToken(token, enabled, cursor)
            || !enabled)
        {
            cursor.Diagnostics().Error(
                "dillen.authoring.controlled_script_terminator_invalid",
                "yield and halt instructions must be enabled with yes",
                node.span
            );
            return false;
        }
        output.kind = name == "yield"
            ? kernel::ControlledScriptInstructionKind::Yield
            : kernel::ControlledScriptInstructionKind::Halt;
        return true;
    }
    return false;
}

bool ParseControlledScriptProgram(
    const SyntaxNode& node,
    kernel::ControlledScriptProgramDefinition& output,
    ParserCursor& cursor
)
{
    if (!node.block || !node.items.empty())
    {
        cursor.Diagnostics().Error(
            "dillen.authoring.controlled_script_block_required",
            "script must be a property block",
            node.span
        );
        return false;
    }
    bool stateSeen = false;
    for (std::size_t index = 0; index < node.keys.size(); ++index)
    {
        const Token& key = node.keys[index];
        const SyntaxNode& value = node.values[index];
        if (key.text == "state")
        {
            if (stateSeen)
            {
                cursor.Diagnostics().Error(
                    "dillen.authoring.controlled_script_state_duplicate",
                    "script state appears more than once",
                    key.span
                );
                return false;
            }
            stateSeen = true;
            std::map<std::string, kernel::MechanismValue> state;
            if (!ParseScalarFieldMap(value, state, cursor)) return false;
            for (auto& entry : state)
            {
                output.state.push_back({
                    std::move(entry.first),
                    std::move(entry.second)
                });
            }
            continue;
        }
        const auto entryPoint = ParseEntryPoint(key, cursor);
        if (!entryPoint) return false;
        if (!value.block || !value.items.empty())
        {
            cursor.Diagnostics().Error(
                "dillen.authoring.controlled_script_stage_block_required",
                "script stage must be an instruction block",
                value.span
            );
            return false;
        }
        std::vector<kernel::ControlledScriptInstructionDefinition>
            instructions;
        for (std::size_t instructionIndex = 0;
            instructionIndex < value.keys.size();
            ++instructionIndex)
        {
            kernel::ControlledScriptInstructionDefinition instruction;
            if (!ParseControlledScriptInstruction(
                    value.keys[instructionIndex].text,
                    value.values[instructionIndex],
                    instruction,
                    cursor))
            {
                cursor.Diagnostics().Error(
                    "dillen.authoring.controlled_script_instruction_unknown",
                    "unknown or invalid Controlled Script instruction: "
                        + std::string(value.keys[instructionIndex].text),
                    value.keys[instructionIndex].span
                );
                return false;
            }
            instructions.push_back(std::move(instruction));
        }
        if (!output.stages.emplace(
                *entryPoint,
                std::move(instructions)).second)
        {
            cursor.Diagnostics().Error(
                "dillen.authoring.controlled_script_stage_duplicate",
                "script stage appears more than once",
                key.span
            );
            return false;
        }
    }
    return true;
}

std::optional<kernel::RulesetContractKind> ParseContractKind(
    const Token& token,
    ParserCursor& cursor
)
{
    if (token.text == "package") return kernel::RulesetContractKind::Package;
    if (token.text == "mechanism_schema")
        return kernel::RulesetContractKind::MechanismSchema;
    if (token.text == "component_schema")
        return kernel::RulesetContractKind::ComponentSchema;
    if (token.text == "relation_schema")
        return kernel::RulesetContractKind::RelationSchema;
    if (token.text == "mechanism_definition")
        return kernel::RulesetContractKind::MechanismDefinition;
    if (token.text == "entity_definition")
        return kernel::RulesetContractKind::EntityDefinition;
    if (token.text == "relation_definition")
        return kernel::RulesetContractKind::RelationDefinition;
    if (token.text == "mechanism_spawn")
        return kernel::RulesetContractKind::MechanismSpawn;
    if (token.text == "algorithm")
        return kernel::RulesetContractKind::Algorithm;
    if (token.text == "capability")
        return kernel::RulesetContractKind::Capability;
    cursor.Diagnostics().Error(
        "dillen.authoring.ruleset_contract_kind_unknown",
        "unknown Ruleset contract kind: " + std::string(token.text),
        token.span
    );
    return std::nullopt;
}

bool ParseVersionedNameMap(
    const SyntaxNode& node,
    ParserCursor& cursor,
    const std::function<void(std::string, std::uint32_t)>& append
)
{
    if (!node.block || !node.items.empty())
    {
        cursor.Diagnostics().Error(
            "dillen.authoring.versioned_map_required",
            "requirement list must be a name-to-version block",
            node.span
        );
        return false;
    }
    std::set<std::string> names;
    for (std::size_t index = 0; index < node.keys.size(); ++index)
    {
        Token token;
        std::uint64_t version = 0;
        if (!RequireScalar(
                &node.values[index],
                cursor,
                node.keys[index].text,
                token)
            || !ParseUnsigned(
                token,
                std::numeric_limits<std::uint32_t>::max(),
                version,
                cursor)
            || version == 0)
        {
            return false;
        }
        std::string name(node.keys[index].text);
        if (!names.emplace(name).second)
        {
            cursor.Diagnostics().Error(
                "dillen.authoring.duplicate_requirement",
                "requirement appears more than once: " + name,
                node.keys[index].span
            );
            return false;
        }
        append(std::move(name), static_cast<std::uint32_t>(version));
    }
    return true;
}

bool ParseDefinitionRequirements(
    const SyntaxNode& node,
    std::vector<kernel::MechanismDefinitionId>& output,
    ParserCursor& cursor
)
{
    if (!node.block || !node.items.empty())
    {
        return false;
    }
    for (std::size_t index : FindAll(node, "requirement"))
    {
        const SyntaxNode& requirement = node.values[index];
        if (!requirement.block
            || !RejectUnknown(
                requirement,
                {"mechanism", "name"},
                cursor,
                "definition requirement"))
        {
            return false;
        }
        std::string mechanism;
        std::string name;
        if (!ReadStringProperty(
                requirement,
                "mechanism",
                cursor,
                mechanism)
            || !ReadStringProperty(
                requirement,
                "name",
                cursor,
                name))
        {
            return false;
        }
        output.push_back(kernel::StableMechanismDefinitionId(
            kernel::StableMechanismTypeId(mechanism),
            name
        ));
    }
    if (FindAll(node, "requirement").size() != node.keys.size())
    {
        cursor.Diagnostics().Error(
            "dillen.authoring.definition_requirement_expected",
            "definition requirement block only accepts 'requirement' entries",
            node.span
        );
        return false;
    }
    return true;
}

bool ParseSpawnRequirements(
    const SyntaxNode& node,
    std::vector<kernel::MechanismSpawnDefinitionId>& output,
    ParserCursor& cursor
)
{
    if (!node.block || !node.items.empty())
    {
        return false;
    }
    for (std::size_t index : FindAll(node, "requirement"))
    {
        const SyntaxNode& requirement = node.values[index];
        if (!requirement.block
            || !RejectUnknown(
                requirement,
                {"mechanism", "definition", "name"},
                cursor,
                "spawn requirement"))
        {
            return false;
        }
        std::string mechanism;
        std::string definitionName;
        std::string name;
        if (!ReadStringProperty(
                requirement,
                "mechanism",
                cursor,
                mechanism)
            || !ReadStringProperty(
                requirement,
                "definition",
                cursor,
                definitionName)
            || !ReadStringProperty(
                requirement,
                "name",
                cursor,
                name))
        {
            return false;
        }
        const kernel::MechanismDefinitionId definition =
            kernel::StableMechanismDefinitionId(
                kernel::StableMechanismTypeId(mechanism),
                definitionName
            );
        output.push_back(kernel::StableMechanismSpawnDefinitionId(
            definition,
            name
        ));
    }
    if (FindAll(node, "requirement").size() != node.keys.size())
    {
        cursor.Diagnostics().Error(
            "dillen.authoring.spawn_requirement_expected",
            "spawn requirement block only accepts 'requirement' entries",
            node.span
        );
        return false;
    }
    return true;
}

bool ParseCapabilityRequirements(
    const SyntaxNode& node,
    std::vector<kernel::CapabilityRequirement>& output,
    ParserCursor& cursor
)
{
    if (!node.block || !node.items.empty())
    {
        return false;
    }
    for (std::size_t index : FindAll(node, "requirement"))
    {
        const SyntaxNode& requirement = node.values[index];
        if (!requirement.block
            || !RejectUnknown(
                requirement,
                {"name", "minimum_version", "maximum_version"},
                cursor,
                "capability requirement"))
        {
            return false;
        }
        kernel::CapabilityRequirement value;
        if (!ReadStringProperty(
                requirement,
                "name",
                cursor,
                value.canonicalName)
            || !ReadUInt32Property(
                requirement,
                "minimum_version",
                cursor,
                value.versions.minimumInclusive))
        {
            return false;
        }
        value.capability = kernel::StableCapabilityId(value.canonicalName);
        if (FindUnique(
                requirement,
                "maximum_version",
                cursor,
                false) != nullptr)
        {
            std::uint32_t maximum = 0;
            if (!ReadUInt32Property(
                    requirement,
                    "maximum_version",
                    cursor,
                    maximum))
            {
                return false;
            }
            value.versions.maximumExclusive = maximum;
        }
        output.push_back(std::move(value));
    }
    if (FindAll(node, "requirement").size() != node.keys.size())
    {
        cursor.Diagnostics().Error(
            "dillen.authoring.capability_requirement_expected",
            "capability block only accepts 'requirement' entries",
            node.span
        );
        return false;
    }
    return true;
}

bool ParseRulesetRequirements(
    const SyntaxNode& body,
    kernel::RulesetRequirementSet& output,
    ParserCursor& cursor
)
{
    const SyntaxNode* packages = FindUnique(
        body,
        "required_packages",
        cursor,
        false
    );
    if (packages != nullptr)
    {
        if (!packages->block || !packages->items.empty())
        {
            return false;
        }
        for (std::size_t index : FindAll(*packages, "requirement"))
        {
            const SyntaxNode& node = packages->values[index];
            if (!node.block
                || !RejectUnknown(
                    node,
                    {
                        "name", "minimum_major", "minimum_minor",
                        "minimum_patch", "maximum_major",
                        "maximum_minor", "maximum_patch"
                    },
                    cursor,
                    "package requirement"))
            {
                return false;
            }
            kernel::RulesetPackageRequirement requirement;
            kernel::PackageVersion minimum;
            if (!ReadStringProperty(
                    node,
                    "name",
                    cursor,
                    requirement.canonicalName)
                || !ReadUInt32Property(
                    node,
                    "minimum_major",
                    cursor,
                    minimum.major)
                || !ReadUInt32Property(
                    node,
                    "minimum_minor",
                    cursor,
                    minimum.minor)
                || !ReadUInt32Property(
                    node,
                    "minimum_patch",
                    cursor,
                    minimum.patch))
            {
                return false;
            }
            requirement.package = kernel::StablePackageId(
                requirement.canonicalName
            );
            requirement.versions.minimumInclusive = minimum;
            if (FindUnique(
                    node,
                    "maximum_major",
                    cursor,
                    false) != nullptr)
            {
                kernel::PackageVersion maximum;
                if (!ReadUInt32Property(
                        node,
                        "maximum_major",
                        cursor,
                        maximum.major)
                    || !ReadUInt32Property(
                        node,
                        "maximum_minor",
                        cursor,
                        maximum.minor)
                    || !ReadUInt32Property(
                        node,
                        "maximum_patch",
                        cursor,
                        maximum.patch))
                {
                    return false;
                }
                requirement.versions.maximumExclusive = maximum;
            }
            output.packages.push_back(std::move(requirement));
        }
        if (FindAll(*packages, "requirement").size()
            != packages->keys.size())
        {
            return false;
        }
    }
    const SyntaxNode* schemas = FindUnique(
        body,
        "required_schemas",
        cursor,
        false
    );
    if (schemas != nullptr
        && !ParseVersionedNameMap(
            *schemas,
            cursor,
            [&output](std::string name, std::uint32_t version)
            {
                output.requiredSchemas.push_back({
                    kernel::StableMechanismTypeId(name),
                    version
                });
            }))
    {
        return false;
    }
    const SyntaxNode* components = FindUnique(
        body,
        "required_components",
        cursor,
        false
    );
    if (components != nullptr
        && !ParseVersionedNameMap(
            *components,
            cursor,
            [&output](std::string name, std::uint32_t version)
            {
                output.requiredComponents.push_back({
                    kernel::StableComponentTypeId(name),
                    version
                });
            }))
    {
        return false;
    }
    const SyntaxNode* relations = FindUnique(
        body,
        "required_relations",
        cursor,
        false
    );
    if (relations != nullptr
        && !ParseVersionedNameMap(
            *relations,
            cursor,
            [&output](std::string name, std::uint32_t version)
            {
                output.requiredRelations.push_back({
                    kernel::StableRelationTypeId(name),
                    version
                });
            }))
    {
        return false;
    }
    const SyntaxNode* algorithms = FindUnique(
        body,
        "required_algorithms",
        cursor,
        false
    );
    if (algorithms != nullptr
        && !ParseVersionedNameMap(
            *algorithms,
            cursor,
            [&output](std::string name, std::uint32_t version)
            {
                output.requiredAlgorithms.push_back({
                    kernel::StableAlgorithmId(name),
                    version
                });
            }))
    {
        return false;
    }
    const SyntaxNode* definitions = FindUnique(
        body,
        "required_definitions",
        cursor,
        false
    );
    if (definitions != nullptr
        && !ParseDefinitionRequirements(
            *definitions,
            output.requiredDefinitions,
            cursor))
    {
        return false;
    }
    const SyntaxNode* entityDefinitions = FindUnique(
        body,
        "required_entity_definitions",
        cursor,
        false
    );
    if (entityDefinitions != nullptr)
    {
        if (!entityDefinitions->block || !entityDefinitions->items.empty())
        {
            return false;
        }
        for (std::size_t index
            : FindAll(*entityDefinitions, "requirement"))
        {
            const SyntaxNode& node = entityDefinitions->values[index];
            std::string type;
            std::string name;
            if (!node.block
                || !RejectUnknown(
                    node,
                    {"entity_type", "name"},
                    cursor,
                    "entity definition requirement")
                || !ReadStringProperty(
                    node,
                    "entity_type",
                    cursor,
                    type)
                || !ReadStringProperty(node, "name", cursor, name))
            {
                return false;
            }
            output.requiredEntityDefinitions.push_back(
                kernel::StableEntityDefinitionId(
                    kernel::StableEntityTypeId(type),
                    name
                )
            );
        }
        if (FindAll(*entityDefinitions, "requirement").size()
            != entityDefinitions->keys.size())
        {
            return false;
        }
    }
    const SyntaxNode* relationDefinitions = FindUnique(
        body,
        "required_relation_definitions",
        cursor,
        false
    );
    if (relationDefinitions != nullptr)
    {
        if (!relationDefinitions->block
            || !relationDefinitions->items.empty())
        {
            return false;
        }
        for (std::size_t index
            : FindAll(*relationDefinitions, "requirement"))
        {
            const SyntaxNode& node = relationDefinitions->values[index];
            std::string type;
            std::string name;
            if (!node.block
                || !RejectUnknown(
                    node,
                    {"relation", "name"},
                    cursor,
                    "relation definition requirement")
                || !ReadStringProperty(node, "relation", cursor, type)
                || !ReadStringProperty(node, "name", cursor, name))
            {
                return false;
            }
            output.requiredRelationDefinitions.push_back(
                kernel::StableRelationDefinitionId(
                    kernel::StableRelationTypeId(type),
                    name
                )
            );
        }
        if (FindAll(*relationDefinitions, "requirement").size()
            != relationDefinitions->keys.size())
        {
            return false;
        }
    }
    const SyntaxNode* spawns = FindUnique(
        body,
        "required_spawns",
        cursor,
        false
    );
    if (spawns != nullptr
        && !ParseSpawnRequirements(
            *spawns,
            output.requiredMechanismSpawns,
            cursor))
    {
        return false;
    }
    const SyntaxNode* capabilities = FindUnique(
        body,
        "required_capabilities",
        cursor,
        false
    );
    return capabilities == nullptr
        || ParseCapabilityRequirements(
            *capabilities,
            output.requiredCapabilities,
            cursor
        );
}

}

bool ParseComponentSchema(
    ParserCursor& cursor,
    parser::ParseArtifact& artifact
)
{
    ParsedRoot root;
    if (!ParseRoot(cursor, root)
        || root.keyword.text != "component_schema"
        || !RejectUnknown(
            root.body,
            {"name", "version", "fields"},
            cursor,
            "component schema"))
    {
        return false;
    }
    ComponentSchemaDocument document;
    document.declarationSpan = root.body.span;
    if (!ReadStringProperty(
            root.body,
            "name",
            cursor,
            document.value.canonicalName)
        || !ReadUInt32Property(
            root.body,
            "version",
            cursor,
            document.value.version))
    {
        return false;
    }
    document.value.type = kernel::StableComponentTypeId(
        document.value.canonicalName
    );
    const SyntaxNode* fields = FindUnique(
        root.body,
        "fields",
        cursor,
        false
    );
    if (fields != nullptr)
    {
        if (!fields->block || !fields->items.empty())
        {
            return false;
        }
        for (std::size_t index : FindAll(*fields, "field"))
        {
            kernel::MechanismFieldSchema field;
            if (!ParseFieldSchema(fields->values[index], field, cursor))
            {
                return false;
            }
            document.value.fields.push_back(std::move(field));
        }
        if (FindAll(*fields, "field").size() != fields->keys.size())
        {
            return false;
        }
    }
    artifact.value = std::move(document);
    return true;
}

bool ParseEntityDefinition(
    ParserCursor& cursor,
    parser::ParseArtifact& artifact
)
{
    ParsedRoot root;
    if (!ParseRoot(cursor, root)
        || root.keyword.text != "entity_definition"
        || !RejectUnknown(
            root.body,
            {"name", "entity_type", "components"},
            cursor,
            "entity definition"))
    {
        return false;
    }
    EntityDefinitionDocument document;
    document.declarationSpan = root.body.span;
    std::string entityType;
    if (!ReadStringProperty(
            root.body,
            "name",
            cursor,
            document.value.canonicalName)
        || !ReadStringProperty(
            root.body,
            "entity_type",
            cursor,
            entityType))
    {
        return false;
    }
    document.value.type = kernel::StableEntityTypeId(entityType);
    document.value.id = kernel::StableEntityDefinitionId(
        document.value.type,
        document.value.canonicalName
    );
    const SyntaxNode* components = FindUnique(
        root.body,
        "components",
        cursor,
        false
    );
    if (components != nullptr)
    {
        if (!components->block || !components->items.empty())
        {
            return false;
        }
        for (std::size_t index : FindAll(*components, "component"))
        {
            const SyntaxNode& node = components->values[index];
            if (!node.block
                || !RejectUnknown(
                    node,
                    {"type", "schema_version", "fields"},
                    cursor,
                    "entity component"))
            {
                return false;
            }
            kernel::EntityComponentDefinition component;
            std::string type;
            if (!ReadStringProperty(node, "type", cursor, type)
                || !ReadUInt32Property(
                    node,
                    "schema_version",
                    cursor,
                    component.schemaVersion))
            {
                return false;
            }
            component.type = kernel::StableComponentTypeId(type);
            const SyntaxNode* fields = FindUnique(
                node,
                "fields",
                cursor,
                false
            );
            if (fields != nullptr
                && !ParseScalarFieldMap(*fields, component.fields, cursor))
            {
                return false;
            }
            document.value.components.push_back(std::move(component));
        }
        if (FindAll(*components, "component").size()
            != components->keys.size())
        {
            return false;
        }
    }
    artifact.value = std::move(document);
    return true;
}

bool ParseRelationSchema(
    ParserCursor& cursor,
    parser::ParseArtifact& artifact
)
{
    ParsedRoot root;
    if (!ParseRoot(cursor, root)
        || root.keyword.text != "relation_schema"
        || !RejectUnknown(
            root.body,
            {
                "name", "version", "source_type", "target_type",
                "allow_self"
            },
            cursor,
            "relation schema"))
    {
        return false;
    }
    RelationSchemaDocument document;
    document.declarationSpan = root.body.span;
    if (!ReadStringProperty(
            root.body,
            "name",
            cursor,
            document.value.canonicalName)
        || !ReadUInt32Property(
            root.body,
            "version",
            cursor,
            document.value.version))
    {
        return false;
    }
    document.value.type = kernel::StableRelationTypeId(
        document.value.canonicalName
    );
    std::string type;
    if (FindUnique(root.body, "source_type", cursor, false) != nullptr)
    {
        if (!ReadStringProperty(root.body, "source_type", cursor, type))
        {
            return false;
        }
        document.value.sourceType = kernel::StableEntityTypeId(type);
    }
    if (FindUnique(root.body, "target_type", cursor, false) != nullptr)
    {
        if (!ReadStringProperty(root.body, "target_type", cursor, type))
        {
            return false;
        }
        document.value.targetType = kernel::StableEntityTypeId(type);
    }
    if (FindUnique(root.body, "allow_self", cursor, false) != nullptr
        && !ReadBoolProperty(
            root.body,
            "allow_self",
            cursor,
            document.value.allowSelf))
    {
        return false;
    }
    artifact.value = std::move(document);
    return true;
}

bool ParseRelationDefinition(
    ParserCursor& cursor,
    parser::ParseArtifact& artifact
)
{
    ParsedRoot root;
    if (!ParseRoot(cursor, root)
        || root.keyword.text != "relation_definition"
        || !RejectUnknown(
            root.body,
            {
                "name", "relation", "schema_version",
                "source_entity_type", "source_definition",
                "target_entity_type", "target_definition"
            },
            cursor,
            "relation definition"))
    {
        return false;
    }
    RelationDefinitionDocument document;
    document.declarationSpan = root.body.span;
    std::string relation;
    std::string sourceType;
    std::string sourceDefinition;
    std::string targetType;
    std::string targetDefinition;
    if (!ReadStringProperty(
            root.body,
            "name",
            cursor,
            document.value.canonicalName)
        || !ReadStringProperty(root.body, "relation", cursor, relation)
        || !ReadUInt32Property(
            root.body,
            "schema_version",
            cursor,
            document.value.schemaVersion)
        || !ReadStringProperty(
            root.body,
            "source_entity_type",
            cursor,
            sourceType)
        || !ReadStringProperty(
            root.body,
            "source_definition",
            cursor,
            sourceDefinition)
        || !ReadStringProperty(
            root.body,
            "target_entity_type",
            cursor,
            targetType)
        || !ReadStringProperty(
            root.body,
            "target_definition",
            cursor,
            targetDefinition))
    {
        return false;
    }
    document.value.type = kernel::StableRelationTypeId(relation);
    document.value.id = kernel::StableRelationDefinitionId(
        document.value.type,
        document.value.canonicalName
    );
    document.value.source = kernel::StableEntityDefinitionId(
        kernel::StableEntityTypeId(sourceType),
        sourceDefinition
    );
    document.value.target = kernel::StableEntityDefinitionId(
        kernel::StableEntityTypeId(targetType),
        targetDefinition
    );
    artifact.value = std::move(document);
    return true;
}

bool ParsePackageManifest(
    ParserCursor& cursor,
    parser::ParseArtifact& artifact
)
{
    ParsedRoot root;
    if (!ParseRoot(cursor, root)
        || root.keyword.text != "package_manifest"
        || !RejectUnknown(
            root.body,
            {
                "name", "version_major", "version_minor",
                "version_patch", "content_digest", "load_priority",
                "dependencies", "provides"
            },
            cursor,
            "package manifest"))
    {
        return false;
    }
    PackageManifestDocument document;
    document.declarationSpan = root.body.span;
    if (!ReadStringProperty(
            root.body,
            "name",
            cursor,
            document.value.canonicalName)
        || !ReadUInt32Property(
            root.body,
            "version_major",
            cursor,
            document.value.version.major)
        || !ReadUInt32Property(
            root.body,
            "version_minor",
            cursor,
            document.value.version.minor)
        || !ReadUInt32Property(
            root.body,
            "version_patch",
            cursor,
            document.value.version.patch)
        || !ReadStringProperty(
            root.body,
            "content_digest",
            cursor,
            document.value.contentDigest))
    {
        return false;
    }
    document.value.id = kernel::StablePackageId(
        document.value.canonicalName
    );
    if (FindUnique(root.body, "load_priority", cursor, false) != nullptr
        && !ReadInt32Property(
            root.body,
            "load_priority",
            cursor,
            document.value.loadPriority))
    {
        return false;
    }
    const SyntaxNode* dependencies = FindUnique(
        root.body,
        "dependencies",
        cursor,
        false
    );
    if (dependencies != nullptr)
    {
        if (!dependencies->block || !dependencies->items.empty())
        {
            return false;
        }
        for (std::size_t index
            : FindAll(*dependencies, "dependency"))
        {
            const SyntaxNode& node = dependencies->values[index];
            if (!node.block
                || !RejectUnknown(
                    node,
                    {
                        "name", "minimum_major", "minimum_minor",
                        "minimum_patch", "maximum_major",
                        "maximum_minor", "maximum_patch", "required"
                    },
                    cursor,
                    "package dependency"))
            {
                return false;
            }
            kernel::PackageDependency dependency;
            kernel::PackageVersion minimum;
            if (!ReadStringProperty(
                    node,
                    "name",
                    cursor,
                    dependency.canonicalName)
                || !ReadUInt32Property(
                    node,
                    "minimum_major",
                    cursor,
                    minimum.major)
                || !ReadUInt32Property(
                    node,
                    "minimum_minor",
                    cursor,
                    minimum.minor)
                || !ReadUInt32Property(
                    node,
                    "minimum_patch",
                    cursor,
                    minimum.patch))
            {
                return false;
            }
            dependency.package = kernel::StablePackageId(
                dependency.canonicalName
            );
            dependency.versions.minimumInclusive = minimum;
            if (FindUnique(node, "required", cursor, false) != nullptr
                && !ReadBoolProperty(
                    node,
                    "required",
                    cursor,
                    dependency.required))
            {
                return false;
            }
            if (FindUnique(
                    node,
                    "maximum_major",
                    cursor,
                    false) != nullptr)
            {
                kernel::PackageVersion maximum;
                if (!ReadUInt32Property(
                        node,
                        "maximum_major",
                        cursor,
                        maximum.major)
                    || !ReadUInt32Property(
                        node,
                        "maximum_minor",
                        cursor,
                        maximum.minor)
                    || !ReadUInt32Property(
                        node,
                        "maximum_patch",
                        cursor,
                        maximum.patch))
                {
                    return false;
                }
                dependency.versions.maximumExclusive = maximum;
            }
            document.value.dependencies.push_back(std::move(dependency));
        }
        if (FindAll(*dependencies, "dependency").size()
            != dependencies->keys.size())
        {
            return false;
        }
    }
    const SyntaxNode* provides = FindUnique(
        root.body,
        "provides",
        cursor,
        false
    );
    if (provides != nullptr)
    {
        if (!provides->block || !provides->items.empty())
        {
            return false;
        }
        for (std::size_t index : FindAll(*provides, "capability"))
        {
            const SyntaxNode& node = provides->values[index];
            if (!node.block
                || !RejectUnknown(
                    node,
                    {"name", "version"},
                    cursor,
                    "provided capability"))
            {
                return false;
            }
            kernel::CapabilityProvision provision;
            if (!ReadStringProperty(
                    node,
                    "name",
                    cursor,
                    provision.canonicalName)
                || !ReadUInt32Property(
                    node,
                    "version",
                    cursor,
                    provision.version))
            {
                return false;
            }
            provision.capability = kernel::StableCapabilityId(
                provision.canonicalName
            );
            document.value.providedCapabilities.push_back(
                std::move(provision)
            );
        }
        if (FindAll(*provides, "capability").size()
            != provides->keys.size())
        {
            return false;
        }
    }
    artifact.value = std::move(document);
    return true;
}

bool ParseCapabilityContract(
    ParserCursor& cursor,
    parser::ParseArtifact& artifact
)
{
    ParsedRoot root;
    if (!ParseRoot(cursor, root)
        || root.keyword.text != "capability_contract"
        || !RejectUnknown(
            root.body,
            {"name", "version", "deterministic", "operations"},
            cursor,
            "capability contract"))
    {
        return false;
    }
    CapabilityContractDocument document;
    document.declarationSpan = root.body.span;
    if (!ReadStringProperty(
            root.body,
            "name",
            cursor,
            document.value.canonicalName)
        || !ReadUInt32Property(
            root.body,
            "version",
            cursor,
            document.value.version))
    {
        return false;
    }
    document.value.id = kernel::StableCapabilityId(
        document.value.canonicalName
    );
    if (FindUnique(root.body, "deterministic", cursor, false) != nullptr
        && !ReadBoolProperty(
            root.body,
            "deterministic",
            cursor,
            document.value.deterministic))
    {
        return false;
    }
    const SyntaxNode* operations = FindUnique(
        root.body,
        "operations",
        cursor,
        false
    );
    if (operations != nullptr)
    {
        if (!operations->block || !operations->keys.empty())
        {
            return false;
        }
        for (const Token& item : operations->items)
        {
            document.value.operations.emplace_back(item.text);
        }
    }
    artifact.value = std::move(document);
    return true;
}

bool ParseMechanismTemplate(
    ParserCursor& cursor,
    parser::ParseArtifact& artifact
)
{
    ParsedRoot root;
    if (!ParseRoot(cursor, root)
        || root.keyword.text != "mechanism_template")
    {
        cursor.Diagnostics().Error(
            "dillen.authoring.mechanism_root_expected",
            "mechanism source must begin with mechanism_template",
            root.keyword.span
        );
        return false;
    }
    if (!RejectUnknown(
            root.body,
            {"name", "version", "fields", "roles"},
            cursor,
            "mechanism template"))
    {
        return false;
    }
    MechanismTemplateDocument document;
    document.declarationSpan = root.body.span;
    if (!ReadStringProperty(
            root.body,
            "name",
            cursor,
            document.value.canonicalName)
        || !ReadUInt32Property(
            root.body,
            "version",
            cursor,
            document.value.version))
    {
        return false;
    }
    document.value.type = kernel::StableMechanismTypeId(
        document.value.canonicalName
    );
    const SyntaxNode* fields = FindUnique(
        root.body,
        "fields",
        cursor,
        false
    );
    if (fields != nullptr)
    {
        if (!fields->block || !fields->items.empty())
        {
            return false;
        }
        for (std::size_t index : FindAll(*fields, "field"))
        {
            kernel::MechanismFieldSchema field;
            if (!ParseFieldSchema(fields->values[index], field, cursor))
            {
                return false;
            }
            document.value.fields.push_back(std::move(field));
        }
        if (FindAll(*fields, "field").size() != fields->keys.size())
        {
            cursor.Diagnostics().Error(
                "dillen.authoring.field_entry_expected",
                "fields block only accepts 'field' entries",
                fields->span
            );
            return false;
        }
    }
    const SyntaxNode* roles = FindUnique(
        root.body,
        "roles",
        cursor,
        false
    );
    if (roles != nullptr)
    {
        if (!roles->block || !roles->items.empty())
        {
            return false;
        }
        for (std::size_t index : FindAll(*roles, "role"))
        {
            kernel::MechanismRoleSchema role;
            if (!ParseRoleSchema(roles->values[index], role, cursor))
            {
                return false;
            }
            document.value.roles.push_back(std::move(role));
        }
        if (FindAll(*roles, "role").size() != roles->keys.size())
        {
            cursor.Diagnostics().Error(
                "dillen.authoring.role_entry_expected",
                "roles block only accepts 'role' entries",
                roles->span
            );
            return false;
        }
    }
    artifact.value = std::move(document);
    return true;
}

bool ParseAlgorithmDescriptor(
    ParserCursor& cursor,
    parser::ParseArtifact& artifact
)
{
    ParsedRoot root;
    if (!ParseRoot(cursor, root)
        || root.keyword.text != "algorithm_descriptor")
    {
        cursor.Diagnostics().Error(
            "dillen.authoring.algorithm_root_expected",
            "algorithm source must begin with algorithm_descriptor",
            root.keyword.span
        );
        return false;
    }
    if (!RejectUnknown(
            root.body,
            {
                "name", "version", "backend", "entry_points",
                "deterministic", "execution_policy",
                "required_capabilities", "program", "script"
            },
            cursor,
            "algorithm descriptor"))
    {
        return false;
    }
    AlgorithmDescriptorDocument document;
    document.declarationSpan = root.body.span;
    std::string backend;
    if (!ReadStringProperty(
            root.body,
            "name",
            cursor,
            document.value.canonicalName)
        || !ReadUInt32Property(
            root.body,
            "version",
            cursor,
            document.value.version)
        || !ReadStringProperty(
            root.body,
            "backend",
            cursor,
            backend))
    {
        return false;
    }
    document.value.id = kernel::StableAlgorithmId(
        document.value.canonicalName
    );
    Token backendToken;
    backendToken.text = backend;
    backendToken.span = root.body.span;
    const auto parsedBackend = ParseBackend(backendToken, cursor);
    if (!parsedBackend)
    {
        return false;
    }
    document.value.backend = *parsedBackend;
    if (FindUnique(
            root.body,
            "deterministic",
            cursor,
            false) != nullptr
        && !ReadBoolProperty(
            root.body,
            "deterministic",
            cursor,
            document.value.deterministic))
    {
        return false;
    }
    const SyntaxNode* executionPolicy = FindUnique(
        root.body,
        "execution_policy",
        cursor,
        false
    );
    if (executionPolicy != nullptr
        && !ParseAlgorithmExecutionPolicy(
            *executionPolicy,
            document.value.executionPolicy,
            cursor))
    {
        return false;
    }
    const SyntaxNode* entries = FindUnique(
        root.body,
        "entry_points",
        cursor,
        true
    );
    if (entries == nullptr || !entries->block || !entries->keys.empty())
    {
        cursor.Diagnostics().Error(
            "dillen.authoring.entry_point_list_required",
            "entry_points must be a list block",
            entries == nullptr ? root.body.span : entries->span
        );
        return false;
    }
    for (const Token& item : entries->items)
    {
        const auto entry = ParseEntryPoint(item, cursor);
        if (!entry)
        {
            return false;
        }
        document.value.entryPoints = document.value.entryPoints | *entry;
    }
    const SyntaxNode* capabilities = FindUnique(
        root.body,
        "required_capabilities",
        cursor,
        false
    );
    if (capabilities != nullptr
        && !ParseCapabilityRequirements(
            *capabilities,
            document.value.requiredCapabilities,
            cursor))
    {
        return false;
    }
    const bool declarative = document.value.backend
        == kernel::AlgorithmBackend::Declarative;
    const bool controlledScript = document.value.backend
        == kernel::AlgorithmBackend::Script;
    const SyntaxNode* program = FindUnique(
        root.body,
        "program",
        cursor,
        declarative
    );
    if (program != nullptr
        && (!declarative
            || !ParseAlgorithmProgram(
                *program,
                document.value.program,
                cursor)))
    {
        if (!declarative && !cursor.Diagnostics().HasErrors())
        {
            cursor.Diagnostics().Error(
                "dillen.authoring.algorithm_program_backend_invalid",
                "only declarative algorithms may define a program",
                program->span
            );
        }
        return false;
    }
    const SyntaxNode* script = FindUnique(
        root.body,
        "script",
        cursor,
        controlledScript
    );
    if (script != nullptr
        && (!controlledScript
            || !ParseControlledScriptProgram(
                *script,
                document.value.script,
                cursor)))
    {
        if (!controlledScript && !cursor.Diagnostics().HasErrors())
        {
            cursor.Diagnostics().Error(
                "dillen.authoring.controlled_script_backend_invalid",
                "only script algorithms may define a script program",
                script->span
            );
        }
        return false;
    }
    if (declarative
        && !kernel::IsValidAlgorithmProgram(
            document.value.program,
            document.value.entryPoints))
    {
        cursor.Diagnostics().Error(
            "dillen.authoring.algorithm_program_entry_mismatch",
            "declarative program stages must exactly match entry_points",
            program == nullptr ? root.body.span : program->span
        );
        return false;
    }
    if (controlledScript
        && !kernel::IsValidControlledScriptProgram(
            document.value.script,
            document.value.entryPoints))
    {
        cursor.Diagnostics().Error(
            "dillen.authoring.controlled_script_entry_mismatch",
            "script stages must exactly match entry_points",
            script == nullptr ? root.body.span : script->span
        );
        return false;
    }
    artifact.value = std::move(document);
    return true;
}

bool ParseMechanismDefinition(
    ParserCursor& cursor,
    parser::ParseArtifact& artifact
)
{
    ParsedRoot root;
    if (!ParseRoot(cursor, root)
        || root.keyword.text != "mechanism_definition")
    {
        cursor.Diagnostics().Error(
            "dillen.authoring.definition_root_expected",
            "definition source must begin with mechanism_definition",
            root.keyword.span
        );
        return false;
    }
    if (!RejectUnknown(
            root.body,
            {
                "name", "mechanism", "schema_version", "algorithm",
                "algorithm_version", "fields"
            },
            cursor,
            "mechanism definition"))
    {
        return false;
    }
    MechanismDefinitionDocument document;
    document.declarationSpan = root.body.span;
    std::string mechanism;
    std::string algorithm;
    if (!ReadStringProperty(
            root.body,
            "name",
            cursor,
            document.value.canonicalName)
        || !ReadStringProperty(
            root.body,
            "mechanism",
            cursor,
            mechanism)
        || !ReadUInt32Property(
            root.body,
            "schema_version",
            cursor,
            document.value.schemaVersion))
    {
        return false;
    }
    document.value.type = kernel::StableMechanismTypeId(mechanism);
    document.value.id = kernel::StableMechanismDefinitionId(
        document.value.type,
        document.value.canonicalName
    );
    const SyntaxNode* algorithmNode = FindUnique(
        root.body,
        "algorithm",
        cursor,
        false
    );
    if (algorithmNode != nullptr)
    {
        Token token;
        if (!RequireScalar(
                algorithmNode,
                cursor,
                "algorithm",
                token)
            || !ReadUInt32Property(
                root.body,
                "algorithm_version",
                cursor,
                document.value.algorithmVersion))
        {
            return false;
        }
        document.value.algorithm = kernel::StableAlgorithmId(token.text);
    }
    else if (FindUnique(
        root.body,
        "algorithm_version",
        cursor,
        false) != nullptr)
    {
        cursor.Diagnostics().Error(
            "dillen.authoring.algorithm_name_missing",
            "algorithm_version requires an algorithm name",
            root.body.span
        );
        return false;
    }
    const SyntaxNode* fields = FindUnique(
        root.body,
        "fields",
        cursor,
        false
    );
    if (fields != nullptr
        && !ParseScalarFieldMap(
            *fields,
            document.value.fields,
            cursor))
    {
        return false;
    }
    artifact.value = std::move(document);
    return true;
}

bool ParseMechanismSpawn(
    ParserCursor& cursor,
    parser::ParseArtifact& artifact
)
{
    ParsedRoot root;
    if (!ParseRoot(cursor, root)
        || root.keyword.text != "mechanism_spawn")
    {
        cursor.Diagnostics().Error(
            "dillen.authoring.spawn_root_expected",
            "spawn source must begin with mechanism_spawn",
            root.keyword.span
        );
        return false;
    }
    if (!RejectUnknown(
            root.body,
            {"name", "mechanism", "definition", "count", "fields"},
            cursor,
            "mechanism spawn"))
    {
        return false;
    }
    MechanismSpawnDocument document;
    document.declarationSpan = root.body.span;
    std::string mechanism;
    std::string definition;
    if (!ReadStringProperty(
            root.body,
            "name",
            cursor,
            document.value.canonicalName)
        || !ReadStringProperty(
            root.body,
            "mechanism",
            cursor,
            mechanism)
        || !ReadStringProperty(
            root.body,
            "definition",
            cursor,
            definition))
    {
        return false;
    }
    if (FindUnique(root.body, "count", cursor, false) != nullptr
        && !ReadUInt32Property(
            root.body,
            "count",
            cursor,
            document.value.count))
    {
        return false;
    }
    document.value.definition = kernel::StableMechanismDefinitionId(
        kernel::StableMechanismTypeId(mechanism),
        definition
    );
    document.value.id = kernel::StableMechanismSpawnDefinitionId(
        document.value.definition,
        document.value.canonicalName
    );
    const SyntaxNode* fields = FindUnique(
        root.body,
        "fields",
        cursor,
        false
    );
    if (fields != nullptr
        && !ParseScalarFieldMap(
            *fields,
            document.value.initialFields,
            cursor))
    {
        return false;
    }
    artifact.value = std::move(document);
    return true;
}

bool ParseRuleset(
    ParserCursor& cursor,
    parser::ParseArtifact& artifact
)
{
    ParsedRoot root;
    if (!ParseRoot(cursor, root))
    {
        return false;
    }
    const bool isRoot = root.keyword.text == "root_ruleset";
    const bool isExtension = root.keyword.text == "extension_ruleset";
    if (!isRoot && !isExtension)
    {
        cursor.Diagnostics().Error(
            "dillen.authoring.ruleset_root_expected",
            "ruleset source must begin with root_ruleset or extension_ruleset",
            root.keyword.span
        );
        return false;
    }
    if (!root.body.items.empty())
    {
        cursor.Diagnostics().Error(
            "dillen.authoring.unexpected_bare_value",
            "bare values are not allowed in a Ruleset document",
            root.body.items.front().span
        );
        return false;
    }
    const auto commonKeys = {
        std::string_view("name"),
        std::string_view("version"),
        std::string_view("required_packages"),
        std::string_view("required_schemas"),
        std::string_view("required_components"),
        std::string_view("required_relations"),
        std::string_view("required_definitions"),
        std::string_view("required_entity_definitions"),
        std::string_view("required_relation_definitions"),
        std::string_view("required_spawns"),
        std::string_view("required_algorithms"),
        std::string_view("required_capabilities")
    };
    for (const Token& key : root.body.keys)
    {
        const bool common = std::find(
            commonKeys.begin(),
            commonKeys.end(),
            key.text
        ) != commonKeys.end();
        const bool rootOnly = key.text == "allow_additions"
            || key.text == "protected_contracts";
        const bool extensionOnly = key.text == "priority"
            || key.text == "target_root"
            || key.text == "target_minimum_version"
            || key.text == "target_maximum_version";
        if (!common
            && !(isRoot && rootOnly)
            && !(isExtension && extensionOnly))
        {
            cursor.Diagnostics().Error(
                "dillen.authoring.unknown_property",
                "unknown ruleset property: " + std::string(key.text),
                key.span
            );
            return false;
        }
    }

    RulesetDocument document;
    document.declarationSpan = root.body.span;
    if (isRoot)
    {
        kernel::RootRulesetDefinition value;
        if (!ReadStringProperty(
                root.body,
                "name",
                cursor,
                value.ruleset.canonicalName)
            || !ReadUInt32Property(
                root.body,
                "version",
                cursor,
                value.ruleset.version)
            || !ParseRulesetRequirements(
                root.body,
                value.ruleset,
                cursor))
        {
            return false;
        }
        value.ruleset.id = kernel::StableRulesetId(
            value.ruleset.canonicalName
        );
        const SyntaxNode* additions = FindUnique(
            root.body,
            "allow_additions",
            cursor,
            false
        );
        if (additions != nullptr)
        {
            if (!additions->block || !additions->keys.empty())
            {
                return false;
            }
            for (const Token& item : additions->items)
            {
                const auto kind = ParseContractKind(item, cursor);
                if (!kind)
                {
                    return false;
                }
                value.extensionPolicy.allowedAdditions.push_back(*kind);
            }
        }
        const SyntaxNode* protectedContracts = FindUnique(
            root.body,
            "protected_contracts",
            cursor,
            false
        );
        if (protectedContracts != nullptr)
        {
            if (!protectedContracts->block
                || !protectedContracts->items.empty())
            {
                return false;
            }
            for (std::size_t index
                : FindAll(*protectedContracts, "contract"))
            {
                const SyntaxNode& contract =
                    protectedContracts->values[index];
                if (!contract.block
                    || !RejectUnknown(
                        contract,
                        {"kind", "id", "version"},
                        cursor,
                        "protected contract"))
                {
                    return false;
                }
                std::string kindName;
                if (!ReadStringProperty(
                        contract,
                        "kind",
                        cursor,
                        kindName))
                {
                    return false;
                }
                Token kindToken;
                kindToken.text = kindName;
                kindToken.span = contract.span;
                const auto kind = ParseContractKind(kindToken, cursor);
                const SyntaxNode* idNode = FindUnique(
                    contract,
                    "id",
                    cursor,
                    true
                );
                Token idToken;
                std::uint64_t id = 0;
                if (!kind
                    || !RequireScalar(idNode, cursor, "id", idToken)
                    || !ParseUnsigned(
                        idToken,
                        std::numeric_limits<std::uint64_t>::max(),
                        id,
                        cursor))
                {
                    return false;
                }
                kernel::RulesetContractKey key;
                key.kind = *kind;
                key.id = id;
                if (FindUnique(contract, "version", cursor, false)
                    != nullptr
                    && !ReadUInt32Property(
                        contract,
                        "version",
                        cursor,
                        key.version))
                {
                    return false;
                }
                value.extensionPolicy.protectedContracts.push_back(key);
            }
            if (FindAll(*protectedContracts, "contract").size()
                != protectedContracts->keys.size())
            {
                return false;
            }
        }
        document.value = std::move(value);
    }
    else
    {
        kernel::ExtensionRulesetDefinition value;
        if (!ReadStringProperty(
                root.body,
                "name",
                cursor,
                value.canonicalName)
            || !ReadUInt32Property(
                root.body,
                "version",
                cursor,
                value.version)
            || !ReadStringProperty(
                root.body,
                "target_root",
                cursor,
                value.targetRootCanonicalName)
            || !ReadUInt32Property(
                root.body,
                "target_minimum_version",
                cursor,
                value.targetVersions.minimumInclusive)
            || !ParseRulesetRequirements(root.body, value, cursor))
        {
            return false;
        }
        value.id = kernel::StableRulesetId(value.canonicalName);
        value.targetRoot = kernel::StableRulesetId(
            value.targetRootCanonicalName
        );
        if (FindUnique(root.body, "priority", cursor, false) != nullptr
            && !ReadInt32Property(
                root.body,
                "priority",
                cursor,
                value.priority))
        {
            return false;
        }
        if (FindUnique(
                root.body,
                "target_maximum_version",
                cursor,
                false) != nullptr)
        {
            std::uint32_t maximum = 0;
            if (!ReadUInt32Property(
                    root.body,
                    "target_maximum_version",
                    cursor,
                    maximum))
            {
                return false;
            }
            value.targetVersions.maximumExclusive = maximum;
        }
        document.value = std::move(value);
    }
    artifact.value = std::move(document);
    return true;
}

}
