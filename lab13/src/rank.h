#pragma once

#include "query.h"

#include <cmath>

namespace rank {

inline double tf_weight(uint32_t tf) {
    return tf ? 1.0 + std::log10(double(tf)) : 0.0;
}

inline double idf(uint32_t docs, uint32_t df) {
    return df && docs ? std::log10(double(docs) / double(df)) : 0.0;
}

inline bool is_fuzzy(const char* q, size_t n) {
    for (size_t i = 0; i < n; ++i)
        if (q[i] == '&' || q[i] == '|' || q[i] == '!' || q[i] == '(' || q[i] == ')') return false;
    return true;
}

struct QTerm {
    const char* term;
    size_t len;
    uint32_t tf;
};

inline void collect(const Node* n, Vec<QTerm>& out) {
    if (!n || n->kind == NOT) return;
    if (n->kind != TERM) {
        for (size_t i = 0; i < n->kids.size; ++i) collect(n->kids[i], out);
        return;
    }
    for (size_t i = 0; i < out.size; ++i)
        if (out[i].len == n->len && std::memcmp(out[i].term, n->term, n->len) == 0) {
            ++out[i].tf;
            return;
        }
    QTerm t = {n->term, n->len, 1};
    out.push(t);
}

inline void show_fuzzy(const Node* root, Vec<char>& out) {
    Vec<QTerm> q;
    collect(root, q);
    out.append("any(", 4);
    for (size_t i = 0; i < q.size; ++i) {
        if (i) out.push(' ');
        out.append(q[i].term, q[i].len);
    }
    out.push(')');
}

struct Hit {
    uint32_t doc;
    double score;
};

inline bool before(const Hit& a, const Hit& b) {
    return a.score > b.score || (a.score == b.score && a.doc < b.doc);
}

inline void sort_hits(Hit* a, size_t n) {
    Hit* tmp = static_cast<Hit*>(xrealloc(nullptr, (n ? n : 1) * sizeof(Hit)));
    Hit* src = a;
    Hit* dst = tmp;
    for (size_t width = 1; width < n; width *= 2) {
        for (size_t lo = 0; lo < n; lo += 2 * width) {
            size_t mid = lo + width < n ? lo + width : n;
            size_t hi = lo + 2 * width < n ? lo + 2 * width : n;
            size_t i = lo, j = mid, k = lo;
            while (i < mid && j < hi) dst[k++] = before(src[j], src[i]) ? src[j++] : src[i++];
            while (i < mid) dst[k++] = src[i++];
            while (j < hi) dst[k++] = src[j++];
        }
        Hit* s = src;
        src = dst;
        dst = s;
    }
    if (src != a) std::memcpy(a, src, n * sizeof(Hit));
    std::free(tmp);
}

class Ranker {
public:
    explicit Ranker(const Index& index) : ix(index) {
        size_t n = ix.docs ? ix.docs : 1;
        acc = static_cast<double*>(xrealloc(nullptr, n * sizeof(double)));
        mark = static_cast<unsigned char*>(xrealloc(nullptr, n));
        for (size_t i = 0; i < n; ++i) acc[i] = 0;
        std::memset(mark, 0, n);
    }
    ~Ranker() {
        std::free(acc);
        std::free(mark);
        std::free(ids);
        std::free(tfs);
    }
    Ranker(const Ranker&) = delete;
    Ranker& operator=(const Ranker&) = delete;

    void run(const Node* root, bool fuzzy, Vec<Hit>& out) {
        out.size = 0;
        Vec<QTerm> q;
        collect(root, q);
        accumulate(q);
        if (fuzzy) {
            out.reserve(touched.size);
            for (size_t i = 0; i < touched.size; ++i) out.data[out.size++] = Hit{touched[i], acc[touched[i]]};
        } else {
            List res;
            if (root) eval(ix, root, res);
            out.reserve(res.size);
            for (size_t i = 0; i < res.size; ++i) out.data[out.size++] = Hit{res[i], acc[res[i]]};
        }
        for (size_t i = 0; i < touched.size; ++i) {
            acc[touched[i]] = 0;
            mark[touched[i]] = 0;
        }
        touched.size = 0;
        sort_hits(out.data, out.size);
    }

private:
    void accumulate(const Vec<QTerm>& q) {
        Vec<long> keys;
        Vec<double> w;
        double norm2 = 0;
        for (size_t i = 0; i < q.size; ++i) {
            long k = ix.find(q[i].term, q[i].len);
            double x = k < 0 ? 0.0 : tf_weight(q[i].tf) * idf(ix.docs, ix.df(size_t(k)));
            keys.push(k);
            w.push(x);
            norm2 += x * x;
        }
        double qnorm = std::sqrt(norm2);
        for (size_t i = 0; i < q.size; ++i) {
            if (keys[i] < 0) continue;
            size_t k = size_t(keys[i]);
            uint32_t n = ix.df(k);
            reserve(n);
            if (!ix.read_list_tf(k, ids, tfs)) {
                std::fprintf(stderr, "search: document list of %.*s is damaged\n", int(q[i].len), q[i].term);
                continue;
            }
            double wq = qnorm > 0 ? w[i] / qnorm : 0.0;
            for (uint32_t j = 0; j < n; ++j) {
                uint32_t d = ids[j];
                if (!mark[d]) {
                    mark[d] = 1;
                    touched.push(d);
                }
                double norm = ix.doc_norm(d);
                if (norm > 0) acc[d] += wq * tf_weight(tfs[j]) / norm;
            }
        }
    }

    void reserve(size_t n) {
        if (n <= cap) return;
        cap = n;
        ids = static_cast<uint32_t*>(xrealloc(ids, cap * sizeof(uint32_t)));
        tfs = static_cast<uint32_t*>(xrealloc(tfs, cap * sizeof(uint32_t)));
    }

    const Index& ix;
    double* acc = nullptr;
    unsigned char* mark = nullptr;
    uint32_t* ids = nullptr;
    uint32_t* tfs = nullptr;
    size_t cap = 0;
    Vec<uint32_t> touched;
};

}
