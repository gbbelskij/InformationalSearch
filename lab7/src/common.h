#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

inline void* xrealloc(void* p, size_t n) {
    void* q = std::realloc(p, n);
    if (!q && n) {
        std::fprintf(stderr, "out of memory\n");
        std::exit(3);
    }
    return q;
}

template <class T>
struct Vec {
    T* data = nullptr;
    size_t size = 0;
    size_t cap = 0;

    Vec() = default;
    Vec(const Vec&) = delete;
    Vec& operator=(const Vec&) = delete;
    ~Vec() { std::free(data); }

    void reserve(size_t n) {
        if (n <= cap) return;
        size_t c = cap ? cap : 16;
        while (c < n) c *= 2;
        data = static_cast<T*>(xrealloc(data, c * sizeof(T)));
        cap = c;
    }
    void push(const T& v) {
        if (size == cap) reserve(size + 1);
        data[size++] = v;
    }
    void append(const T* v, size_t n) {
        reserve(size + n);
        std::memcpy(data + size, v, n * sizeof(T));
        size += n;
    }
    T& operator[](size_t i) { return data[i]; }
    const T& operator[](size_t i) const { return data[i]; }
};

inline double now() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

inline bool read_file(const char* path, Vec<char>& out) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    out.size = 0;
    char buf[1 << 16];
    size_t k;
    while ((k = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, k);
    std::fclose(f);
    return true;
}

inline int compare_bytes(const char* a, size_t na, const char* b, size_t nb) {
    int c = std::memcmp(a, b, na < nb ? na : nb);
    if (c) return c;
    return na < nb ? -1 : (na > nb ? 1 : 0);
}

inline uint16_t get_u16(const unsigned char* p) {
    return uint16_t(p[0] | (p[1] << 8));
}

inline uint32_t get_u32(const unsigned char* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

inline float get_f32(const unsigned char* p) {
    uint32_t v = get_u32(p);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}

inline uint64_t get_u64(const unsigned char* p) {
    return uint64_t(get_u32(p)) | (uint64_t(get_u32(p + 4)) << 32);
}

namespace fmt {
const char MAGIC[4] = {'B', 'I', 'D', 'X'};
const uint16_t VERSION = 1;
const uint16_t HEADER_SIZE = 64;
const uint16_t DOC_REC = 12;
const uint16_t TERM_REC = 16;
const size_t MAX_STRING = 65535;
const uint32_t FLAG_STEMMED = 1;
const uint32_t FLAG_COMPRESSED = 2;
const uint32_t FLAG_TF = 4;
const uint32_t KNOWN_FLAGS = FLAG_STEMMED | FLAG_COMPRESSED | FLAG_TF;
const uint16_t DOC_REC_TF = 16;
}
