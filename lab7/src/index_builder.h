#pragma once

#include "common.h"
#include "porter.h"
#include "tokenizer.h"
#include "vbyte.h"

#include <cmath>

struct Term {
    uint32_t str;
    uint32_t hash;
    uint16_t len;
    uint16_t chars;
    uint32_t last_doc;
    uint32_t df;
    uint32_t cap;
    uint32_t* docs;
    uint32_t* tfs;
};

struct DocRec {
    uint32_t title;
    uint32_t url;
    uint32_t tokens;
};

inline uint32_t fnv1a(const char* s, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; ++i) {
        h ^= static_cast<unsigned char>(s[i]);
        h *= 16777619u;
    }
    return h;
}

struct Dict {
    Vec<Term> terms;
    Vec<char> arena;
    uint32_t* slots = nullptr;
    size_t mask = 0;
    bool with_tf = false;

    Dict() { resize(1 << 12); }
    ~Dict() {
        std::free(slots);
        for (size_t i = 0; i < terms.size; ++i) {
            std::free(terms[i].docs);
            std::free(terms[i].tfs);
        }
    }

    void resize(size_t cap) {
        std::free(slots);
        slots = static_cast<uint32_t*>(xrealloc(nullptr, cap * sizeof(uint32_t)));
        std::memset(slots, 0xFF, cap * sizeof(uint32_t));
        mask = cap - 1;
        for (size_t i = 0; i < terms.size; ++i) {
            size_t j = terms[i].hash & mask;
            while (slots[j] != UINT32_MAX) j = (j + 1) & mask;
            slots[j] = uint32_t(i);
        }
    }

    void add(const char* s, size_t n, size_t chars, uint32_t doc) {
        uint32_t h = fnv1a(s, n);
        size_t j = h & mask;
        while (slots[j] != UINT32_MAX) {
            Term& t = terms[slots[j]];
            if (t.hash == h && t.len == n && std::memcmp(arena.data + t.str, s, n) == 0) {
                post(t, doc, with_tf);
                return;
            }
            j = (j + 1) & mask;
        }
        Term t = {uint32_t(arena.size), h, uint16_t(n), uint16_t(chars), UINT32_MAX, 0, 0, nullptr, nullptr};
        arena.append(s, n);
        post(t, doc, with_tf);
        slots[j] = uint32_t(terms.size);
        terms.push(t);
        if (terms.size * 2 > mask + 1) resize((mask + 1) * 2);
    }

    static void post(Term& t, uint32_t doc, bool with_tf) {
        if (t.last_doc == doc) {
            if (with_tf && t.tfs[t.df - 1] < UINT32_MAX) ++t.tfs[t.df - 1];
            return;
        }
        if (t.df == t.cap) {
            t.cap = t.cap ? t.cap * 2 : 2;
            t.docs = static_cast<uint32_t*>(xrealloc(t.docs, t.cap * sizeof(uint32_t)));
            if (with_tf) t.tfs = static_cast<uint32_t*>(xrealloc(t.tfs, t.cap * sizeof(uint32_t)));
        }
        if (with_tf) t.tfs[t.df] = 1;
        t.docs[t.df++] = doc;
        t.last_doc = doc;
    }

    int compare(uint32_t a, uint32_t b) const {
        const Term& x = terms[a];
        const Term& y = terms[b];
        return compare_bytes(arena.data + x.str, x.len, arena.data + y.str, y.len);
    }
};

inline void sort_terms(const Dict& d, uint32_t* ids, size_t n) {
    uint32_t* tmp = static_cast<uint32_t*>(xrealloc(nullptr, (n ? n : 1) * sizeof(uint32_t)));
    uint32_t* src = ids;
    uint32_t* dst = tmp;
    for (size_t width = 1; width < n; width *= 2) {
        for (size_t lo = 0; lo < n; lo += 2 * width) {
            size_t mid = lo + width < n ? lo + width : n;
            size_t hi = lo + 2 * width < n ? lo + 2 * width : n;
            size_t i = lo, j = mid, k = lo;
            while (i < mid && j < hi) dst[k++] = d.compare(src[j], src[i]) < 0 ? src[j++] : src[i++];
            while (i < mid) dst[k++] = src[i++];
            while (j < hi) dst[k++] = src[j++];
        }
        uint32_t* s = src;
        src = dst;
        dst = s;
    }
    if (src != ids) std::memcpy(ids, src, n * sizeof(uint32_t));
    std::free(tmp);
}

