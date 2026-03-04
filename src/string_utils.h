#pragma once

#include <cctype>
#include <string_view>

/*! @brief check if the given character is considered whitespace.
 *  This matches the behavior of std::isspace under the default locale.
 */
inline bool isWhitespace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

/*! @brief check if the given string only consists of whitespace characters */
inline bool isWhitespace(std::string_view string) {
    for (auto& c : string) {
        if (!isWhitespace(c)) {
            return false;
        }
    }
    return true;
}

/*! @brief check if the given character is alphanumeric. Faster alternative to std::isalnum() */
inline bool isAlphaNumeric(char c) {
    // 0-9 || A-Z || a-z
    return (c >= 48 && c <= 57) || (c >= 65 && c <= 90) || (c >= 97 && c <= 122);
}

/*! @brief check if the given string only consists of alphanumeric characters */
inline bool isAlphaNumeric(std::string_view string) {
    for (auto& c : string) {
        if (!std::isalnum(c)) {
            return false;
        }
    }
    return true;
}

/*! @brief iterate over all lines in the given std::string_view.
 *  'func' receives the line as a std::string_view, followed by the position of the line in the string.
 *  @note The delimiting character is *not* included in the result!
 */
template <typename Func> void forEachLine(std::string_view string, Func&& func, char delim = '\n') {
    size_t pos = 0;
    while (pos < string.size()) {
        size_t next = string.find(delim, pos);
        if (next != std::string_view::npos) {
            func(string.substr(pos, next - pos), pos);
            pos = next + 1;
        } else {
            // till the end of the string
            func(string.substr(pos), pos);
            break;
        }
    }
}

/*! @brief removes all leading whitespace from the given string_view */
inline std::string_view trimLeft(std::string_view string) {
    if (string.empty()) {
        return string;
    }

    size_t pos = 0;
    for (; pos < string.size(); ++pos) {
        if (!isWhitespace(string[pos])) {
            break;
        }
    }

    return string.substr(pos);
}

/*! @brief removes all trailing whitespace from the given string_view */
inline std::string_view trimRight(std::string_view string) {
    if (string.empty()) {
        return string;
    }

    ptrdiff_t pos = string.size() - 1;
    for (; pos >= 0; --pos) {
        if (!isWhitespace(string[pos])) {
            break;
        }
    }

    if (pos >= 0) {
        return string.substr(0, pos + 1);
    } else {
        return {};
    }
}

/*! @brief removes all leading and trailing whitespace from the given string_view */
inline std::string_view trim(std::string_view string) {
    if (string.empty()) {
        return string;
    }

    ptrdiff_t lpos = 0;
    for (; lpos < string.size(); ++lpos) {
        if (!isWhitespace(string[lpos])) {
            break;
        }
    }

    ptrdiff_t rpos = string.size() - 1;
    for (; rpos >= 0; --rpos) {
        if (!isWhitespace(string[rpos])) {
            break;
        }
    }

    if (rpos >= lpos) {
        return string.substr(lpos, rpos - lpos + 1);
    } else {
        return {};
    }
}
