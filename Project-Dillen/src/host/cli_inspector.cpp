#include "cli_inspector.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#endif

#include "mechanism_command.hpp"
#include "mechanism_instance.hpp"
#include "world_transaction.hpp"

namespace dillen::host {

namespace {

constexpr std::uint64_t kMaximumTicksPerCommand = 1000000;
constexpr std::uintmax_t kMaximumSaveBytes = 512ULL * 1024ULL * 1024ULL;

std::vector<std::string> Tokenize(std::string_view line, bool& valid)
{
    valid = true;
    std::vector<std::string> tokens;
    std::size_t index = 0;
    while (index < line.size())
    {
        while (index < line.size()
            && static_cast<unsigned char>(line[index]) <= ' ')
        {
            ++index;
        }
        if (index == line.size() || line[index] == '#')
        {
            break;
        }
        std::string token;
        if (line[index] == '"' || line[index] == '\'')
        {
            const char quote = line[index++];
            bool closed = false;
            while (index < line.size())
            {
                const char value = line[index++];
                if (value == quote)
                {
                    closed = true;
                    break;
                }
                if (value == '\\' && index < line.size())
                {
                    const char escaped = line[index++];
                    switch (escaped)
                    {
                    case 'n': token.push_back('\n'); break;
                    case 'r': token.push_back('\r'); break;
                    case 't': token.push_back('\t'); break;
                    default: token.push_back(escaped); break;
                    }
                }
                else
                {
                    token.push_back(value);
                }
            }
            if (!closed)
            {
                valid = false;
                return {};
            }
        }
        else
        {
            const std::size_t begin = index;
            while (index < line.size()
                && static_cast<unsigned char>(line[index]) > ' ')
            {
                ++index;
            }
            token.assign(line.substr(begin, index - begin));
        }
        tokens.push_back(std::move(token));
    }
    return tokens;
}

template <typename Integer>
bool ParseInteger(std::string_view text, Integer& output)
{
    int base = 10;
    if (text.size() > 2 && text[0] == '0'
        && (text[1] == 'x' || text[1] == 'X'))
    {
        text.remove_prefix(2);
        base = 16;
    }
    if (text.empty())
    {
        return false;
    }
    Integer value{};
    const auto result = std::from_chars(
        text.data(), text.data() + text.size(), value, base
    );
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
    {
        return false;
    }
    output = value;
    return true;
}

bool ParseFieldValue(
    std::string_view text,
    kernel::MechanismValueKind expected,
    kernel::MechanismValue& output,
    std::string& message
)
{
    if (expected == kernel::MechanismValueKind::Null)
    {
        if (text != "null")
        {
            message = "field requires null";
            return false;
        }
        output = {};
        return true;
    }
    if (expected == kernel::MechanismValueKind::Boolean)
    {
        if (text == "true" || text == "yes")
        {
            output = kernel::MechanismValue(true);
            return true;
        }
        if (text == "false" || text == "no")
        {
            output = kernel::MechanismValue(false);
            return true;
        }
        message = "field requires true/false or yes/no";
        return false;
    }
    if (expected == kernel::MechanismValueKind::Integer)
    {
        std::int64_t integer = 0;
        if (!ParseInteger(text, integer))
        {
            message = "field requires an integer";
            return false;
        }
        output = kernel::MechanismValue(integer);
        return true;
    }
    if (expected == kernel::MechanismValueKind::Decimal)
    {
        std::string copy(text);
        char* end = nullptr;
        const double decimal = std::strtod(copy.c_str(), &end);
        if (end != copy.c_str() + copy.size() || !std::isfinite(decimal))
        {
            message = "field requires a finite decimal";
            return false;
        }
        output = kernel::MechanismValue(decimal);
        return true;
    }
    if (expected == kernel::MechanismValueKind::String)
    {
        output = kernel::MechanismValue(std::string(text));
        return true;
    }
    message = "CLI scalar input does not support this field value kind";
    return false;
}

std::string Hex(std::uint64_t value)
{
    std::ostringstream output;
    output << "0x" << std::hex << std::uppercase << value;
    return output.str();
}

const char* LifecycleName(kernel::MechanismLifecycleState state)
{
    switch (state)
    {
    case kernel::MechanismLifecycleState::Created: return "created";
    case kernel::MechanismLifecycleState::Active: return "active";
    case kernel::MechanismLifecycleState::Paused: return "paused";
    case kernel::MechanismLifecycleState::Completed: return "completed";
    case kernel::MechanismLifecycleState::Failed: return "failed";
    }
    return "unknown";
}

std::string Escape(std::string_view value)
{
    std::string output;
    output.push_back('"');
    for (const char character : value)
    {
        switch (character)
        {
        case '\\': output += "\\\\"; break;
        case '"': output += "\\\""; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default: output.push_back(character); break;
        }
    }
    output.push_back('"');
    return output;
}

std::string FormatValue(const kernel::MechanismValue& value)
{
    using kernel::MechanismReference;
    using kernel::MechanismValue;
    switch (value.Kind())
    {
    case kernel::MechanismValueKind::Null:
        return "null";
    case kernel::MechanismValueKind::Boolean:
        return std::get<bool>(value.data) ? "true" : "false";
    case kernel::MechanismValueKind::Integer:
        return std::to_string(std::get<std::int64_t>(value.data));
    case kernel::MechanismValueKind::Decimal:
    {
        std::ostringstream output;
        output << std::setprecision(17) << std::get<double>(value.data);
        return output.str();
    }
    case kernel::MechanismValueKind::String:
        return Escape(std::get<std::string>(value.data));
    case kernel::MechanismValueKind::Reference:
    {
        const MechanismReference& reference =
            std::get<MechanismReference>(value.data);
        return "ref(" + std::to_string(static_cast<int>(reference.kind))
            + "," + Hex(reference.type) + "," + Hex(reference.value) + ")";
    }
    case kernel::MechanismValueKind::List:
    {
        std::string output = "[";
        const MechanismValue::List& values =
            std::get<MechanismValue::List>(value.data);
        for (std::size_t index = 0; index < values.size(); ++index)
        {
            if (index != 0) output += ", ";
            output += FormatValue(values[index]);
        }
        output += "]";
        return output;
    }
    case kernel::MechanismValueKind::Object:
    {
        std::string output = "{";
        const MechanismValue::Object& values =
            std::get<MechanismValue::Object>(value.data);
        bool first = true;
        for (const auto& [name, nested] : values)
        {
            if (!first) output += ", ";
            first = false;
            output += name + ": " + FormatValue(nested);
        }
        output += "}";
        return output;
    }
    }
    return "null";
}

bool WriteAtomically(
    const std::filesystem::path& target,
    const std::vector<std::uint8_t>& bytes,
    std::string& message
)
{
    if (target.empty())
    {
        message = "save path is empty";
        return false;
    }
    const std::filesystem::path temporary = target.string() + ".tmp";
    std::error_code error;
    std::filesystem::remove(temporary, error);
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output)
    {
        message = "temporary save file could not be opened";
        return false;
    }
    output.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size())
    );
    output.flush();
    if (!output)
    {
        output.close();
        std::filesystem::remove(temporary, error);
        message = "temporary save file could not be written";
        return false;
    }
    output.close();
