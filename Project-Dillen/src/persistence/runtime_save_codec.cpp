#include "runtime_save_codec.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <type_traits>
#include <utility>

namespace dillen::persistence {

namespace {

constexpr std::uint8_t kSaveMagic[] = {
    'D', 'I', 'L', 'S', 'A', 'V', 'E', 0
};
constexpr std::uint8_t kFactMagic[] = {
    'D', 'I', 'L', 'F', 'A', 'C', 'T', 0
};
constexpr std::uint32_t kMaximumContainerItems = 16U * 1024U * 1024U;
constexpr std::uint32_t kMaximumStringBytes = 16U * 1024U * 1024U;
constexpr std::size_t kMaximumValueDepth = 64;


std::uint64_t Checksum(
    const std::uint8_t* bytes,
    std::size_t size
) noexcept
{
    std::uint64_t value = 1469598103934665603ULL;
    for (std::size_t index = 0; index < size; ++index)
    {
        value ^= bytes[index];
        value *= 1099511628211ULL;
    }
    return value;
}

class Writer
{
public:
    explicit Writer(std::vector<std::uint8_t>& bytes) : bytes_(bytes) {}

    void U8(std::uint8_t value)
    {
        bytes_.push_back(value);
    }

    void U32(std::uint32_t value)
    {
        for (unsigned shift = 0; shift < 32; shift += 8)
        {
            U8(static_cast<std::uint8_t>(value >> shift));
        }
    }

    void U64(std::uint64_t value)
    {
        for (unsigned shift = 0; shift < 64; shift += 8)
        {
            U8(static_cast<std::uint8_t>(value >> shift));
        }
    }

    void I32(std::int32_t value)
    {
        U32(static_cast<std::uint32_t>(value));
    }

    void I64(std::int64_t value)
    {
        U64(static_cast<std::uint64_t>(value));
    }

    void Double(double value)
    {
        std::uint64_t bits = 0;
        static_assert(sizeof(bits) == sizeof(value));
        std::memcpy(&bits, &value, sizeof(bits));
        U64(bits);
    }

    void Boolean(bool value)
    {
        U8(value ? 1 : 0);
    }

    bool Count(std::size_t value)
    {
        if (value > kMaximumContainerItems)
        {
            return false;
        }
        U32(static_cast<std::uint32_t>(value));
        return true;
    }

    bool String(const std::string& value)
    {
        if (value.size() > kMaximumStringBytes || !Count(value.size()))
        {
            return false;
        }
        bytes_.insert(bytes_.end(), value.begin(), value.end());
        return true;
    }

    void Raw(const std::uint8_t* bytes, std::size_t size)
    {
        bytes_.insert(bytes_.end(), bytes, bytes + size);
    }

private:
    std::vector<std::uint8_t>& bytes_;
};

class Reader
{
public:
    Reader(const std::vector<std::uint8_t>& bytes, std::size_t limit)
        : bytes_(bytes), limit_(limit)
    {
    }

    bool U8(std::uint8_t& value)
    {
        if (offset_ >= limit_)
        {
            return false;
        }
        value = bytes_[offset_++];
        return true;
    }

    bool U32(std::uint32_t& value)
    {
        value = 0;
        for (unsigned shift = 0; shift < 32; shift += 8)
        {
            std::uint8_t byte = 0;
            if (!U8(byte))
            {
                return false;
            }
            value |= static_cast<std::uint32_t>(byte) << shift;
        }
        return true;
    }

    bool U64(std::uint64_t& value)
    {
        value = 0;
        for (unsigned shift = 0; shift < 64; shift += 8)
        {
            std::uint8_t byte = 0;
            if (!U8(byte))
            {
                return false;
            }
            value |= static_cast<std::uint64_t>(byte) << shift;
        }
        return true;
    }

    bool I32(std::int32_t& value)
    {
        std::uint32_t raw = 0;
        if (!U32(raw))
        {
            return false;
        }
        value = static_cast<std::int32_t>(raw);
        return true;
    }

    bool I64(std::int64_t& value)
    {
        std::uint64_t raw = 0;
        if (!U64(raw))
        {
            return false;
        }
        value = static_cast<std::int64_t>(raw);
        return true;
    }

    bool Double(double& value)
    {
        std::uint64_t bits = 0;
        if (!U64(bits))
        {
            return false;
        }
        std::memcpy(&value, &bits, sizeof(value));
        return true;
    }

    bool Boolean(bool& value)
    {
        std::uint8_t raw = 0;
        if (!U8(raw) || raw > 1)
        {
            return false;
        }
        value = raw != 0;
        return true;
    }

    bool Count(std::uint32_t& value)
    {
        return U32(value) && value <= kMaximumContainerItems;
    }

    bool String(std::string& value)
    {
        std::uint32_t size = 0;
        if (!U32(size)
            || size > kMaximumStringBytes
            || offset_ > limit_
            || size > limit_ - offset_)
        {
            return false;
        }
        value.assign(
            reinterpret_cast<const char*>(bytes_.data() + offset_),
            size
        );
        offset_ += size;
        return true;
    }

    bool Raw(const std::uint8_t* expected, std::size_t size)
    {
        if (offset_ > limit_
            || size > limit_ - offset_
            || !std::equal(
                expected,
                expected + size,
                bytes_.begin() + static_cast<std::ptrdiff_t>(offset_)))
        {
            return false;
        }
        offset_ += size;
        return true;
    }