inline uint32_t add_string(Vec<char>& pool, const char* s, size_t n) {
    if (n > fmt::MAX_STRING) n = fmt::MAX_STRING;
    if (pool.size + n + 2 > UINT32_MAX) {
        std::fprintf(stderr, "indexer: string pool exceeds 4 GiB\n");
        std::exit(1);
    }
    uint32_t off = uint32_t(pool.size);
    char len[2] = {char(n & 0xFF), char(n >> 8)};
    pool.append(len, 2);
    pool.append(s, n);
    return off;
}

struct Writer {
    FILE* f;
    unsigned char buf[1 << 16];
    size_t n = 0;

    explicit Writer(FILE* file) : f(file) {}
    void flush() {
        if (n && std::fwrite(buf, 1, n, f) != n) {
            std::perror("indexer: write");
            std::exit(1);
        }
        n = 0;
    }
    void put(const void* p, size_t k) {
        const unsigned char* s = static_cast<const unsigned char*>(p);
        while (k) {
            size_t c = sizeof buf - n < k ? sizeof buf - n : k;
            std::memcpy(buf + n, s, c);
            n += c;
            s += c;
            k -= c;
            if (n == sizeof buf) flush();
        }
    }
    void u16(uint16_t v) {
        unsigned char b[2] = {uint8_t(v), uint8_t(v >> 8)};
        put(b, 2);
    }
    void u32(uint32_t v) {
        unsigned char b[4] = {uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24)};
        put(b, 4);
    }
    void u64(uint64_t v) {
        u32(uint32_t(v));
        u32(uint32_t(v >> 32));
    }
    void f32(float v) {
        uint32_t u;
        std::memcpy(&u, &v, 4);
        u32(u);
    }
    void vbyte(uint32_t v) {
        if (sizeof buf - n < vb::MAX_BYTES) flush();
        n += vb::encode(v, buf + n);
    }
};

inline bool split_doc_line(char* line, char*& path, char*& title, char*& url) {
    char* t1 = std::strchr(line, '\t');
    char* t2 = t1 ? std::strchr(t1 + 1, '\t') : nullptr;
    if (!t2) return false;
    *t1 = 0;
    *t2 = 0;
    path = line;
    title = t1 + 1;
    url = t2 + 1;
    return true;
}

struct IndexStats {
    uint64_t docs = 0, bytes = 0, tokens = 0, chars = 0, skipped = 0;
    uint64_t terms = 0, postings = 0, term_chars = 0, index_bytes = 0, postings_bytes = 0;
};

class IndexBuilder {
public:
    IndexStats stats;

    explicit IndexBuilder(bool stem, bool compress = false, bool tf = false)
        : stem_terms(stem), compress_lists(compress), with_tf(tf) {
        dict.with_tf = tf;
        stem_buf = static_cast<char*>(xrealloc(nullptr, fmt::MAX_STRING));
    }
    ~IndexBuilder() {
        std::free(stem_buf);
        std::free(order);
    }
    IndexBuilder(const IndexBuilder&) = delete;
    IndexBuilder& operator=(const IndexBuilder&) = delete;

    void add_text(const char* text, size_t n, const char* title, size_t title_len, const char* url, size_t url_len) {
        uint32_t id = uint32_t(docs.size);
        uint32_t doc_tokens = 0;
        tok::tokenize(text, n, [&](const std::string& tk, size_t c) {
            ++doc_tokens;
            stats.chars += c;
            if (tk.size() > fmt::MAX_STRING) {
                ++stats.skipped;
                return;
            }
            if (!stem_terms) {
                dict.add(tk.data(), tk.size(), c, id);
                return;
            }
            std::memcpy(stem_buf, tk.data(), tk.size());
            size_t len = porter::stem(stem_buf, tk.size());
            dict.add(stem_buf, len, c - (tk.size() - len), id);
        });
        stats.bytes += n;
        stats.tokens += doc_tokens;
        ++stats.docs;
        DocRec d = {add_string(pool, title, title_len), add_string(pool, url, url_len), doc_tokens};
        docs.push(d);
    }

    void sort() {
        size_t T = dict.terms.size;
        order = static_cast<uint32_t*>(xrealloc(order, (T ? T : 1) * sizeof(uint32_t)));
        for (size_t i = 0; i < T; ++i) order[i] = uint32_t(i);
        sort_terms(dict, order, T);
        sorted = true;
    }

