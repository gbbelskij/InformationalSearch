#pragma once

#include "common.h"
#include "vbyte.h"

struct Index {
    Vec<char> raw;
    const unsigned char* base = nullptr;
    uint32_t flags = 0, docs = 0, terms = 0;
    bool compressed = false, has_tf = false;
    uint16_t doc_rec = 0, term_rec = 0;
    uint64_t postings = 0, docs_off = 0, terms_off = 0, postings_off = 0, strings_off = 0;

    const unsigned char* str(uint32_t off, uint16_t& len) const {
        const unsigned char* p = base + strings_off + off;
        len = get_u16(p);
        return p + 2;
    }
    const unsigned char* term_at(size_t k) const { return base + terms_off + k * term_rec; }
    uint32_t df(size_t k) const { return get_u32(term_at(k) + 4); }
    uint64_t first(size_t k) const { return get_u64(term_at(k) + 8); }
    uint64_t postings_bytes() const { return strings_off - postings_off; }
    unsigned record() const { return has_tf ? 8 : 4; }
    const unsigned char* postings_of(size_t k) const {
        return base + postings_off + (compressed ? first(k) : record() * first(k));
    }
    uint64_t list_bytes(size_t k) const {
        if (!compressed) return record() * uint64_t(df(k));
        return (k + 1 < terms ? first(k + 1) : postings_bytes()) - first(k);
    }
    bool read_list_tf(size_t k, uint32_t* out, uint32_t* tfs) const {
        uint32_t n = df(k);
        const unsigned char* p = postings_of(k);
        if (!compressed) {
            unsigned step = record();
            for (uint32_t i = 0; i < n; ++i) {
                out[i] = get_u32(p + step * i);
                if (tfs) tfs[i] = has_tf ? get_u32(p + step * i + 4) : 1;
            }
            return true;
        }
        const unsigned char* end = p + list_bytes(k);
        if (has_tf) return vb::decode_pairs(p, end, n, out, tfs) == end;
        if (tfs)
            for (uint32_t i = 0; i < n; ++i) tfs[i] = 1;
        return vb::decode_list(p, end, n, out) == end;
    }
    bool read_list(size_t k, uint32_t* out) const { return read_list_tf(k, out, nullptr); }
    float doc_norm(uint32_t id) const { return has_tf ? get_f32(doc_at(id) + 12) : 1.0f; }
    const unsigned char* doc_at(uint32_t id) const { return base + docs_off + size_t(id) * doc_rec; }

    bool load(const char* path, const char* prog) {
        if (!read_file(path, raw)) {
            std::fprintf(stderr, "%s: cannot open %s\n", prog, path);
            return false;
        }
        const unsigned char* p = reinterpret_cast<const unsigned char*>(raw.data);
        size_t size = raw.size;
        if (size < fmt::HEADER_SIZE || std::memcmp(p, fmt::MAGIC, 4) != 0 || get_u16(p + 4) != fmt::VERSION) {
            std::fprintf(stderr, "%s: %s is not an index of version %u\n", prog, path, fmt::VERSION);
            return false;
        }
        base = p;
        flags = get_u32(p + 8);
        docs = get_u32(p + 12);
        terms = get_u32(p + 16);
        doc_rec = get_u16(p + 20);
        term_rec = get_u16(p + 22);
        postings = get_u64(p + 24);
        docs_off = get_u64(p + 32);
        terms_off = get_u64(p + 40);
        postings_off = get_u64(p + 48);
        strings_off = get_u64(p + 56);
        compressed = flags & fmt::FLAG_COMPRESSED;
        has_tf = flags & fmt::FLAG_TF;
        if (flags & ~fmt::KNOWN_FLAGS) {
            std::fprintf(stderr, "%s: %s has unknown flags %u\n", prog, path, flags);
            return false;
        }
        bool ok = doc_rec >= fmt::DOC_REC && term_rec >= fmt::TERM_REC && docs_off + uint64_t(doc_rec) * docs <= size &&
                  terms_off + uint64_t(term_rec) * terms <= size && strings_off <= size;
        if (ok && has_tf) ok = doc_rec >= fmt::DOC_REC_TF;
        if (ok && !compressed) ok = postings_off + record() * postings <= size;
        if (ok && compressed) {
            ok = postings_off <= strings_off;
            uint64_t prev = 0;
            for (size_t k = 0; ok && k < terms; ++k) {
                uint64_t f = first(k);
                ok = f >= prev && f <= postings_bytes();
                prev = f;
            }
        }
        if (!ok) {
            std::fprintf(stderr, "%s: %s is damaged\n", prog, path);
            return false;
        }
        return true;
    }

    long find(const char* s, size_t n) const {
        size_t lo = 0, hi = terms;
        while (lo < hi) {
            size_t mid = lo + (hi - lo) / 2;
            uint16_t len;
            const unsigned char* term = str(get_u32(term_at(mid)), len);
            int c = compare_bytes(reinterpret_cast<const char*>(term), len, s, n);
            if (c == 0) return long(mid);
            if (c < 0) lo = mid + 1;
            else hi = mid;
        }
        return -1;
    }
};