#ifdef _WIN32
    if (!MoveFileExW(
            temporary.c_str(),
            target.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        std::filesystem::remove(temporary, error);
        message = "atomic save replacement failed";
        return false;
    }
#else
    std::filesystem::rename(temporary, target, error);
    if (error)
    {
        std::filesystem::remove(temporary, error);
        message = "atomic save replacement failed";
        return false;
    }
#endif
    return true;
}

bool ReadSave(
    const std::filesystem::path& path,
    std::vector<std::uint8_t>& bytes,
    std::string& message
)
{
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error || size > kMaximumSaveBytes
        || size > static_cast<std::uintmax_t>(
            std::numeric_limits<std::size_t>::max()))
    {
        message = error ? "save file could not be inspected"
                        : "save file exceeds Host safety limit";
        return false;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        message = "save file could not be opened";
        return false;
    }
    bytes.resize(static_cast<std::size_t>(size));
    input.read(
        reinterpret_cast<char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size())
    );
    if (!input && !bytes.empty())
    {
        message = "save file could not be read completely";
        return false;
    }
    return true;
}

CliCommandResult Failure(std::ostream& errors, std::string message)
{
    errors << "error: " << message << '\n';
    return {true, false, CliCommandDisposition::Continue};
}

}

CliRunReport::operator bool() const noexcept
{
    return failures == 0;
}

