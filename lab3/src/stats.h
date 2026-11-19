#pragma once

#include "tokenizer.h"

#include <chrono>
#include <cstdio>
#include <string>

namespace tok {

using Clock = std::chrono::steady_clock;

inline double seconds(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double>(b - a).count();
}

inline void read_stream(FILE* f, std::string& out) {
    out.clear();
    char buf[1 << 16];
    size_t k;
    while ((k = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, k);
}

struct Stats {
    size_t files = 0, bytes = 0, tokens = 0, chars = 0;
    double read_s = 0, tok_s = 0;

    double avg_len() const { return tokens ? double(chars) / tokens : 0.0; }
};

inline void count_text(const char* data, size_t n, Stats& st) {
    tokenize(data, n, [&](const std::string&, size_t c) {
        ++st.tokens;
        st.chars += c;
    });
    st.bytes += n;
    ++st.files;
}

inline bool count_file(const char* path, Stats& st, std::string& buf) {
    Clock::time_point a = Clock::now();
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    read_stream(f, buf);
    std::fclose(f);
    Clock::time_point b = Clock::now();
    count_text(buf.data(), buf.size(), st);
    Clock::time_point e = Clock::now();
    st.read_s += seconds(a, b);
    st.tok_s += seconds(b, e);
    return true;
}

}