    bool write(const char* path) {
        if (!sorted) sort();
        size_t T = dict.terms.size;
        uint64_t P = 0, term_chars = 0, list_bytes = 0;
        uint32_t* term_off = static_cast<uint32_t*>(xrealloc(nullptr, (T ? T : 1) * sizeof(uint32_t)));
        uint64_t* first = static_cast<uint64_t*>(xrealloc(nullptr, (T ? T : 1) * sizeof(uint64_t)));
        for (size_t k = 0; k < T; ++k) {
            const Term& t = dict.terms[order[k]];
            first[k] = compress_lists ? list_bytes : P;
            P += t.df;
            if (compress_lists)
                list_bytes += with_tf ? vb::pairs_size(t.docs, t.tfs, t.df) : vb::list_size(t.docs, t.df);
            else
                list_bytes += (with_tf ? 8 : 4) * uint64_t(t.df);
            term_chars += t.chars;
            term_off[k] = add_string(pool, dict.arena.data + t.str, t.len);
        }
        uint32_t flags = (stem_terms ? fmt::FLAG_STEMMED : 0) | (compress_lists ? fmt::FLAG_COMPRESSED : 0) |
                         (with_tf ? fmt::FLAG_TF : 0);
        uint16_t doc_rec = with_tf ? fmt::DOC_REC_TF : fmt::DOC_REC;
        float* norms = with_tf ? doc_norms() : nullptr;
        uint64_t docs_off = fmt::HEADER_SIZE;
        uint64_t terms_off = docs_off + uint64_t(doc_rec) * docs.size;
        uint64_t postings_off = terms_off + uint64_t(fmt::TERM_REC) * T;
        uint64_t strings_off = postings_off + list_bytes;
        FILE* f = std::fopen(path, "wb");
        if (!f) {
            std::free(term_off);
            std::free(first);
            std::free(norms);
            return false;
        }
        Writer* w = new Writer(f);
        w->put(fmt::MAGIC, 4);
        w->u16(fmt::VERSION);
        w->u16(fmt::HEADER_SIZE);
        w->u32(flags);
        w->u32(uint32_t(docs.size));
        w->u32(uint32_t(T));
        w->u16(doc_rec);
        w->u16(fmt::TERM_REC);
        w->u64(P);
        w->u64(docs_off);
        w->u64(terms_off);
        w->u64(postings_off);
        w->u64(strings_off);
        for (size_t i = 0; i < docs.size; ++i) {
            w->u32(docs[i].title);
            w->u32(docs[i].url);
            w->u32(docs[i].tokens);
            if (with_tf) w->f32(norms[i]);
        }
        for (size_t k = 0; k < T; ++k) {
            const Term& t = dict.terms[order[k]];
            w->u32(term_off[k]);
            w->u32(t.df);
            w->u64(first[k]);
        }
        for (size_t k = 0; k < T; ++k) {
            const Term& t = dict.terms[order[k]];
            if (compress_lists) {
                uint32_t prev = 0;
                for (uint32_t i = 0; i < t.df; ++i) {
                    w->vbyte(t.docs[i] - prev);
                    if (with_tf) w->vbyte(t.tfs[i]);
                    prev = t.docs[i];
                }
            } else {
                for (uint32_t i = 0; i < t.df; ++i) {
                    w->u32(t.docs[i]);
                    if (with_tf) w->u32(t.tfs[i]);
                }
            }
        }
        w->put(pool.data, pool.size);
        w->flush();
        delete w;
        std::free(term_off);
        std::free(first);
        std::free(norms);
        stats.terms = T;
        stats.postings = P;
        stats.postings_bytes = list_bytes;
        stats.term_chars = term_chars;
        stats.index_bytes = strings_off + pool.size;
        return std::fclose(f) == 0;
    }

private:
    float* doc_norms() const {
        size_t n = docs.size;
        double* sum = static_cast<double*>(xrealloc(nullptr, (n ? n : 1) * sizeof(double)));
        for (size_t i = 0; i < n; ++i) sum[i] = 0;
        for (size_t k = 0; k < dict.terms.size; ++k) {
            const Term& t = dict.terms[k];
            for (uint32_t i = 0; i < t.df; ++i) {
                double w = 1.0 + std::log10(double(t.tfs[i]));
                sum[t.docs[i]] += w * w;
            }
        }
        float* out = static_cast<float*>(xrealloc(nullptr, (n ? n : 1) * sizeof(float)));
        for (size_t i = 0; i < n; ++i) out[i] = float(std::sqrt(sum[i]));
        std::free(sum);
        return out;
    }

    bool stem_terms;
    bool compress_lists;
    bool with_tf;
    bool sorted = false;
    Dict dict;
    Vec<DocRec> docs;
    Vec<char> pool;
    uint32_t* order = nullptr;
    char* stem_buf = nullptr;
};
