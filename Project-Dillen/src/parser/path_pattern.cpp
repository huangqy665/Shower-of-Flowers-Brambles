#include "path_pattern.hpp"

#include <algorithm>

namespace dillen::parser {

namespace {

bool MatchImpl(
    const char* pattern,
    const char* patternEnd,
    const char* text,
    const char* textEnd
)
{
    while (pattern != patternEnd)
    {
        if (*pattern == '*')
        {
            ++pattern;
            if (pattern != patternEnd && *pattern == '*')
            {
                while (pattern != patternEnd && *pattern == '*')
                {
                    ++pattern;
                }
                if (pattern != patternEnd && *pattern == '/')
                {
                    const char* afterSlash = pattern + 1;
                    if (MatchImpl(afterSlash, patternEnd, text, textEnd))
                    {
                        return true;
                    }
                    for (const char* cursor = text;
                        cursor != textEnd;
                        ++cursor)
                    {
                        if (*cursor == '/'
                            && MatchImpl(
                                afterSlash,
                                patternEnd,
                                cursor + 1,
                                textEnd))
                        {
                            return true;
                        }
                    }
                    return false;
                }
                if (pattern == patternEnd)
                {
                    return true;
                }
                for (const char* cursor = text;; ++cursor)
                {
                    if (MatchImpl(pattern, patternEnd, cursor, textEnd))
                    {
                        return true;
                    }
                    if (cursor == textEnd)
                    {
                        break;
                    }
                }
                return false;
            }

            if (pattern == patternEnd)
            {
                return std::find(text, textEnd, '/') == textEnd;
            }
            for (const char* cursor = text;; ++cursor)
            {
                if (MatchImpl(pattern, patternEnd, cursor, textEnd))
                {
                    return true;
                }
                if (cursor == textEnd || *cursor == '/')
                {
                    break;
                }
            }
            return false;
        }

        if (*pattern == '?')
        {
            if (text == textEnd || *text == '/')
            {
                return false;
            }
            ++pattern;
            ++text;
            continue;
        }

        if (text == textEnd || *pattern != *text)
        {
            return false;
        }
        ++pattern;
        ++text;
    }
    return text == textEnd;
}

}

bool MatchPathPattern(
    std::string_view pattern,
    std::string_view path
) noexcept
{
    return MatchImpl(
        pattern.data(),
        pattern.data() + pattern.size(),
        path.data(),
        path.data() + path.size()
    );
}

}