    bool AtEnd() const noexcept
    {
        return offset_ == limit_;
    }

private:
    const std::vector<std::uint8_t>& bytes_;
    std::size_t limit_ = 0;
    std::size_t offset_ = 0;
};

template<typename Id>
void WriteId(Writer& writer, Id id)
{
    writer.U64(id.value);
}

template<typename Id>
bool ReadId(Reader& reader, Id& id)
{
    return reader.U64(id.value);
}

template<typename Slot>
void WriteSlot(Writer& writer, Slot slot)
{
    writer.U32(slot.value);
}

template<typename Slot>
bool ReadSlot(Reader& reader, Slot& slot)
{
    return reader.U32(slot.value);
}

void WriteVersion(Writer& writer, kernel::PackageVersion version)
{
    writer.U32(version.major);
    writer.U32(version.minor);
    writer.U32(version.patch);
}

bool ReadVersion(Reader& reader, kernel::PackageVersion& version)
{
    return reader.U32(version.major)
        && reader.U32(version.minor)
        && reader.U32(version.patch);
}

void WriteReference(
    Writer& writer,
    const kernel::MechanismReference& reference
)
{
    writer.U8(static_cast<std::uint8_t>(reference.kind));
    writer.U64(reference.type);
    writer.U64(reference.value);
}

bool ReadReference(
    Reader& reader,
    kernel::MechanismReference& reference
)
{
    std::uint8_t kind = 0;
    if (!reader.U8(kind)
        || kind > static_cast<std::uint8_t>(
            kernel::MechanismReferenceKind::Custom)
        || !reader.U64(reference.type)
        || !reader.U64(reference.value))
    {
        return false;
    }
    reference.kind = static_cast<kernel::MechanismReferenceKind>(kind);
    return true;
}

bool WriteValue(
    Writer& writer,
    const kernel::MechanismValue& value,
    std::size_t depth = 0
)
{
    if (depth > kMaximumValueDepth)
    {
        return false;
    }
    writer.U8(static_cast<std::uint8_t>(value.Kind()));
    switch (value.Kind())
    {
    case kernel::MechanismValueKind::Null:
        return true;
    case kernel::MechanismValueKind::Boolean:
        writer.Boolean(std::get<bool>(value.data));
        return true;
    case kernel::MechanismValueKind::Integer:
        writer.I64(std::get<std::int64_t>(value.data));
        return true;
    case kernel::MechanismValueKind::Decimal:
        writer.Double(std::get<double>(value.data));
        return true;
    case kernel::MechanismValueKind::String:
        return writer.String(std::get<std::string>(value.data));
    case kernel::MechanismValueKind::Reference:
        WriteReference(
            writer,
            std::get<kernel::MechanismReference>(value.data)
        );
        return true;
    case kernel::MechanismValueKind::List:
    {
        const auto& list = std::get<kernel::MechanismValue::List>(value.data);
        if (!writer.Count(list.size()))
        {
            return false;
        }
        for (const kernel::MechanismValue& item : list)
        {
            if (!WriteValue(writer, item, depth + 1))
            {
                return false;
            }
        }
        return true;
    }
    case kernel::MechanismValueKind::Object:
    {
        const auto& object = std::get<kernel::MechanismValue::Object>(
            value.data
        );
        if (!writer.Count(object.size()))
        {
            return false;
        }
        for (const auto& item : object)
        {
            if (!writer.String(item.first)
                || !WriteValue(writer, item.second, depth + 1))
            {
                return false;
            }
        }
        return true;
    }
    }
    return false;
}

bool ReadValue(
    Reader& reader,
    kernel::MechanismValue& value,
    std::size_t depth = 0
)
{
    if (depth > kMaximumValueDepth)
    {
        return false;
    }
    std::uint8_t kind = 0;
    if (!reader.U8(kind)
        || kind > static_cast<std::uint8_t>(
            kernel::MechanismValueKind::Object))
    {
        return false;
    }
    switch (static_cast<kernel::MechanismValueKind>(kind))
    {
    case kernel::MechanismValueKind::Null:
        value = {};
        return true;
    case kernel::MechanismValueKind::Boolean:
    {
        bool stored = false;
        if (!reader.Boolean(stored))
        {
            return false;
        }
        value = kernel::MechanismValue(stored);
        return true;
    }
    case kernel::MechanismValueKind::Integer:
    {
        std::int64_t stored = 0;
        if (!reader.I64(stored))
        {
            return false;
        }
        value = kernel::MechanismValue(stored);
        return true;
    }
    case kernel::MechanismValueKind::Decimal:
    {
        double stored = 0.0;
        if (!reader.Double(stored))
        {
            return false;
        }
        value = kernel::MechanismValue(stored);
        return true;
    }
    case kernel::MechanismValueKind::String:
    {
        std::string stored;
        if (!reader.String(stored))
        {
            return false;
        }
        value = kernel::MechanismValue(std::move(stored));
        return true;
    }
    case kernel::MechanismValueKind::Reference:
    {
        kernel::MechanismReference stored;
        if (!ReadReference(reader, stored))
        {
            return false;
        }
        value = kernel::MechanismValue(stored);
        return true;
    }
    case kernel::MechanismValueKind::List:
    {
        std::uint32_t count = 0;
        if (!reader.Count(count))
        {
            return false;
        }
        kernel::MechanismValue::List list;
        list.reserve(count);
        for (std::uint32_t index = 0; index < count; ++index)
        {
            kernel::MechanismValue item;
            if (!ReadValue(reader, item, depth + 1))
            {
                return false;
            }
            list.push_back(std::move(item));
        }
        value = kernel::MechanismValue(std::move(list));
        return true;
    }
    case kernel::MechanismValueKind::Object:
    {
        std::uint32_t count = 0;
        if (!reader.Count(count))
        {
            return false;
        }
        kernel::MechanismValue::Object object;
        for (std::uint32_t index = 0; index < count; ++index)
        {
            std::string key;
            kernel::MechanismValue item;
            if (!reader.String(key)
                || !ReadValue(reader, item, depth + 1)
                || !object.emplace(std::move(key), std::move(item)).second)
            {
                return false;
            }
        }
        value = kernel::MechanismValue(std::move(object));
        return true;
    }
    }
    return false;
}

bool WriteValues(
    Writer& writer,
    const std::vector<kernel::MechanismValue>& values
)
{
    if (!writer.Count(values.size()))
    {
        return false;
    }
    for (const kernel::MechanismValue& value : values)
    {
        if (!WriteValue(writer, value))
        {
            return false;
        }
    }
    return true;
}

bool ReadValues(
    Reader& reader,
    std::vector<kernel::MechanismValue>& values
)
{
    std::uint32_t count = 0;
    if (!reader.Count(count))
    {
        return false;
    }
    values.clear();
    values.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index)
    {
        kernel::MechanismValue value;
        if (!ReadValue(reader, value))
        {
            return false;
        }
        values.push_back(std::move(value));
    }
    return true;
}

void WriteFault(
    Writer& writer,
    const kernel::AlgorithmFaultState& fault
)
{
    writer.Boolean(fault.isolated);
    writer.U32(fault.failureCount);
    writer.U8(static_cast<std::uint8_t>(fault.code));
    writer.U8(static_cast<std::uint8_t>(fault.stage));
    writer.U64(fault.tick);
}

bool ReadFault(Reader& reader, kernel::AlgorithmFaultState& fault)
{
    std::uint8_t code = 0;
    std::uint8_t stage = 0;
    if (!reader.Boolean(fault.isolated)
        || !reader.U32(fault.failureCount)
        || !reader.U8(code)
        || !reader.U8(stage)
        || stage > static_cast<std::uint8_t>(
            kernel::AlgorithmFaultStage::Destroy)
        || !reader.U64(fault.tick))
    {
        return false;
    }
    const kernel::AlgorithmFaultCode parsedCode =
        static_cast<kernel::AlgorithmFaultCode>(code);
    if (parsedCode != kernel::AlgorithmFaultCode::None
        && !kernel::IsAuthoritativeAlgorithmFaultCode(parsedCode))
    {
        return false;
    }
    fault.code = parsedCode;
    fault.stage = static_cast<kernel::AlgorithmFaultStage>(stage);
    return true;
}

bool WriteMechanismCommand(
    Writer& writer,
    const kernel::MechanismCommand& command
)
{
    WriteId(writer, command.target);
    if (command.operation.index() > std::numeric_limits<std::uint8_t>::max())
    {
        return false;
    }
    writer.U8(static_cast<std::uint8_t>(command.operation.index()));
    return std::visit(
        [&](const auto& operation) -> bool
        {
            using Operation = std::decay_t<decltype(operation)>;
            if constexpr (std::is_same_v<
                    Operation,
                    kernel::MechanismSetFieldOperation>)
            {
                WriteSlot(writer, operation.field);
                return WriteValue(writer, operation.value);
            }
            else if constexpr (std::is_same_v<
                    Operation,
                    kernel::MechanismTransitionLifecycleOperation>)
            {
                writer.U8(static_cast<std::uint8_t>(operation.target));
            }
            else if constexpr (std::is_same_v<
                    Operation,
                    kernel::MechanismRecordAlgorithmFaultOperation>)
            {
                writer.U8(static_cast<std::uint8_t>(operation.code));
                writer.U8(static_cast<std::uint8_t>(operation.stage));
            }
            else if constexpr (std::is_same_v<
                    Operation,
                    kernel::MechanismReplaceAlgorithmStateOperation>)
            {
                if (!WriteValues(writer, operation.state)
                    || !writer.Count(operation.continuations.size()))
                {
                    return false;
                }
                for (const kernel::ControlledScriptContinuation& continuation
                    : operation.continuations)
                {
                    writer.U32(static_cast<std::uint32_t>(
                        continuation.entryPoint
                    ));
                    writer.U32(continuation.programCounter);
                }
            }
            return true;
        },
        command.operation
    );
}

bool ReadMechanismCommand(
    Reader& reader,
    kernel::MechanismCommand& command
)
{
    std::uint8_t kind = 0;
    if (!ReadId(reader, command.target) || !reader.U8(kind))
    {
        return false;
    }
    switch (kind)
    {
    case 0:
    {
        kernel::MechanismSetFieldOperation operation;
        if (!ReadSlot(reader, operation.field)
            || !ReadValue(reader, operation.value))
        {
            return false;
        }
        command.operation = std::move(operation);
        return true;
    }
    case 1:
    {
        std::uint8_t state = 0;
        if (!reader.U8(state)
            || state > static_cast<std::uint8_t>(
                kernel::MechanismLifecycleState::Failed))
        {
            return false;
        }
        command.operation = kernel::MechanismTransitionLifecycleOperation{
            static_cast<kernel::MechanismLifecycleState>(state)
        };
        return true;
    }
    case 2:
        command.operation = kernel::MechanismCompleteAlgorithmCreateOperation{};
        return true;
    case 3:
    {
        std::uint8_t code = 0;
        std::uint8_t stage = 0;
        if (!reader.U8(code)
            || !reader.U8(stage)
            || stage > static_cast<std::uint8_t>(
                kernel::AlgorithmFaultStage::Destroy))
        {
            return false;
        }
        const kernel::AlgorithmFaultCode parsedCode =
            static_cast<kernel::AlgorithmFaultCode>(code);
        if (!kernel::IsAuthoritativeAlgorithmFaultCode(parsedCode))
        {
            return false;
        }
        command.operation = kernel::MechanismRecordAlgorithmFaultOperation{
            parsedCode,
            static_cast<kernel::AlgorithmFaultStage>(stage)
        };
        return true;
    }
    case 4:
        command.operation = kernel::MechanismClearAlgorithmFaultOperation{};
        return true;
    case 5:
        command.operation = kernel::MechanismDestroyOperation{};
        return true;
    case 6:
    {
        kernel::MechanismReplaceAlgorithmStateOperation operation;
        std::uint32_t count = 0;
        if (!ReadValues(reader, operation.state) || !reader.Count(count))
        {
            return false;
        }
        operation.continuations.reserve(count);
        for (std::uint32_t index = 0; index < count; ++index)
        {
            std::uint32_t entryPoint = 0;
            kernel::ControlledScriptContinuation continuation;
            if (!reader.U32(entryPoint)
                || !reader.U32(continuation.programCounter))
            {
                return false;
            }
            continuation.entryPoint =
                static_cast<kernel::AlgorithmEntryPoint>(entryPoint);
            operation.continuations.push_back(continuation);
        }
        command.operation = std::move(operation);
        return true;
    }
    default:
        return false;
    }
}

bool WriteWorldCommand(Writer& writer, const kernel::WorldCommand& command)
{
    if (command.payload.index() > std::numeric_limits<std::uint8_t>::max())
    {
        return false;
    }
    writer.U8(static_cast<std::uint8_t>(command.payload.index()));
    return std::visit(
        [&](const auto& operation) -> bool
        {
            using Operation = std::decay_t<decltype(operation)>;
            if constexpr (std::is_same_v<
                    Operation,
                    kernel::EntityCreateCommand>)
            {
                WriteId(writer, operation.definition);
            }
            else if constexpr (std::is_same_v<
                    Operation,
                    kernel::ComponentSetFieldCommand>)
            {
                WriteId(writer, operation.owner);
                WriteId(writer, operation.component);
                WriteSlot(writer, operation.field);
                return WriteValue(writer, operation.value);
            }
            else if constexpr (std::is_same_v<
                    Operation,
                    kernel::RelationAddCommand>)
            {
                WriteId(writer, operation.type);
                WriteId(writer, operation.source);
                WriteId(writer, operation.target);
            }
            else if constexpr (std::is_same_v<
                    Operation,
                    kernel::RelationRemoveCommand>)
            {
                WriteId(writer, operation.relation);
            }
            else if constexpr (std::is_same_v<
                    Operation,
                    kernel::MechanismSpawnCommand>)
            {
                WriteId(writer, operation.spawn);
            }
            else if constexpr (std::is_same_v<
                    Operation,
                    kernel::MechanismCommand>)
            {
                return WriteMechanismCommand(writer, operation);
            }
            else if constexpr (std::is_same_v<
                    Operation,
                    kernel::ScheduledEventScheduleCommand>)
            {
                WriteId(writer, operation.type);
                WriteId(writer, operation.target);
                writer.U64(operation.dueTick);
                writer.I32(operation.priority);
                return WriteValue(writer, operation.payload);
            }
            else if constexpr (std::is_same_v<
                    Operation,
                    kernel::ScheduledEventCancelCommand>)
            {
                writer.U64(operation.sequence);
            }
            else if constexpr (std::is_same_v<
                    Operation,
                    kernel::RngStreamCreateCommand>)
            {
                WriteId(writer, operation.stream);
                writer.U64(operation.seed);
            }
            else if constexpr (std::is_same_v<
                    Operation,
                    kernel::RngStreamAdvanceCommand>)
            {
                WriteId(writer, operation.stream);
                writer.U64(operation.expectedDrawCount);
                writer.U64(operation.count);
            }
            else
            {
                WriteId(writer, operation.capability);
                WriteId(writer, operation.deliveryType);
                writer.U64(operation.dueTick);
                writer.I32(operation.priority);
                if (!WriteValue(writer, operation.payload)) return false;
                WriteId(writer, operation.targetInstance);
                writer.U32(operation.capabilityVersion);
            }
            return true;
        },
        command.payload
    );
}

bool ReadWorldCommand(Reader& reader, kernel::WorldCommand& command)
{
    std::uint8_t kind = 0;
    if (!reader.U8(kind))
    {
        return false;
    }
    switch (kind)
    {
    case 0:
    {
        kernel::EntityCreateCommand value;
        if (!ReadId(reader, value.definition)) return false;
        command.payload = value;
        return true;
    }
    case 1:
    {
        kernel::ComponentSetFieldCommand value;
        if (!ReadId(reader, value.owner)
            || !ReadId(reader, value.component)
            || !ReadSlot(reader, value.field)
            || !ReadValue(reader, value.value)) return false;
        command.payload = std::move(value);
        return true;
    }
    case 2:
    {
        kernel::RelationAddCommand value;
        if (!ReadId(reader, value.type)
            || !ReadId(reader, value.source)
            || !ReadId(reader, value.target)) return false;
        command.payload = value;
        return true;
    }
    case 3:
    {
        kernel::RelationRemoveCommand value;
        if (!ReadId(reader, value.relation)) return false;
        command.payload = value;
        return true;
    }
    case 4:
    {
        kernel::MechanismSpawnCommand value;
        if (!ReadId(reader, value.spawn)) return false;
        command.payload = value;
        return true;
    }
    case 5:
    {
        kernel::MechanismCommand value;
        if (!ReadMechanismCommand(reader, value)) return false;
        command.payload = std::move(value);
        return true;
    }
    case 6:
    {
        kernel::ScheduledEventScheduleCommand value;
        if (!ReadId(reader, value.type)
            || !ReadId(reader, value.target)
            || !reader.U64(value.dueTick)
            || !reader.I32(value.priority)
            || !ReadValue(reader, value.payload)) return false;
        command.payload = std::move(value);
        return true;
    }
    case 7:
    {
        kernel::ScheduledEventCancelCommand value;
        if (!reader.U64(value.sequence)) return false;
        command.payload = value;
        return true;
    }
    case 8:
    {
        kernel::RngStreamCreateCommand value;
        if (!ReadId(reader, value.stream)
            || !reader.U64(value.seed)) return false;
        command.payload = value;
        return true;
    }
    case 9:
    {
        kernel::RngStreamAdvanceCommand value;
        if (!ReadId(reader, value.stream)
            || !reader.U64(value.expectedDrawCount)
            || !reader.U64(value.count)) return false;
        command.payload = value;
        return true;
    }
    case 10:
    {
        kernel::InvokeCapabilityCommand value;
        if (!ReadId(reader, value.capability)
            || !ReadId(reader, value.deliveryType)
            || !reader.U64(value.dueTick)
            || !reader.I32(value.priority)
            || !ReadValue(reader, value.payload)
            || !ReadId(reader, value.targetInstance)
            || !reader.U32(value.capabilityVersion)) return false;
        command.payload = std::move(value);
        return true;
    }
    default:
        return false;
    }
}

bool WriteTransaction(
    Writer& writer,
    const kernel::WorldTransaction& transaction
)
{
    if (!writer.Count(transaction.commands.size()))
    {
        return false;
    }
    for (const kernel::WorldCommand& command : transaction.commands)
    {
        if (!WriteWorldCommand(writer, command))
        {
            return false;
        }
    }
    return true;
}

bool ReadTransaction(
    Reader& reader,
    kernel::WorldTransaction& transaction
)
{
    std::uint32_t count = 0;
    if (!reader.Count(count))
    {
        return false;
    }
    transaction.commands.clear();
    transaction.commands.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index)
    {
        kernel::WorldCommand command;
        if (!ReadWorldCommand(reader, command))
        {
            return false;
        }
        transaction.commands.push_back(std::move(command));
    }
    return true;
}