CliInspector::CliInspector(
    runtime::KernelRuntime& runtime,
    const kernel::FrozenRuntimeCatalog& catalog
)
    : runtime_(runtime), catalog_(catalog)
{
}

void CliInspector::PrintHelp(std::ostream& output) const
{
    output
        << "help\n"
        << "status\n"
        << "list entities|components|relations|mechanisms|queue|events\n"
        << "show entity <id>\n"
        << "show component <entity-id> <component-type-id>\n"
        << "show relation <id>\n"
        << "show mechanism <id>\n"
        << "tick [count]\n"
        << "set mechanism <id> <field> <scalar-value>\n"
        << "set component <entity-id> <component-type-id> <field> <scalar-value>\n"
        << "enqueue mechanism <id> <field> <scalar-value> <tick> [priority]\n"
        << "save <path>\n"
        << "load <path>\n"
        << "quit\n";
}

void CliInspector::PrintStatus(std::ostream& output) const
{
    const runtime::WorldQuerySnapshot& query = runtime_.Query();
    output
        << "ruleset=" << Hex(catalog_.ActiveRuleset().value)
        << '@' << catalog_.ActiveRulesetVersion()
        << " fingerprint=" << catalog_.Fingerprint().ToHex() << '\n'
        << "tick=" << query.Tick()
        << " revision=" << query.Revision()
        << " publication=" << query.Publication() << '\n'
        << "entities=" << query.Entities().Size()
        << " components=" << query.Components().Size()
        << " relations=" << query.Relations().Size()
        << " mechanisms=" << query.Mechanisms().Size() << '\n'
        << "queued=" << runtime_.Commands().Size()
        << " events=" << runtime_.Events().Size() << '\n';
}

