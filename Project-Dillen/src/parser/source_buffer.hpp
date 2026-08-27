#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace dillen::parser {

using SourceId = std::uint32_t;

constexpr SourceId kInvalidSourceId = 0;

enum class SourceEncoding
{
    Unknown,
    Utf8,
    Utf8Bom,
    Utf16LittleEndian,
    Utf16BigEndian,
    LegacySingleByte,
    Binary
};

struct SourceLocation
{
    SourceId source = kInvalidSourceId;
    std::size_t offset = 0;
    std::uint32_t line = 1;
    std::uint32_t column = 1;
};

struct SourceSpan
{
    SourceLocation begin;
    SourceLocation end;

    bool IsValid() const noexcept;
    std::size_t Length() const noexcept;
};

class SourceBuffer
{
public:
    SourceBuffer() = default;
    SourceBuffer(
        SourceId id,
        std::string virtualPath,
        std::string physicalPath,
        std::string bytes,
        SourceEncoding encoding = SourceEncoding::Unknown
    );

    SourceId Id() const noexcept;
    std::string_view VirtualPath() const noexcept;
    std::string_view PhysicalPath() const noexcept;
    std::string_view Bytes() const noexcept;
    SourceEncoding Encoding() const noexcept;
    std::string_view Slice(const SourceSpan& span) const noexcept;

private:
    SourceId id_ = kInvalidSourceId;
    std::string virtualPath_;
    std::string physicalPath_;
    std::string bytes_;
    SourceEncoding encoding_ = SourceEncoding::Unknown;
};

SourceEncoding DetectSourceEncoding(std::string_view bytes) noexcept;

}