bool WriteIdentity(Writer& writer, const RuntimeSaveIdentity& identity)
{
    writer.U32(identity.formatVersion);
    WriteId(writer, identity.ruleset);
    writer.U32(identity.rulesetVersion);
    writer.U64(identity.rulesetFingerprint.high);
    writer.U64(identity.rulesetFingerprint.low);
    if (!writer.Count(identity.rulesetExtensions.size())) return false;
    for (const kernel::AppliedRulesetExtension& extension
        : identity.rulesetExtensions)
    {
        WriteId(writer, extension.id);
        if (!writer.String(extension.canonicalName)) return false;
        writer.U32(extension.version);
        writer.I32(extension.priority);
    }
    if (!writer.Count(identity.packageLock.size())) return false;
    for (const kernel::PackageLockEntry& package : identity.packageLock)
    {
        WriteId(writer, package.package);
        if (!writer.String(package.canonicalName)) return false;
        WriteVersion(writer, package.version);
        if (!writer.String(package.contentDigest)) return false;
        writer.U64(static_cast<std::uint64_t>(package.loadIndex));
        if (!writer.Count(package.dependencies.size())) return false;
        for (const kernel::LockedPackageDependency& dependency
            : package.dependencies)
        {
            WriteId(writer, dependency.package);
            WriteVersion(writer, dependency.version);
        }
        if (!writer.Count(package.providedCapabilities.size())) return false;
        for (const kernel::CapabilityProvision& capability
            : package.providedCapabilities)
        {
            WriteId(writer, capability.capability);
            if (!writer.String(capability.canonicalName)) return false;
            writer.U32(capability.version);
        }
    }
    if (!writer.Count(identity.sourceLock.size())) return false;
    for (const RuntimeSourceLockEntry& source : identity.sourceLock)
    {
        WriteId(writer, source.package);
        WriteVersion(writer, source.packageVersion);
        if (!writer.String(source.sourceLayer)
            || !writer.String(source.virtualPath)) return false;
        writer.U64(source.fingerprint);
        writer.U64(source.size);
    }
    return true;
}