CliCommandResult CliInspector::ExecuteLine(
    std::string_view line,
    std::ostream& output,
    std::ostream& errors
)
{
    bool valid = false;
    const std::vector<std::string> tokens = Tokenize(line, valid);
    if (!valid)
    {
        return Failure(errors, "unterminated quoted token");
    }
    if (tokens.empty())
    {
        return {};
    }
    const std::string& command = tokens[0];
    if (command == "help")
    {
        PrintHelp(output);
        return {};
    }
    if (command == "status")
    {
        PrintStatus(output);
        return {};
    }
    if (command == "quit" || command == "exit")
    {
        return {true, true, CliCommandDisposition::Exit};
    }
    if (command == "tick")
    {
        std::uint64_t count = 1;
        if (tokens.size() > 2
            || (tokens.size() == 2 && !ParseInteger(tokens[1], count))
            || count == 0 || count > kMaximumTicksPerCommand)
        {
            return Failure(errors, "tick expects a count from 1 to 1000000");
        }
        for (std::uint64_t index = 0; index < count; ++index)
        {
            const std::uint64_t nextTick = runtime_.Query().Tick() + 1;
            if (!runtime_.RunTick(nextTick))
            {
                return Failure(errors, "Kernel Runtime rejected tick "
                    + std::to_string(nextTick));
            }
        }
        output << "tick=" << runtime_.Query().Tick()
               << " revision=" << runtime_.Query().Revision() << '\n';
        return {};
    }
    if (command == "list")
    {
        if (tokens.size() != 2)
        {
            return Failure(errors, "list expects one collection name");
        }
        const runtime::WorldQuerySnapshot& query = runtime_.Query();
        if (tokens[1] == "entities")
        {
            for (const auto& [id, entity] : query.Entities().All())
            {
                output << Hex(id.value)
                       << " definition=" << Hex(entity.definition.value)
                       << " type=" << Hex(entity.type.value) << '\n';
            }
            return {};
        }
        if (tokens[1] == "components")
        {
            for (const auto& [key, component] : query.Components().All())
            {
                output << "owner=" << Hex(component.owner.value)
                       << " type=" << Hex(component.type.value)
                       << " schema=" << component.schemaVersion
                       << " fields=" << component.values.size() << '\n';
            }
            return {};
        }
        if (tokens[1] == "relations")
        {
            for (const auto& [id, relation] : query.Relations().All())
            {
                output << Hex(id.value)
                       << " type=" << Hex(relation.type.value)
                       << " source=" << Hex(relation.source.value)
                       << " target=" << Hex(relation.target.value) << '\n';
            }
            return {};
        }
        if (tokens[1] == "mechanisms")
        {
            for (const auto& [id, instance] : query.Mechanisms().All())
            {
                output << Hex(id.value)
                       << " definition=" << Hex(instance.definition.value)
                       << " type=" << Hex(instance.type.value)
                       << " lifecycle=" << LifecycleName(instance.lifecycle)
                       << " fields=" << instance.values.size() << '\n';
            }
            return {};
        }
        if (tokens[1] == "queue")
        {
            for (const kernel::QueuedWorldTransaction& queued
                : runtime_.Commands().Pending())
            {
                output << "sequence=" << queued.sequence
                       << " tick=" << queued.notBeforeTick
                       << " priority=" << queued.priority
                       << " commands=" << queued.transaction.commands.size()
                       << '\n';
            }
            return {};
        }
        if (tokens[1] == "events")
        {
            for (const kernel::WorldEvent& event : runtime_.Events().Pending())
            {
                output << "sequence=" << event.sequence
                       << " tick=" << event.tick
                       << " transaction=" << event.transactionSequence
                       << " kind=" << event.payload.index() << '\n';
            }
            return {};
        }
        return Failure(errors, "unknown list collection: " + tokens[1]);
    }
    if (command == "show")
    {
        const runtime::WorldQuerySnapshot& query = runtime_.Query();
        if (tokens.size() < 3)
        {
            return Failure(errors, "show expects an object kind and id");
        }
        std::uint64_t firstId = 0;
        if (!ParseInteger(tokens[2], firstId))
        {
            return Failure(errors, "invalid object id");
        }
        if (tokens[1] == "entity" && tokens.size() == 3)
        {
            const world::EntityRecord* entity = query.Entities().Find({firstId});
            if (entity == nullptr) return Failure(errors, "entity not found");
            output << "entity " << Hex(entity->id.value)
                   << " definition=" << Hex(entity->definition.value)
                   << " type=" << Hex(entity->type.value) << '\n';
            for (const kernel::ComponentTypeId type
                : query.Components().FindTypes(entity->id))
            {
                output << "  component=" << Hex(type.value) << '\n';
            }
            return {};
        }
        if (tokens[1] == "relation" && tokens.size() == 3)
        {
            const world::RelationRecord* relation =
                query.Relations().Find({firstId});
            if (relation == nullptr) return Failure(errors, "relation not found");
            output << "relation " << Hex(relation->id.value)
                   << " type=" << Hex(relation->type.value)
                   << " source=" << Hex(relation->source.value)
                   << " target=" << Hex(relation->target.value) << '\n';
            return {};
        }
        if (tokens[1] == "mechanism" && tokens.size() == 3)
        {
            const kernel::MechanismInstance* instance =
                query.Mechanisms().Find({firstId});
            if (instance == nullptr)
            {
                return Failure(errors, "mechanism instance not found");
            }
            output << "mechanism " << Hex(instance->id.value)
                   << " definition=" << Hex(instance->definition.value)
                   << " type=" << Hex(instance->type.value)
                   << " schema=" << instance->schemaVersion
                   << " lifecycle=" << LifecycleName(instance->lifecycle)
                   << " fault=" << static_cast<int>(instance->algorithmFault.code)
                   << '\n';
            const kernel::CompiledMechanismLayout* layout =
                catalog_.FindLayout(instance->type, instance->schemaVersion);
            for (std::size_t index = 0; index < instance->values.size(); ++index)
            {
                const std::string name = layout != nullptr
                    && index < layout->fields.size()
                    ? layout->fields[index].name
                    : std::to_string(index);
                output << "  " << name << '='
                       << FormatValue(instance->values[index]) << '\n';
            }
            return {};
        }
        if (tokens[1] == "component" && tokens.size() == 4)
        {
            std::uint64_t typeId = 0;
            if (!ParseInteger(tokens[3], typeId))
            {
                return Failure(errors, "invalid component type id");
            }
            const world::ComponentRecord* component =
                query.Components().Find({firstId}, {typeId});
            if (component == nullptr)
            {
                return Failure(errors, "component not found");
            }
            output << "component owner=" << Hex(component->owner.value)
                   << " type=" << Hex(component->type.value)
                   << " schema=" << component->schemaVersion << '\n';
            const kernel::CompiledComponentLayout* layout =
                catalog_.FindComponentLayout(
                    component->type, component->schemaVersion
                );
            for (std::size_t index = 0; index < component->values.size(); ++index)
            {
                const std::string name = layout != nullptr
                    && index < layout->fields.size()
                    ? layout->fields[index].name
                    : std::to_string(index);
                output << "  " << name << '='
                       << FormatValue(component->values[index]) << '\n';
            }
            return {};
        }
        return Failure(errors, "invalid show command");
    }
    if (command == "set" || command == "enqueue")
    {
        const bool queued = command == "enqueue";
        const std::size_t minimum = queued ? 6 : 5;
        if (tokens.size() < minimum)
        {
            return Failure(errors, "set/enqueue command is incomplete");
        }
        kernel::WorldTransaction transaction;
        if (tokens[1] == "mechanism")
        {
            if ((!queued && tokens.size() != 5)
                || (queued && tokens.size() != 6 && tokens.size() != 7))
            {
                return Failure(errors, "invalid mechanism command arity");
            }
            std::uint64_t id = 0;
            if (!ParseInteger(tokens[2], id))
            {
                return Failure(errors, "invalid mechanism instance id");
            }
            const kernel::MechanismInstance* instance =
                runtime_.Query().Mechanisms().Find({id});
            if (instance == nullptr)
            {
                return Failure(errors, "mechanism instance not found");
            }
            const auto field = catalog_.ResolveFieldSlot(
                instance->type, instance->schemaVersion, tokens[3]
            );
            const kernel::CompiledMechanismLayout* layout =
                catalog_.FindLayout(instance->type, instance->schemaVersion);
            if (!field || layout == nullptr
                || field->value >= layout->fields.size())
            {
                return Failure(errors, "mechanism field not found");
            }
            kernel::MechanismValue value;
            std::string valueError;
            if (!ParseFieldValue(
                    tokens[4],
                    layout->fields[field->value].kind,
                    value,
                    valueError))
            {
                return Failure(errors, std::move(valueError));
            }
            transaction.commands.push_back(kernel::WorldCommand::Mechanism(
                kernel::MechanismCommand::SetField(
                    instance->id, *field, std::move(value)
                )
            ));
        }
        else if (tokens[1] == "component" && !queued)
        {
            if (tokens.size() != 6)
            {
                return Failure(errors, "invalid component command arity");
            }
            std::uint64_t entityId = 0;
            std::uint64_t typeId = 0;
            if (!ParseInteger(tokens[2], entityId)
                || !ParseInteger(tokens[3], typeId))
            {
                return Failure(errors, "invalid entity or component id");
            }
            const world::ComponentRecord* component =
                runtime_.Query().Components().Find({entityId}, {typeId});
            if (component == nullptr)
            {
                return Failure(errors, "component not found");
            }
            const auto field = catalog_.ResolveComponentFieldSlot(
                component->type, component->schemaVersion, tokens[4]
            );
            const kernel::CompiledComponentLayout* layout =
                catalog_.FindComponentLayout(
                    component->type, component->schemaVersion
                );
            if (!field || layout == nullptr
                || field->value >= layout->fields.size())
            {
                return Failure(errors, "component field not found");
            }
            kernel::MechanismValue value;
            std::string valueError;
            if (!ParseFieldValue(
                    tokens[5],
                    layout->fields[field->value].kind,
                    value,
                    valueError))
            {
                return Failure(errors, std::move(valueError));
            }
            transaction.commands.push_back(
                kernel::WorldCommand::SetComponentField(
                    component->owner,
                    component->type,
                    *field,
                    std::move(value)
                )
            );
        }
        else
        {
            return Failure(errors, queued
                ? "enqueue currently accepts generic mechanism field commands"
                : "set expects mechanism or component");
        }
        if (queued)
        {
            std::uint64_t tick = 0;
            std::int32_t priority = 0;
            if (!ParseInteger(tokens[5], tick)
                || (tokens.size() == 7
                    && !ParseInteger(tokens[6], priority)))
            {
                return Failure(errors, "invalid queue tick or priority");
            }
            const std::uint64_t sequence = runtime_.Enqueue(
                std::move(transaction), tick, priority
            );
            output << "queued sequence=" << sequence << '\n';
            return {};
        }
        const kernel::WorldTransactionResult result = runtime_.ApplyImmediate(
            transaction, runtime_.Query().Tick()
        );
        if (!result)
        {
            return Failure(errors, "World Transaction rejected at command "
                + std::to_string(result.commandIndex));
        }
        output << "committed changes=" << result.changes.size()
               << " revision=" << runtime_.Query().Revision() << '\n';
        return {};
    }
    if (command == "save")
    {
        if (tokens.size() != 2)
        {
            return Failure(errors, "save expects one path");
        }
        std::vector<std::uint8_t> bytes;
        const persistence::RuntimePersistenceReport report =
            persistence_.Save(runtime_, bytes);
        if (!report)
        {
            return Failure(errors, "runtime save failed: " + report.message);
        }
        std::string message;
        if (!WriteAtomically(tokens[1], bytes, message))
        {
            return Failure(errors, std::move(message));
        }
        output << "saved bytes=" << bytes.size() << '\n';
        return {};
    }
    if (command == "load")
    {
        if (tokens.size() != 2)
        {
            return Failure(errors, "load expects one path");
        }
        std::vector<std::uint8_t> bytes;
        std::string message;
        if (!ReadSave(tokens[1], bytes, message))
        {
            return Failure(errors, std::move(message));
        }
        const persistence::RuntimePersistenceReport report =
            persistence_.Load(runtime_, bytes);
        if (!report)
        {
            return Failure(errors, "runtime load failed: " + report.message);
        }
        output << "loaded bytes=" << bytes.size()
               << " tick=" << runtime_.Query().Tick()
               << " revision=" << runtime_.Query().Revision() << '\n';
        return {};
    }
    errors << "error: unknown command: " << command << '\n';
    return {false, false, CliCommandDisposition::Continue};
}

CliRunReport CliInspector::Run(
    std::istream& input,
    std::ostream& output,
    std::ostream& errors,
    bool showPrompt
)
{
    CliRunReport report;
    std::string line;
    while (true)
    {
        if (showPrompt)
        {
            output << "dillen> " << std::flush;
        }
        if (!std::getline(input, line))
        {
            break;
        }
        bool tokenValid = false;
        const std::vector<std::string> tokens = Tokenize(line, tokenValid);
        if (tokenValid && tokens.empty())
        {
            continue;
        }
        ++report.commands;
        const CliCommandResult result = ExecuteLine(line, output, errors);
        if (!result.success)
        {
            ++report.failures;
        }
        if (result.disposition == CliCommandDisposition::Exit)
        {
            report.exitRequested = true;
            break;
        }
    }
    return report;
}

}
