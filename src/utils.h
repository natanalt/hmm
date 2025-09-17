#pragma once

#include <cstring>
#include <cctype>

static inline bool StrEndsWith(const char* string, const char* ending, bool caseSensitive = true) {
    const auto srcLength = std::strlen(string);
    const auto endLength = std::strlen(ending);

    if (endLength > srcLength)
        return false;
    
    const auto srcStart = srcLength - endLength;
    for (int i = srcStart; i < srcLength; i++) {
        const char a = caseSensitive ? string[i] : std::tolower(string[i]);
        const char b = caseSensitive ? ending[i - srcStart] : std::tolower(ending[i - srcStart]);

        if (a != b)
            return false;
    }

    return true;
}