bool ReadIdentity(Reader& reader, RuntimeSaveIdentity& identity)
{
    if (!reader.U32(identity.formatVersion)
        || !ReadId(reader, identity.ruleset)
        || !reader.U32(identity.rulesetVersion)
        || !reader.U64(identity.rulesetFingerprint.high)
        || !reader.U64(identity.rulesetFingerprint.low))
    {
        return false;
    }
    std::uint32_t count = 0;
    if (!reader.Count(count)) return false;
    identity.rulesetExtensions.clear();
    identity.rulesetExtensions.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index)
    {
        kernel::AppliedRulesetExtension extension;
        if (!ReadId(reader, extension.id)
            || !reader.String(extension.canonicalName)
            || !reader.U32(extension.version)
            || !reader.I32(extension.priority)) return false;
        identity.rulesetExtensions.push_back(std::move(extension));
    }
    if (!reader.Count(count)) return false;
    identity.packageLock.clear();
    identity.packageLock.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index)
    {
        kernel::PackageLockEntry package;
        std::uint64_t loadIndex = 0;
        if (!ReadId(reader, package.package)
            || !reader.String(package.canonicalName)
            || !ReadVersion(reader, package.version)
            || !reader.String(package.contentDigest)
            || !reader.U64(loadIndex)
            || loadIndex > std::numeric_limits<std::size_t>::max())
        {
            return false;
        }
        package.loadIndex = static_cast<std::size_t>(loadIndex);
        std::uint32_t dependencyCount = 0;
        if (!reader.Count(dependencyCount)) return false;
        package.dependencies.reserve(dependencyCount);
        for (std::uint32_t dependencyIndex = 0;
            dependencyIndex < dependencyCount;
            ++dependencyIndex)
        {
            kernel::LockedPackageDependency dependency;
            if (!ReadId(reader, dependency.package)
                || !ReadVersion(reader, dependency.version)) return false;
            package.dependencies.push_back(dependency);
        }
        std::uint32_t capabilityCount = 0;
        if (!reader.Count(capabilityCount)) return false;
        package.providedCapabilities.reserve(capabilityCount);
        for (std::uint32_t capabilityIndex = 0;
            capabilityIndex < capabilityCount;
            ++capabilityIndex)
        {
            kernel::CapabilityProvision capability;
            if (!ReadId(reader, capability.capability)
                || !reader.String(capability.canonicalName)
                || !reader.U32(capability.version)) return false;
            package.providedCapabilities.push_back(std::move(capability));
        }
        identity.packageLock.push_back(std::move(package));
    }
    if (!reader.Count(count)) return false;
    identity.sourceLock.clear();
    identity.sourceLock.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index)
    {
        RuntimeSourceLockEntry source;
        if (!ReadId(reader, source.package)
            || !ReadVersion(reader, source.packageVersion)
            || !reader.String(source.sourceLayer)
            || !reader.String(source.virtualPath)
            || !reader.U64(source.fingerprint)
            || !reader.U64(source.size)) return false;
        identity.sourceLock.push_back(std::move(source));
    }
    return true;
}

