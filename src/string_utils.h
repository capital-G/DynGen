#pragma once

#include <string_view>

/*! @brief check if the given character is considered whitespace.
 *  This matches the behavior of std::isspace under the default locale.
 */
inline bool isWhitespace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

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
inline std::string_view trimLeft(std::string_view sv) {
    if (sv.empty()) {
        return sv;
    }

    size_t pos = 0;
    for (; pos < sv.size(); ++pos) {
        if (!isWhitespace(sv[pos])) {
            break;
        }
    }

    return sv.substr(pos);
}

/*! @brief removes all trailing whitespace from the given string_view */
inline std::string_view trimRight(std::string_view sv) {
    if (sv.empty()) {
        return sv;
    }

    ptrdiff_t pos = sv.size() - 1;
    for (; pos >= 0; --pos) {
        if (!isWhitespace(sv[pos])) {
            break;
        }
    }

    if (pos >= 0) {
        return sv.substr(0, pos + 1);
    } else {
        return {};
    }
}

/*! @brief removes all leading and trailing whitespace from the given string_view */
inline std::string_view trim(std::string_view sv) {
    if (sv.empty()) {
        return sv;
    }

    ptrdiff_t lpos = 0;
    for (; lpos < sv.size(); ++lpos) {
        if (!isWhitespace(sv[lpos])) {
            break;
        }
    }

    ptrdiff_t rpos = sv.size() - 1;
    for (; rpos >= 0; --rpos) {
        if (!isWhitespace(sv[rpos])) {
            break;
        }
    }

    if (rpos >= lpos) {
        return sv.substr(lpos, rpos - lpos + 1);
    } else {
        return {};
    }
}
