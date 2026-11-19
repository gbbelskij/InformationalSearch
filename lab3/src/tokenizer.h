#pragma once

#include <clocale>
#include <cstddef>
#include <cstdint>
#include <cwctype>
#include <string>

namespace tok {

inline bool init() {
    return std::setlocale(LC_CTYPE, "C.UTF-8") != nullptr;
}

inline size_t decode(const unsigned char* s, size_t n, uint32_t& cp) {
    unsigned char c = s[0];
    if (c < 0x80) {
        cp = c;
        return 1;
    }
    size_t len;
    uint32_t min;
    if ((c & 0xE0) == 0xC0) {
        len = 2;
        cp = c & 0x1F;
        min = 0x80;
    } else if ((c & 0xF0) == 0xE0) {
        len = 3;
        cp = c & 0x0F;
        min = 0x800;
    } else if ((c & 0xF8) == 0xF0) {
        len = 4;
        cp = c & 0x07;
        min = 0x10000;
    } else {
        cp = 0xFFFD;
        return 1;
    }
    if (len > n) {
        cp = 0xFFFD;
        return 1;
    }
    for (size_t i = 1; i < len; ++i) {
        if ((s[i] & 0xC0) != 0x80) {
            cp = 0xFFFD;
            return 1;
        }
        cp = (cp << 6) | (s[i] & 0x3F);
    }
    if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        cp = 0xFFFD;
        return 1;
    }
    return len;
}

inline void encode(uint32_t cp, std::string& out) {
    if (cp < 0x80) {
        out += char(cp);
    } else if (cp < 0x800) {
        out += char(0xC0 | (cp >> 6));
        out += char(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += char(0xE0 | (cp >> 12));
        out += char(0x80 | ((cp >> 6) & 0x3F));
        out += char(0x80 | (cp & 0x3F));
    } else {
        out += char(0xF0 | (cp >> 18));
        out += char(0x80 | ((cp >> 12) & 0x3F));
        out += char(0x80 | ((cp >> 6) & 0x3F));
        out += char(0x80 | (cp & 0x3F));
    }
}

template <class Emit>
void tokenize(const char* data, size_t n, Emit&& emit) {
    const unsigned char* s = reinterpret_cast<const unsigned char*>(data);
    std::string token;
    size_t chars = 0;
    size_t i = 0;
    while (i < n) {
        uint32_t cp;
        i += decode(s + i, n - i, cp);
        bool word;
        if (cp < 0x80) {
            if (cp >= 'A' && cp <= 'Z') {
                cp += 'a' - 'A';
                word = true;
            } else {
                word = (cp >= 'a' && cp <= 'z') || (cp >= '0' && cp <= '9');
            }
        } else {
            word = std::iswalnum(cp);
            if (word) cp = std::towlower(cp);
        }
        if (word) {
            encode(cp, token);
            ++chars;
        } else if (chars) {
            emit(token, chars);
            token.clear();
            chars = 0;
        }
    }
    if (chars) emit(token, chars);
}

}