bool WriteImage(Writer& writer, const RuntimeSaveImage& image)
{
    if (!WriteIdentity(writer, image.identity)) return false;
    writer.U64(image.worldTick);
    writer.U64(image.worldRevision);
    if (!writer.Count(image.entities.size())) return false;
    for (const world::EntityRecord& entity : image.entities)
    {
        WriteId(writer, entity.id);
        WriteId(writer, entity.definition);
        WriteId(writer, entity.type);
    }
    if (!writer.Count(image.components.size())) return false;
    for (const world::ComponentRecord& component : image.components)
    {
        WriteId(writer, component.owner);
        WriteId(writer, component.type);
        writer.U32(component.schemaVersion);
        if (!WriteValues(writer, component.values)) return false;
    }
    if (!writer.Count(image.relations.size())) return false;
    for (const world::RelationRecord& relation : image.relations)
    {
        WriteId(writer, relation.id);
        WriteId(writer, relation.type);
        WriteId(writer, relation.source);
        WriteId(writer, relation.target);
    }
    if (!writer.Count(image.mechanisms.size())) return false;
    for (const kernel::MechanismInstance& mechanism : image.mechanisms)
    {
        WriteId(writer, mechanism.id);
        WriteId(writer, mechanism.definition);
        WriteId(writer, mechanism.type);
        writer.U32(mechanism.schemaVersion);
        WriteId(writer, mechanism.algorithm);
        writer.U32(mechanism.algorithmVersion);
        writer.U64(mechanism.creationOrdinal);
        writer.U8(static_cast<std::uint8_t>(mechanism.lifecycle));
        if (!WriteValues(writer, mechanism.values)
            || !writer.Count(mechanism.roles.size())) return false;
        for (const auto& role : mechanism.roles)
        {
            if (!writer.Count(role.size())) return false;
            for (const kernel::MechanismReference& reference : role)
            {
                WriteReference(writer, reference);
            }
        }
        if (!WriteValues(writer, mechanism.algorithmState)) return false;
        if (image.identity.formatVersion >= 4)
        {
            if (!writer.Count(mechanism.algorithmContinuations.size()))
                return false;
            for (const kernel::ControlledScriptContinuation& continuation
                : mechanism.algorithmContinuations)
            {
                writer.U32(static_cast<std::uint32_t>(
                    continuation.entryPoint
                ));
                writer.U32(continuation.programCounter);
            }
        }
        writer.Boolean(mechanism.algorithmInitialized);
        WriteFault(writer, mechanism.algorithmFault);
        writer.U64(mechanism.createdTick);
        writer.U64(mechanism.updatedTick);
    }
    if (!writer.Count(image.nextMechanismOrdinalByDefinition.size()))
        return false;
    for (const auto& next : image.nextMechanismOrdinalByDefinition)
    {
        WriteId(writer, next.first);
        writer.U64(next.second);
    }
    if (!writer.Count(image.scheduledInbox.size())) return false;
    for (const kernel::ScheduledAlgorithmEvent& event : image.scheduledInbox)
    {
        writer.U64(event.sequence);
        WriteId(writer, event.type);
        WriteId(writer, event.target);
        writer.U64(event.dueTick);
        writer.I32(event.priority);
        if (!WriteValue(writer, event.payload)) return false;
    }
    writer.U64(image.nextScheduledEventSequence);
    if (!writer.Count(image.rngStreams.size())) return false;
    for (const kernel::DeterministicRngStream& stream : image.rngStreams)
    {
        WriteId(writer, stream.id);
        writer.U64(stream.seed);
        writer.U64(stream.drawCount);
    }
    if (!writer.Count(image.commandQueue.size())) return false;
    for (const kernel::QueuedWorldTransaction& queued : image.commandQueue)
    {
        writer.U64(queued.sequence);
        writer.U64(queued.notBeforeTick);
        writer.I32(queued.priority);
        if (!WriteTransaction(writer, queued.transaction)) return false;
    }
    writer.U64(image.nextCommandSequence);
    writer.U64(image.nextFactSequence);
    return true;
}

