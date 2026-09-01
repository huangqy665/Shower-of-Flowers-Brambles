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

// Defined below, next to the role schema it mainly serves.
bool ParseReferenceType(
    const SyntaxNode& node,
    kernel::MechanismReferenceKind kind,
    std::optional<std::uint64_t>& output,
    ParserCursor& cursor
);

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
    // A field's reference kind is optional; without one there is no domain to
    // hash a reference_type in, so it stays unparsed rather than guessing.
    if (!output.referenceKind)
    {
        return true;
    }
    return ParseReferenceType(
        node,
        *output.referenceKind,
        output.referenceType,
        cursor
    );
}

// `reference_type` narrows a reference to one target type. It used to accept
// only a bare 64-bit number -- the raw hash -- which no author would ever
// write, so the property was unusable in practice and nothing in the tree set
// it. It now takes the symbolic name and hashes it in the domain the
// reference kind implies, the same way every other name in the DSL is
// resolved.
//
// This matters beyond ergonomics: the Runtime Compiler needs the target type
// to resolve a `role -> mechanism field` read path against the right layout.
// Without it the field name was resolved against the *calling* mechanism's
// layout, which silently lands on a same-named field of the wrong type.
bool ParseReferenceType(
    const SyntaxNode& node,
    kernel::MechanismReferenceKind kind,
    std::optional<std::uint64_t>& output,
    ParserCursor& cursor
)
{
    const SyntaxNode* referenceType = FindUnique(
        node,
        "reference_type",
        cursor,
        false
    );
    if (referenceType == nullptr)
    {
        return true;
    }
    Token token;
    if (!RequireScalar(referenceType, cursor, "reference_type", token))
    {
        return false;
    }
    const std::string name(token.text);
    switch (kind)
    {
    case kernel::MechanismReferenceKind::Entity:
        output = kernel::StableEntityTypeId(name).value;
        return true;
    case kernel::MechanismReferenceKind::MechanismDefinition:
    case kernel::MechanismReferenceKind::MechanismInstance:
        output = kernel::StableMechanismTypeId(name).value;
        return true;
    case kernel::MechanismReferenceKind::Resource:
    case kernel::MechanismReferenceKind::Custom:
        // No Stable ID domain of their own; the author's own opaque tag.
        {
            std::uint64_t value = 0;
            if (!ParseUnsigned(
                    token,
                    std::numeric_limits<std::uint64_t>::max(),
                    value,
                    cursor))
            {
                cursor.Diagnostics().Error(
                    "dillen.authoring.reference_type_invalid",
                    "reference_type for resource/custom references must be a "
                    "number",
                    referenceType->span
                );
                return false;
            }
            output = value;
            return true;
        }
    }
    return false;
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
    return ParseReferenceType(
        node,
        output.referenceKind,
        output.referenceType,
        cursor
    );
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

// Defined below; the condition grammar needs read paths and the read-path
// grammar is easier to read next to the instruction that uses it.
bool ParseReadPath(
    const SyntaxNode& node,
    kernel::AlgorithmReadPathDefinition& output,
    ParserCursor& cursor
);
bool ParseCompareOperator(
    const SyntaxNode& node,
    kernel::AlgorithmCompareOperator& output,
    ParserCursor& cursor
);

// Shared by the constant and the computed forms of a field instruction, and
// by every generic transaction instruction: one `when` block, one grammar.
bool ParseAlgorithmConditions(
    const SyntaxNode& node,
    std::vector<kernel::AlgorithmConditionDefinition>& conditions,
    ParserCursor& cursor
)
{
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
            if (name.text == "compare")
            {
                // The general form. field_equals stays as its own kind so
                // existing content keeps lowering to exactly the same bytes.
                if (!conditionNode.block
                    || !RejectUnknown(
                        conditionNode,
                        {"left", "op", "right"},
                        cursor,
                        "compare condition"))
                {
                    return false;
                }
                const SyntaxNode* left =
                    FindUnique(conditionNode, "left", cursor, true);
                const SyntaxNode* right =
                    FindUnique(conditionNode, "right", cursor, true);
                condition.kind = kernel::AlgorithmConditionKind::Compare;
                if (left == nullptr
                    || right == nullptr
                    || !ParseReadPath(*left, condition.left, cursor)
                    || !ParseReadPath(*right, condition.right, cursor)
                    || !ParseCompareOperator(
                        conditionNode,
                        condition.compare,
                        cursor))
                {
                    return false;
                }
            }
            else if (name.text == "field_equals")
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
            else if (name.text == "capability_invoked")
            {
                Token token;
                if (!RequireScalar(
                        &conditionNode,
                        cursor,
                        "capability_invoked",
                        token))
                {
                    return false;
                }
                // Sugar over ScheduledEventTypeEquals: a Capability invocation
                // is delivered on its own derived inbox event type.
                condition.kind = kernel::AlgorithmConditionKind::
                    ScheduledEventTypeEquals;
                condition.eventType = kernel::CapabilityDeliveryEventType(
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
            conditions.push_back(std::move(condition));
        }
    }
    return true;
}

// A read path: where a run-time operand comes from.
//
//   { constant = 5 }
//   { event_payload = yes }
//   { self_field = ore_input }
//   { role = peer    field = counter }                     Mechanism field
//   { role = anchor  component = dillen.game.stock  field = amount }
//   { role = anchor  relation = { type = ...  direction = outgoing }
//     component = ...  field = ...  reduce = sum }
//
// One grammar covers the scalar and the aggregate case, because they are the
// same thing: a role slot holds a list, a Relation hop widens it again, and
// `reduce` says what a set of values means. Omitting `reduce` means
// require_one -- a path that reaches two values is an error rather than a
// silent "first one wins".
bool ParseReadPath(
    const SyntaxNode& node,
    kernel::AlgorithmReadPathDefinition& output,
    ParserCursor& cursor
)
{
    if (!node.block
        || !RejectUnknown(
            node,
            {
                "constant", "event_payload", "self_field", "role",
                "relation", "component", "field", "reduce"
            },
            cursor,
            "read path"))
    {
        return false;
    }

    const auto has = [&node, &cursor](std::string_view key)
    {
        return FindUnique(node, key, cursor, false) != nullptr;
    };

    int roots = 0;
    roots += has("constant") ? 1 : 0;
    roots += has("event_payload") ? 1 : 0;
    roots += has("self_field") ? 1 : 0;
    roots += has("role") ? 1 : 0;
    if (roots != 1)
    {
        cursor.Diagnostics().Error(
            "dillen.authoring.read_path_root_invalid",
            "a read path needs exactly one of constant, event_payload, "
            "self_field or role",
            node.span
        );
        return false;
    }

    if (has("reduce"))
    {
        std::string reduce;
        if (!ReadStringProperty(node, "reduce", cursor, reduce))
        {
            return false;
        }
        if (reduce == "require_one")
        {
            output.reduce = kernel::AlgorithmReduce::RequireOne;
        }
        else if (reduce == "sum")
        {
            output.reduce = kernel::AlgorithmReduce::Sum;
        }
        else if (reduce == "count")
        {
            output.reduce = kernel::AlgorithmReduce::Count;
        }
        else if (reduce == "min")
        {
            output.reduce = kernel::AlgorithmReduce::Minimum;
        }
        else if (reduce == "max")
        {
            output.reduce = kernel::AlgorithmReduce::Maximum;
        }
        else
        {
            cursor.Diagnostics().Error(
                "dillen.authoring.read_path_reduce_unknown",
                "unknown reducer: " + reduce,
                node.span
            );
            return false;
        }
    }

    if (has("constant"))
    {
        const SyntaxNode* value = FindUnique(node, "constant", cursor, true);
        Token token;
        if (!RequireScalar(value, cursor, "constant", token)
            || !InferScalarValue(token, output.constant, cursor))
        {
            return false;
        }
        output.root = kernel::AlgorithmReadRoot::Constant;
        output.terminal = kernel::AlgorithmReadTerminal::Value;
        return true;
    }
    if (has("event_payload"))
    {
        bool enabled = false;
        if (!ReadBoolProperty(node, "event_payload", cursor, enabled, false))
        {
            return false;
        }
        if (!enabled)
        {
            cursor.Diagnostics().Error(
                "dillen.authoring.read_path_root_invalid",
                "event_payload must be enabled with yes",
                node.span
            );
            return false;
        }
        output.root = kernel::AlgorithmReadRoot::EventPayload;
        output.terminal = kernel::AlgorithmReadTerminal::Value;
        return true;
    }
    if (has("self_field"))
    {
        if (!ReadStringProperty(node, "self_field", cursor, output.selfField))
        {
            return false;
        }
        output.root = kernel::AlgorithmReadRoot::SelfField;
        output.terminal = kernel::AlgorithmReadTerminal::Value;
        return true;
    }

    // role
    if (!ReadStringProperty(node, "role", cursor, output.role))
    {
        return false;
    }
    output.root = kernel::AlgorithmReadRoot::RoleTarget;
    if (!has("field"))
    {
        cursor.Diagnostics().Error(
            "dillen.authoring.read_path_terminal_missing",
            "a role read path must name the field it reads",
            node.span
        );
        return false;
    }

    if (has("relation"))
    {
        const SyntaxNode* relation = FindUnique(node, "relation", cursor, true);
        if (relation == nullptr
            || !relation->block
            || !RejectUnknown(
                *relation,
                {"type", "direction"},
                cursor,
                "read path relation"))
        {
            return false;
        }
        std::string type;
        if (!ReadStringProperty(*relation, "type", cursor, type))
        {
            return false;
        }
        output.traverseRelation = true;
        output.relationType = kernel::StableRelationTypeId(type);
        std::string direction = "outgoing";
        if (FindUnique(*relation, "direction", cursor, false) != nullptr
            && !ReadStringProperty(*relation, "direction", cursor, direction))
        {
            return false;
        }
        if (direction == "outgoing")
        {
            output.direction = kernel::AlgorithmRelationDirection::Outgoing;
        }
        else if (direction == "incoming")
        {
            output.direction = kernel::AlgorithmRelationDirection::Incoming;
        }
        else
        {
            cursor.Diagnostics().Error(
                "dillen.authoring.read_path_direction_unknown",
                "relation direction must be outgoing or incoming",
                relation->span
            );
            return false;
        }
    }

    if (has("component"))
    {
        std::string component;
        if (!ReadStringProperty(node, "component", cursor, component)
            || !ReadStringProperty(
                node,
                "field",
                cursor,
                output.componentField))
        {
            return false;
        }
        output.component = kernel::StableComponentTypeId(component);
        output.terminal = kernel::AlgorithmReadTerminal::ComponentField;
        return true;
    }

    // No component: the role must reference Mechanism Instances, and a
    // Relation hop lands on an Entity, so the two cannot combine. The compiler
    // re-checks this against the role's declared reference kind.
    if (output.traverseRelation)
    {
        cursor.Diagnostics().Error(
            "dillen.authoring.read_path_terminal_missing",
            "a Relation hop ends at an Entity, so it needs a component to read",
            node.span
        );
        return false;
    }
    if (!ReadStringProperty(node, "field", cursor, output.targetField))
    {
        return false;
    }
    output.terminal = kernel::AlgorithmReadTerminal::MechanismField;
    return true;
}

bool ParseBinaryOperator(
    const SyntaxNode& node,
    kernel::AlgorithmBinaryOperator& output,
    ParserCursor& cursor
)
{
    std::string op;
    if (!ReadStringProperty(node, "op", cursor, op))
    {
        return false;
    }
    if (op == "add") { output = kernel::AlgorithmBinaryOperator::Add; }
    else if (op == "sub") { output = kernel::AlgorithmBinaryOperator::Subtract; }
    else if (op == "mul") { output = kernel::AlgorithmBinaryOperator::Multiply; }
    else if (op == "div") { output = kernel::AlgorithmBinaryOperator::Divide; }
    else if (op == "min") { output = kernel::AlgorithmBinaryOperator::Minimum; }
    else if (op == "max") { output = kernel::AlgorithmBinaryOperator::Maximum; }
    else
    {
        cursor.Diagnostics().Error(
            "dillen.authoring.binary_operator_unknown",
            "unknown operator: " + op,
            node.span
        );
        return false;
    }
    return true;
}

bool ParseCompareOperator(
    const SyntaxNode& node,
    kernel::AlgorithmCompareOperator& output,
    ParserCursor& cursor
)
{
    std::string op;
    if (!ReadStringProperty(node, "op", cursor, op))
    {
        return false;
    }
    if (op == "eq") { output = kernel::AlgorithmCompareOperator::Equal; }
    else if (op == "ne") { output = kernel::AlgorithmCompareOperator::NotEqual; }
    else if (op == "lt") { output = kernel::AlgorithmCompareOperator::Less; }
    else if (op == "lte")
    {
        output = kernel::AlgorithmCompareOperator::LessOrEqual;
    }
    else if (op == "gt") { output = kernel::AlgorithmCompareOperator::Greater; }
    else if (op == "gte")
    {
        output = kernel::AlgorithmCompareOperator::GreaterOrEqual;
    }
    else
    {
        cursor.Diagnostics().Error(
            "dillen.authoring.compare_operator_unknown",
            "unknown comparison: " + op,
            node.span
        );
        return false;
    }
    return true;
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
            {"field", "value", "from_payload", "when", "op", "left", "right"},
            cursor,
            "algorithm field instruction"))
    {
        return false;
    }
    std::string field;
    bool fromPayload = false;
    if (!ReadStringProperty(node, "field", cursor, field)
        || !ReadBoolProperty(node, "from_payload", cursor, fromPayload, false))
    {
        return false;
    }

    // A `left` block turns this into a computed assignment. The constant form
    // is untouched and still lowers to the *Constant opcodes, which is what
    // keeps every existing Package compiling to exactly the same bytes.
    const SyntaxNode* left = FindUnique(node, "left", cursor, false);
    if (left != nullptr)
    {
        if (fromPayload || FindUnique(node, "value", cursor, false) != nullptr)
        {
            cursor.Diagnostics().Error(
                "dillen.authoring.field_instruction_operand_conflict",
                "a computed field instruction cannot also set value or "
                "from_payload",
                node.span
            );
            return false;
        }
        output = kernel::AlgorithmInstructionDefinition{};
        output.kind = kind == kernel::AlgorithmInstructionKind::SetField
            ? kernel::AlgorithmInstructionKind::SetFieldComputed
            : kernel::AlgorithmInstructionKind::AddFieldComputed;
        output.field = std::move(field);
        if (!ParseReadPath(*left, output.left, cursor))
        {
            return false;
        }
        const SyntaxNode* right = FindUnique(node, "right", cursor, false);
        const bool hasOperator =
            FindUnique(node, "op", cursor, false) != nullptr;
        if ((right == nullptr) != !hasOperator)
        {
            cursor.Diagnostics().Error(
                "dillen.authoring.field_instruction_operand_conflict",
                "a computed field instruction needs op and right together, "
                "or neither",
                node.span
            );
            return false;
        }
        if (right != nullptr)
        {
            output.hasRight = true;
            if (!ParseReadPath(*right, output.right, cursor)
                || !ParseBinaryOperator(node, output.binaryOperator, cursor))
            {
                return false;
            }
        }
        return ParseAlgorithmConditions(node, output.conditions, cursor);
    }
    if (FindUnique(node, "op", cursor, false) != nullptr
        || FindUnique(node, "right", cursor, false) != nullptr)
    {
        cursor.Diagnostics().Error(
            "dillen.authoring.field_instruction_operand_conflict",
            "op and right require a left read path",
            node.span
        );
        return false;
    }

    kernel::MechanismValue value;
    if (!fromPayload)
    {
        const SyntaxNode* valueNode = FindUnique(node, "value", cursor, true);
        Token valueToken;
        if (!RequireScalar(valueNode, cursor, "value", valueToken)
            || !InferScalarValue(valueToken, value, cursor))
        {
            return false;
        }
    }
    else if (FindUnique(node, "value", cursor, false) != nullptr)
    {
        cursor.Diagnostics().Error(
            "dillen.authoring.field_instruction_payload_conflict",
            "field instruction cannot set both value and from_payload",
            node.span
        );
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
    output.operandFromPayload = fromPayload;
    return ParseAlgorithmConditions(node, output.conditions, cursor);
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
    // Mirrors add_relation exactly: the same five keys identify the same edge,
    // and StableRelationId over the resolved endpoints reproduces the id that
    // add_relation's edge was stored under. Authoring had no way to remove a
    // Relation at all until now, even though the instruction kind, the opcode,
    // the compiler lowering and the VM have all existed and been covered by the
    // frozen save-format asserts -- only the DSL entry point was missing.
    if (name == "remove_relation")
    {
        if (!RejectUnknown(
                node,
                {
                    "relation", "source_entity_type",
                    "source_definition", "target_entity_type",
                    "target_definition"
                },
                cursor,
                "remove relation instruction")) return false;
        std::string relation;
        std::string sourceType;
        std::string sourceDefinition;
        std::string targetType;
        std::string targetDefinition;
        output.kind = kernel::AlgorithmInstructionKind::RemoveRelation;
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
        output.relation = kernel::StableRelationId(
            output.relationType,
            output.sourceEntity,
            output.targetEntity
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
    if (name == "invoke_capability")
    {
        if (!RejectUnknown(
                node,
                {"capability", "delay", "priority", "payload",
                 "payload_from", "target_role", "version", "when"},
                cursor,
                "invoke capability instruction")) return false;
        std::uint32_t delay = 0;
        output.kind = kernel::AlgorithmInstructionKind::InvokeCapability;
        const SyntaxNode* payload = FindUnique(
            node,
            "payload",
            cursor,
            false
        );
        const SyntaxNode* payloadFrom = FindUnique(
            node,
            "payload_from",
            cursor,
            false
        );
        if ((payload == nullptr) == (payloadFrom == nullptr))
        {
            cursor.Diagnostics().Error(
                "dillen.authoring.invoke_capability_payload_invalid",
                "invoke_capability requires exactly one of payload or "
                "payload_from",
                node.span
            );
            return false;
        }
        if (!ReadStringProperty(
                node, "capability", cursor, output.capabilityName)
            || !ReadUInt32Property(node, "delay", cursor, delay))
        {
            return false;
        }
        if (payload != nullptr)
        {
            if (!scalarValue("payload", output.payload)) return false;
        }
        else
        {
            output.payloadComputed = true;
            if (!ParseReadPath(*payloadFrom, output.payloadSource, cursor))
            {
                return false;
            }
        }
        if (FindUnique(node, "priority", cursor, false) != nullptr
            && !ReadInt32Property(
                node,
                "priority",
                cursor,
                output.priority)) return false;
        if (FindUnique(node, "target_role", cursor, false) != nullptr
            && !ReadStringProperty(
                node,
                "target_role",
                cursor,
                output.targetRoleName)) return false;
        if (FindUnique(node, "version", cursor, false) != nullptr)
        {
            std::uint32_t version = 0;
            if (!ReadUInt32Property(node, "version", cursor, version)
                || version == 0)
            {
                cursor.Diagnostics().Error(
                    "dillen.authoring.invoke_capability_version_invalid",
                    "invoke_capability version must be a positive integer",
                    node.span
                );
                return false;
            }
            output.capabilityVersions.minimumInclusive = version;
            output.capabilityVersions.maximumExclusive = version + 1;
        }
        output.dueTickOffset = delay;
        return ParseAlgorithmConditions(node, output.conditions, cursor);
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
                || instructionName.text == "remove_relation"
                || instructionName.text == "spawn_mechanism"
                || instructionName.text == "schedule_event"
                || instructionName.text == "invoke_capability"
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
        const kernel::AlgorithmInstructionKind fieldKind =
            name == "set_field"
                ? kernel::AlgorithmInstructionKind::SetField
                : kernel::AlgorithmInstructionKind::AddField;
        // A `when` block (or `from_payload`) means the controlled script needs
        // the same conditional field mutation the declarative backend has, so
        // route it through the shared declarative lowering as a Transact.
        if (FindUnique(node, "when", cursor, false) != nullptr
            || FindUnique(node, "from_payload", cursor, false) != nullptr)
        {
            kernel::AlgorithmInstructionDefinition action;
            if (!ParseAlgorithmFieldInstruction(
                    node, fieldKind, action, cursor))
            {
                return false;
            }
            output.kind = kernel::ControlledScriptInstructionKind::Transact;
            output.action = std::move(action);
            return true;
        }
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
    // Any other name: a declarative transaction instruction (entity /
    // component / add_relation / remove_relation / spawn / schedule_event /
    // rng / invoke_capability). Parse it with the declarative grammar and wrap
    // it as a Transact so it lowers and executes through the shared code path.
    //
    // cancel_event is deliberately NOT in that list, here or in the declarative
    // grammar. The instruction kind, the opcode, the lowering and the VM all
    // exist, but CancelEvent carries a scheduled-event *sequence number*, which
    // is assigned at run time. A literal sequence written in source would name
    // whatever event happened to get that number, so the construct cannot be
    // authored correctly until an operand can be read at run time. Adding a
    // syntax that can only be used wrongly is worse than leaving the gap
    // visible -- it waits for the read-operand work.
    kernel::AlgorithmInstructionDefinition action;
    if (ParseGenericAlgorithmInstruction(name, node, action, cursor))
    {
        output.kind = kernel::ControlledScriptInstructionKind::Transact;
        output.action = std::move(action);
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
                "role", "dependencies", "provides"
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
    if (const SyntaxNode* role = FindUnique(
            root.body,
            "role",
            cursor,
            false))
    {
        Token token;
        if (!RequireScalar(role, cursor, "role", token))
        {
            return false;
        }
        if (token.text == "contract")
        {
            document.value.role = kernel::PackageRole::Contract;
        }
        else if (token.text == "mechanism")
        {
            document.value.role = kernel::PackageRole::Mechanism;
        }
        else if (token.text == "content")
        {
            document.value.role = kernel::PackageRole::Content;
        }
        else if (token.text == "presentation")
        {
            document.value.role = kernel::PackageRole::Presentation;
        }
        else
        {
            cursor.Diagnostics().Error(
                "dillen.authoring.package_role_invalid",
                "package role must be contract, mechanism, content or "
                "presentation",
                token.span
            );
            return false;
        }
    }
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
                "algorithm_version", "fields", "roles",
                "provides_capabilities"
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
    // Role bindings. The schema could declare role slots and the compiler
    // could lower them, but the Definition grammar had no way to fill one, so
    // every role slot was necessarily empty in anything authored externally.
    // That made the whole reference side of the data model unreachable from a
    // Package -- and, once read paths arrived, made `role = ...` unresolvable
    // by construction.
    //
    //   roles = {
    //       home = {
    //           entity = { entity_type = dillen.eco.place
    //                      definition  = dillen.eco.capital }
    //       }
    //   }
    //
    // Entity references only. The other reference kinds a role slot may
    // declare -- Mechanism Instance in particular -- name things that do not
    // exist when a Definition is written, so binding them is a run-time
    // concern and is deliberately not expressible here.
    const SyntaxNode* roles = FindUnique(root.body, "roles", cursor, false);
    if (roles != nullptr)
    {
        if (!roles->block)
        {
            cursor.Diagnostics().Error(
                "dillen.authoring.role_entry_expected",
                "roles must be a block of role bindings",
                roles->span
            );
            return false;
        }
        for (std::size_t index = 0; index < roles->keys.size(); ++index)
        {
            const Token& roleName = roles->keys[index];
            const SyntaxNode& binding = roles->values[index];
            if (!binding.block
                || !RejectUnknown(
                    binding,
                    {"entity"},
                    cursor,
                    "role binding"))
            {
                return false;
            }
            std::vector<kernel::MechanismReference> references;
            for (const std::size_t entry : FindAll(binding, "entity"))
            {
                const SyntaxNode& target = binding.values[entry];
                std::string entityType;
                std::string entityDefinition;
                if (!target.block
                    || !RejectUnknown(
                        target,
                        {"entity_type", "definition"},
                        cursor,
                        "role entity binding")
                    || !ReadStringProperty(
                        target,
                        "entity_type",
                        cursor,
                        entityType)
                    || !ReadStringProperty(
                        target,
                        "definition",
                        cursor,
                        entityDefinition))
                {
                    return false;
                }
                // Resolved the same way add_relation resolves its endpoints,
                // so a role and a Relation naming the same Entity agree.
                kernel::MechanismReference reference;
                reference.kind = kernel::MechanismReferenceKind::Entity;
                reference.type = kernel::StableEntityTypeId(entityType).value;
                reference.value = kernel::StableEntityId(
                    kernel::StableEntityDefinitionId(
                        kernel::StableEntityTypeId(entityType),
                        entityDefinition
                    )
                ).value;
                references.push_back(reference);
            }
            if (references.empty())
            {
                cursor.Diagnostics().Error(
                    "dillen.authoring.role_entry_expected",
                    "role binding names no target",
                    binding.span
                );
                return false;
            }
            document.value.roles.emplace(
                std::string(roleName.text),
                std::move(references)
            );
        }
    }
    const SyntaxNode* provides = FindUnique(
        root.body,
        "provides_capabilities",
        cursor,
        false
    );
    if (provides != nullptr)
    {
        if (!provides->block)
        {
            cursor.Diagnostics().Error(
                "dillen.authoring.provides_capabilities_invalid",
                "provides_capabilities must be a list of Capability names "
                "and/or 'requirement' blocks",
                provides->span
            );
            return false;
        }
        for (const Token& item : provides->items)
        {
            kernel::CapabilityProvisionDeclaration declaration;
            declaration.capabilityName = item.text;
            document.value.providedCapabilities.push_back(
                std::move(declaration)
            );
        }
        for (std::size_t index : FindAll(*provides, "requirement"))
        {
            const SyntaxNode& requirement = provides->values[index];
            if (!requirement.block
                || !RejectUnknown(
                    requirement,
                    {"name", "minimum_version", "maximum_version"},
                    cursor,
                    "provided capability requirement"))
            {
                return false;
            }
            kernel::CapabilityProvisionDeclaration declaration;
            if (!ReadStringProperty(
                    requirement, "name", cursor, declaration.capabilityName)
                || !ReadUInt32Property(
                    requirement,
                    "minimum_version",
                    cursor,
                    declaration.versions.minimumInclusive))
            {
                return false;
            }
            if (FindUnique(
                    requirement, "maximum_version", cursor, false) != nullptr)
            {
                std::uint32_t maximum = 0;
                if (!ReadUInt32Property(
                        requirement, "maximum_version", cursor, maximum))
                {
                    return false;
                }
                declaration.versions.maximumExclusive = maximum;
            }
            document.value.providedCapabilities.push_back(
                std::move(declaration)
            );
        }
        if (FindAll(*provides, "requirement").size() != provides->keys.size())
        {
            cursor.Diagnostics().Error(
                "dillen.authoring.provides_capabilities_invalid",
                "provides_capabilities blocks only accept 'requirement' keys",
                provides->span
            );
            return false;
        }
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
