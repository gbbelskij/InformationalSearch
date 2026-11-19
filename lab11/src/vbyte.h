#pragma once

#include <cstddef>
#include <cstdint>

namespace vb {

const size_t MAX_BYTES = 5;

inline size_t size(uint32_t v) {
    size_t n = 1;
    while (v >= 0x80) {
        v >>= 7;
        ++n;
    }
    return n;
}

inline size_t encode(uint32_t v, unsigned char* out) {
    size_t n = 0;
    while (v >= 0x80) {
        out[n++] = static_cast<unsigned char>(v & 0x7F);
        v >>= 7;
    }
    out[n++] = static_cast<unsigned char>(v | 0x80);
    return n;
}

inline const unsigned char* decode(const unsigned char* p, const unsigned char* end, uint32_t& v) {
    uint32_t x = 0;
    for (unsigned shift = 0; p < end && shift < 35; shift += 7) {
        unsigned char b = *p++;
        if (shift == 28 && (b & 0x70)) return nullptr;
        x |= uint32_t(b & 0x7F) << shift;
        if (b & 0x80) {
            v = x;
            return p;
        }
    }
    return nullptr;
}

inline size_t list_size(const uint32_t* ids, size_t n) {
    size_t bytes = 0;
    uint32_t prev = 0;
    for (size_t i = 0; i < n; ++i) {
        bytes += size(ids[i] - prev);
        prev = ids[i];
    }
    return bytes;
}

inline size_t encode_list(const uint32_t* ids, size_t n, unsigned char* out) {
    size_t bytes = 0;
    uint32_t prev = 0;
    for (size_t i = 0; i < n; ++i) {
        bytes += encode(ids[i] - prev, out + bytes);
        prev = ids[i];
    }
    return bytes;
}

inline size_t pairs_size(const uint32_t* ids, const uint32_t* tfs, size_t n) {
    size_t bytes = 0;
    uint32_t prev = 0;
    for (size_t i = 0; i < n; ++i) {
        bytes += size(ids[i] - prev) + size(tfs[i]);
        prev = ids[i];
    }
    return bytes;
}

inline const unsigned char* decode_pairs(const unsigned char* p, const unsigned char* end, size_t n, uint32_t* ids,
                                         uint32_t* tfs) {
    uint32_t prev = 0;
    for (size_t i = 0; i < n; ++i) {
        uint32_t gap, tf;
        p = decode(p, end, gap);
        if (!p || (i && gap == 0) || gap > UINT32_MAX - prev) return nullptr;
        p = decode(p, end, tf);
        if (!p || tf == 0) return nullptr;
        prev += gap;
        ids[i] = prev;
        if (tfs) tfs[i] = tf;
    }
    return p;
}

inline const unsigned char* decode_list(const unsigned char* p, const unsigned char* end, size_t n, uint32_t* out) {
    uint32_t prev = 0;
    for (size_t i = 0; i < n; ++i) {
        uint32_t gap;
        p = decode(p, end, gap);
        if (!p || (i && gap == 0) || gap > UINT32_MAX - prev) return nullptr;
        prev += gap;
        out[i] = prev;
    }
    return p;
}

}