bool ReadImage(Reader& reader, RuntimeSaveImage& image)
{
    if (!ReadIdentity(reader, image.identity)
        || !reader.U64(image.worldTick)
        || !reader.U64(image.worldRevision)) return false;
    std::uint32_t count = 0;
    if (!reader.Count(count)) return false;
    image.entities.clear();
    image.entities.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index)
    {
        world::EntityRecord entity;
        if (!ReadId(reader, entity.id)
            || !ReadId(reader, entity.definition)
            || !ReadId(reader, entity.type)) return false;
        image.entities.push_back(entity);
    }
    if (!reader.Count(count)) return false;
    image.components.clear();
    image.components.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index)
    {
        world::ComponentRecord component;
        if (!ReadId(reader, component.owner)
            || !ReadId(reader, component.type)
            || !reader.U32(component.schemaVersion)
            || !ReadValues(reader, component.values)) return false;
        image.components.push_back(std::move(component));
    }
    if (!reader.Count(count)) return false;
    image.relations.clear();
    image.relations.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index)
    {
        world::RelationRecord relation;
        if (!ReadId(reader, relation.id)
            || !ReadId(reader, relation.type)
            || !ReadId(reader, relation.source)
            || !ReadId(reader, relation.target)) return false;
        image.relations.push_back(relation);
    }
    if (!reader.Count(count)) return false;
    image.mechanisms.clear();
    image.mechanisms.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index)
    {
        kernel::MechanismInstance mechanism;
        std::uint8_t lifecycle = 0;
        if (!ReadId(reader, mechanism.id)
            || !ReadId(reader, mechanism.definition)
            || !ReadId(reader, mechanism.type)
            || !reader.U32(mechanism.schemaVersion)
            || !ReadId(reader, mechanism.algorithm)
            || !reader.U32(mechanism.algorithmVersion)
            || !reader.U64(mechanism.creationOrdinal)
            || !reader.U8(lifecycle)
            || lifecycle > static_cast<std::uint8_t>(
                kernel::MechanismLifecycleState::Failed)
            || !ReadValues(reader, mechanism.values)) return false;
        mechanism.lifecycle = static_cast<kernel::MechanismLifecycleState>(
            lifecycle
        );
        std::uint32_t roleCount = 0;
        if (!reader.Count(roleCount)) return false;
        mechanism.roles.resize(roleCount);
        for (auto& role : mechanism.roles)
        {
            std::uint32_t referenceCount = 0;
            if (!reader.Count(referenceCount)) return false;
            role.reserve(referenceCount);
            for (std::uint32_t referenceIndex = 0;
                referenceIndex < referenceCount;
                ++referenceIndex)
            {
                kernel::MechanismReference reference;
                if (!ReadReference(reader, reference)) return false;
                role.push_back(reference);
            }
        }
        if (!ReadValues(reader, mechanism.algorithmState)) return false;
        if (image.identity.formatVersion >= 4)
        {
            std::uint32_t continuationCount = 0;
            if (!reader.Count(continuationCount)) return false;
            mechanism.algorithmContinuations.reserve(continuationCount);
            for (std::uint32_t continuationIndex = 0;
                continuationIndex < continuationCount;
                ++continuationIndex)
            {
                std::uint32_t entryPoint = 0;
                kernel::ControlledScriptContinuation continuation;
                if (!reader.U32(entryPoint)
                    || !reader.U32(continuation.programCounter)) return false;
                continuation.entryPoint =
                    static_cast<kernel::AlgorithmEntryPoint>(entryPoint);
                mechanism.algorithmContinuations.push_back(continuation);
            }
        }
        if (!reader.Boolean(mechanism.algorithmInitialized)
            || !ReadFault(reader, mechanism.algorithmFault)
            || !reader.U64(mechanism.createdTick)
            || !reader.U64(mechanism.updatedTick)) return false;
        image.mechanisms.push_back(std::move(mechanism));
    }
    if (!reader.Count(count)) return false;
    image.nextMechanismOrdinalByDefinition.clear();
    for (std::uint32_t index = 0; index < count; ++index)
    {
        kernel::MechanismDefinitionId definition;
        std::uint64_t ordinal = 0;
        if (!ReadId(reader, definition)
            || !reader.U64(ordinal)
            || !image.nextMechanismOrdinalByDefinition.emplace(
                definition,
                ordinal).second) return false;
    }
    if (!reader.Count(count)) return false;
    image.scheduledInbox.clear();
    image.scheduledInbox.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index)
    {
        kernel::ScheduledAlgorithmEvent event;
        if (!reader.U64(event.sequence)
            || !ReadId(reader, event.type)
            || !ReadId(reader, event.target)
            || !reader.U64(event.dueTick)
            || !reader.I32(event.priority)
            || !ReadValue(reader, event.payload)) return false;
        image.scheduledInbox.push_back(std::move(event));
    }
    if (!reader.U64(image.nextScheduledEventSequence)
        || !reader.Count(count)) return false;
    image.rngStreams.clear();
    image.rngStreams.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index)
    {
        kernel::DeterministicRngStream stream;
        if (!ReadId(reader, stream.id)
            || !reader.U64(stream.seed)
            || !reader.U64(stream.drawCount)) return false;
        image.rngStreams.push_back(stream);
    }
    if (!reader.Count(count)) return false;
    image.commandQueue.clear();
    image.commandQueue.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index)
    {
        kernel::QueuedWorldTransaction queued;
        if (!reader.U64(queued.sequence)
            || !reader.U64(queued.notBeforeTick)
            || !reader.I32(queued.priority)
            || !ReadTransaction(reader, queued.transaction)) return false;
        image.commandQueue.push_back(std::move(queued));
    }
    return reader.U64(image.nextCommandSequence)
        && reader.U64(image.nextFactSequence);
}

