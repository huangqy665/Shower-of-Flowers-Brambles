#include "source_buffer.hpp"

#include <algorithm>
#include <utility>

namespace dillen::parser {

bool SourceSpan::IsValid() const noexcept
{
    return begin.source != kInvalidSourceId
        && begin.source == end.source
        && begin.offset <= end.offset;
}

std::size_t SourceSpan::Length() const noexcept
{
    return IsValid() ? end.offset - begin.offset : 0;
}

SourceBuffer::SourceBuffer(
    SourceId id,
    std::string virtualPath,
    std::string physicalPath,
    std::string bytes,
    SourceEncoding encoding
)
    : id_(id),
      virtualPath_(std::move(virtualPath)),
      physicalPath_(std::move(physicalPath)),
      bytes_(std::move(bytes)),
      encoding_(encoding == SourceEncoding::Unknown
          ? DetectSourceEncoding(bytes_)
          : encoding)
{
}

SourceId SourceBuffer::Id() const noexcept
{
    return id_;
}

std::string_view SourceBuffer::VirtualPath() const noexcept
{
    return virtualPath_;
}

std::string_view SourceBuffer::PhysicalPath() const noexcept
{
    return physicalPath_;
}

std::string_view SourceBuffer::Bytes() const noexcept
{
    return bytes_;
}

SourceEncoding SourceBuffer::Encoding() const noexcept
{
    return encoding_;
}

std::string_view SourceBuffer::Slice(
    const SourceSpan& span
) const noexcept
{
    if (!span.IsValid()
        || span.begin.source != id_
        || span.end.offset > bytes_.size())
    {
        return {};
    }
    return std::string_view(bytes_).substr(
        span.begin.offset,
        span.Length()
    );
}

SourceEncoding DetectSourceEncoding(
    std::string_view bytes
) noexcept
{
    if (bytes.size() >= 3
        && static_cast<unsigned char>(bytes[0]) == 0xEF
        && static_cast<unsigned char>(bytes[1]) == 0xBB
        && static_cast<unsigned char>(bytes[2]) == 0xBF)
    {
        return SourceEncoding::Utf8Bom;
    }
    if (bytes.size() >= 2
        && static_cast<unsigned char>(bytes[0]) == 0xFF
        && static_cast<unsigned char>(bytes[1]) == 0xFE)
    {
        return SourceEncoding::Utf16LittleEndian;
    }
    if (bytes.size() >= 2
        && static_cast<unsigned char>(bytes[0]) == 0xFE
        && static_cast<unsigned char>(bytes[1]) == 0xFF)
    {
        return SourceEncoding::Utf16BigEndian;
    }
    if (std::find(bytes.begin(), bytes.end(), '\0') != bytes.end())
    {
        return SourceEncoding::Binary;
    }

    bool validUtf8 = true;
    bool hasMultibyte = false;
    for (std::size_t index = 0; index < bytes.size();)
    {
        const auto first = static_cast<unsigned char>(bytes[index]);
        if (first < 0x80)
        {
            ++index;
            continue;
        }

        hasMultibyte = true;
        std::size_t continuationCount = 0;
        if ((first & 0xE0) == 0xC0)
        {
            continuationCount = 1;
        }
        else if ((first & 0xF0) == 0xE0)
        {
            continuationCount = 2;
        }
        else if ((first & 0xF8) == 0xF0)
        {
            continuationCount = 3;
        }
        else
        {
            validUtf8 = false;
            break;
        }

        if (index + continuationCount >= bytes.size())
        {
            validUtf8 = false;
            break;
        }
        for (std::size_t offset = 1;
            offset <= continuationCount;
            ++offset)
        {
            const auto continuation = static_cast<unsigned char>(
                bytes[index + offset]
            );
            if ((continuation & 0xC0) != 0x80)
            {
                validUtf8 = false;
                break;
            }
        }
        if (!validUtf8)
        {
            break;
        }
        index += continuationCount + 1;
    }

    if (validUtf8 && hasMultibyte)
    {
        return SourceEncoding::Utf8;
    }
    return SourceEncoding::LegacySingleByte;
}

}