bool WriteOptionalValue(
    Writer& writer,
    const std::optional<kernel::MechanismValue>& value
)
{
    writer.Boolean(value.has_value());
    return !value || WriteValue(writer, *value);
}

bool WriteWorldEventPayload(
    Writer& writer,
    const kernel::WorldEventPayload& payload
)
{
    writer.U8(static_cast<std::uint8_t>(payload.index()));
    return std::visit(
        [&](const auto& value) -> bool
        {
            using Value = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Value,
                    kernel::WorldTransactionCommittedEvent>)
            {
                writer.U64(value.changedInstances);
                writer.U64(value.changedObjects);
            }
            else if constexpr (std::is_same_v<Value,
                    kernel::WorldTransactionRejectedEvent>)
            {
                writer.U32(static_cast<std::uint32_t>(value.status));
                writer.U32(static_cast<std::uint32_t>(value.mechanismStatus));
                writer.U64(value.commandIndex);
                writer.U64(value.subject);
                WriteId(writer, value.target);
            }
            else if constexpr (std::is_same_v<Value,
                    kernel::EntityCreatedChange>)
            {
                WriteId(writer, value.entity);
                WriteId(writer, value.definition);
            }
            else if constexpr (std::is_same_v<Value,
                    kernel::ComponentAttachedChange>)
            {
                WriteId(writer, value.owner);
                WriteId(writer, value.component);
            }
            else if constexpr (std::is_same_v<Value,
                    kernel::ComponentFieldChange>)
            {
                WriteId(writer, value.owner);
                WriteId(writer, value.component);
                WriteSlot(writer, value.field);
                return WriteValue(writer, value.previousValue)
                    && WriteValue(writer, value.currentValue);
            }
            else if constexpr (std::is_same_v<Value,
                    kernel::RelationAddedChange>
                || std::is_same_v<Value,
                    kernel::RelationRemovedChange>)
            {
                WriteId(writer, value.relation);
                WriteId(writer, value.type);
                WriteId(writer, value.source);
                WriteId(writer, value.target);
            }
            else if constexpr (std::is_same_v<Value,
                    kernel::MechanismSpawnedChange>)
            {
                WriteId(writer, value.instance);
                WriteId(writer, value.spawn);
            }
            else if constexpr (std::is_same_v<Value,
                    kernel::MechanismFieldChange>)
            {
                WriteId(writer, value.target);
                WriteSlot(writer, value.field);
                return WriteOptionalValue(writer, value.previousValue)
                    && WriteValue(writer, value.currentValue);
            }
            else if constexpr (std::is_same_v<Value,
                    kernel::MechanismLifecycleChange>)
            {
                WriteId(writer, value.target);
                writer.U8(static_cast<std::uint8_t>(value.previousState));
                writer.U8(static_cast<std::uint8_t>(value.currentState));
            }
            else if constexpr (std::is_same_v<Value,
                    kernel::MechanismAlgorithmInitializedChange>)
            {
                WriteId(writer, value.target);
            }
            else if constexpr (std::is_same_v<Value,
                    kernel::MechanismAlgorithmFaultChange>)
            {
                WriteId(writer, value.target);
                WriteFault(writer, value.previousState);
                WriteFault(writer, value.currentState);
            }
            else if constexpr (std::is_same_v<Value,
                    kernel::MechanismDestroyedChange>)
            {
                WriteId(writer, value.target);
                WriteId(writer, value.definition);
                WriteId(writer, value.type);
            }
            else if constexpr (std::is_same_v<Value,
                    kernel::ScheduledEventAddedChange>
                || std::is_same_v<Value,
                    kernel::ScheduledEventCancelledChange>)
            {
                writer.U64(value.event.sequence);
                WriteId(writer, value.event.type);
                WriteId(writer, value.event.target);
                writer.U64(value.event.dueTick);
                writer.I32(value.event.priority);
                return WriteValue(writer, value.event.payload);
            }
            else if constexpr (std::is_same_v<Value,
                    kernel::RngStreamCreatedChange>)
            {
                WriteId(writer, value.stream);
                writer.U64(value.seed);
            }
            else if constexpr (std::is_same_v<Value,
                    kernel::MechanismAlgorithmStateChange>)
            {
                WriteId(writer, value.target);
                if (!WriteValues(writer, value.previousState)
                    || !WriteValues(writer, value.currentState)
                    || !writer.Count(value.previousContinuations.size()))
                {
                    return false;
                }
                for (const auto& continuation
                    : value.previousContinuations)
                {
                    writer.U32(static_cast<std::uint32_t>(
                        continuation.entryPoint
                    ));
                    writer.U32(continuation.programCounter);
                }
                if (!writer.Count(value.currentContinuations.size()))
                {
                    return false;
                }
                for (const auto& continuation
                    : value.currentContinuations)
                {
                    writer.U32(static_cast<std::uint32_t>(
                        continuation.entryPoint
                    ));
                    writer.U32(continuation.programCounter);
                }
            }
            else
            {
                WriteId(writer, value.stream);
                writer.U64(value.previousDrawCount);
                writer.U64(value.currentDrawCount);
            }
            return true;
        },
        payload
    );
}

RuntimeSaveCodecReport CodecFailure(
    RuntimeSaveCodecStatus status,
    std::string message
)
{
    return {status, std::move(message)};
}

}

RuntimeSaveCodecReport::operator bool() const noexcept
{
    return status == RuntimeSaveCodecStatus::Completed;
}

std::uint64_t StableRuntimeChecksum(
    const std::vector<std::uint8_t>& bytes
) noexcept
{
    return Checksum(bytes.data(), bytes.size());
}

RuntimeSaveCodecReport RuntimeSaveCodec::Encode(
    const RuntimeSaveImage& image,
    std::vector<std::uint8_t>& output
) const
{
    std::vector<std::uint8_t> bytes;
    Writer writer(bytes);
    writer.Raw(kSaveMagic, sizeof(kSaveMagic));
    if (!WriteImage(writer, image))
    {
        return CodecFailure(
            RuntimeSaveCodecStatus::InvalidImage,
            "Runtime Save Image exceeds codec limits"
        );
    }
    writer.U64(Checksum(bytes.data(), bytes.size()));
    output = std::move(bytes);
    return {};
}

RuntimeSaveCodecReport RuntimeSaveCodec::Decode(
    const std::vector<std::uint8_t>& bytes,
    RuntimeSaveImage& output
) const
{
    if (bytes.size() < sizeof(kSaveMagic) + sizeof(std::uint64_t))
    {
        return CodecFailure(
            RuntimeSaveCodecStatus::Truncated,
            "Runtime save is shorter than its envelope"
        );
    }
    const std::size_t payloadSize = bytes.size() - sizeof(std::uint64_t);
    std::uint64_t storedChecksum = 0;
    for (unsigned shift = 0; shift < 64; shift += 8)
    {
        storedChecksum |= static_cast<std::uint64_t>(
            bytes[payloadSize + shift / 8]
        ) << shift;
    }
    if (storedChecksum != Checksum(bytes.data(), payloadSize))
    {
        return CodecFailure(
            RuntimeSaveCodecStatus::ChecksumMismatch,
            "Runtime save checksum does not match its payload"
        );
    }
    Reader reader(bytes, payloadSize);
    if (!reader.Raw(kSaveMagic, sizeof(kSaveMagic)))
    {
        return CodecFailure(
            RuntimeSaveCodecStatus::InvalidMagic,
            "Runtime save magic is invalid"
        );
    }
    RuntimeSaveImage image;
    if (!ReadImage(reader, image))
    {
        return CodecFailure(
            RuntimeSaveCodecStatus::InvalidValue,
            "Runtime save contains a truncated or invalid field"
        );
    }
    if (!reader.AtEnd())
    {
        return CodecFailure(
            RuntimeSaveCodecStatus::TrailingBytes,
            "Runtime save contains unrecognized trailing fields"
        );
    }
    output = std::move(image);
    return {};
}

RuntimeSaveCodecReport RuntimeSaveCodec::EncodeFactStream(
    const std::vector<kernel::WorldEvent>& events,
    std::vector<std::uint8_t>& output
) const
{
    std::vector<std::uint8_t> bytes;
    Writer writer(bytes);
    writer.Raw(kFactMagic, sizeof(kFactMagic));
    if (!writer.Count(events.size()))
    {
        return CodecFailure(
            RuntimeSaveCodecStatus::LimitExceeded,
            "Fact Stream exceeds codec limits"
        );
    }
    for (const kernel::WorldEvent& event : events)
    {
        writer.U64(event.sequence);
        writer.U64(event.tick);
        writer.U64(event.transactionSequence);
        if (!WriteWorldEventPayload(writer, event.payload))
        {
            return CodecFailure(
                RuntimeSaveCodecStatus::InvalidValue,
                "Fact Stream contains an unsupported payload"
            );
        }
    }
    output = std::move(bytes);
    return {};
}

}
