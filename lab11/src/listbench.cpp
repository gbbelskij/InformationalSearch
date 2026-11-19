#include "index_reader.h"

struct Group {
    const char* name;
    uint32_t lo, hi;
};

static const Group GROUPS[] = {
    {"all", 1, UINT32_MAX},          {"rare", 1, 100},           {"medium", 101, 10000},
    {"high", 10001, UINT32_MAX},     {"df1", 1, 1},              {"df2-10", 2, 10},
    {"df11-100", 11, 100},           {"df101-1000", 101, 1000},  {"df1001-10000", 1001, 10000},
    {"df10001-100000", 10001, 100000}, {"df100001+", 100001, UINT32_MAX},
};

static bool parse_num(const char* s, size_t& out) {
    char* end;
    unsigned long long v = std::strtoull(s, &end, 10);
    if (end == s || *end) return false;
    out = size_t(v);
    return true;
}

static double traverse(const Index& ix, const Vec<uint32_t>& ks, uint32_t* buf, uint64_t& sum, bool& ok) {
    double start = now();
    uint64_t s = 0;
    for (size_t i = 0; i < ks.size; ++i) {
        size_t k = ks[i];
        uint32_t n = ix.df(k);
        if (!ix.read_list(k, buf)) ok = false;
        for (uint32_t j = 0; j < n; ++j) s += buf[j];
    }
    double t = now() - start;
    sum = s;
    return t;
}

static int bench(const char* path, size_t repeat) {
    double start = now();
    Index ix;
    if (!ix.load(path, "listbench")) return 1;
    double load_ms = (now() - start) * 1e3;
    uint32_t max_df = 0;
    for (size_t k = 0; k < ix.terms; ++k)
        if (ix.df(k) > max_df) max_df = ix.df(k);
    uint32_t* buf = static_cast<uint32_t*>(xrealloc(nullptr, (max_df ? max_df : 1) * sizeof(uint32_t)));
    std::printf("index\t%s\n", path);
    std::printf("compressed\t%d\n", ix.compressed ? 1 : 0);
    std::printf("docs\t%u\n", ix.docs);
    std::printf("terms\t%u\n", ix.terms);
    std::printf("postings\t%llu\n", (unsigned long long)ix.postings);
    std::printf("file_bytes\t%zu\n", ix.raw.size);
    std::printf("postings_bytes\t%llu\n", (unsigned long long)ix.postings_bytes());
    std::printf("load_ms\t%.1f\n", load_ms);
    std::printf("repeat\t%zu\n", repeat);
    bool ok = true;
    for (const Group& g : GROUPS) {
        Vec<uint32_t> ks;
        uint64_t postings = 0, bytes = 0;
        for (size_t k = 0; k < ix.terms; ++k) {
            uint32_t df = ix.df(k);
            if (df < g.lo || df > g.hi) continue;
            ks.push(uint32_t(k));
            postings += df;
            bytes += ix.list_bytes(k);
        }
        double best = 0;
        uint64_t sum = 0;
        for (size_t r = 0; r < repeat; ++r) {
            double t = traverse(ix, ks, buf, sum, ok);
            if (r == 0 || t < best) best = t;
        }
        std::printf("group\t%s\t%u\t%u\t%zu\t%llu\t%llu\t%.3f\t%llu\n", g.name, g.lo, g.hi, ks.size,
                    (unsigned long long)postings, (unsigned long long)bytes, best * 1e3, (unsigned long long)sum);
    }
    std::free(buf);
    if (!ok) {
        std::fprintf(stderr, "listbench: %s: damaged document lists\n", path);
        return 1;
    }
    return 0;
}

static int verify(const char* a_path, const char* b_path) {
    Index a, b;
    if (!a.load(a_path, "listbench") || !b.load(b_path, "listbench")) return 1;
    if (a.docs != b.docs || a.terms != b.terms || a.postings != b.postings) {
        std::printf("same_shape\t0\n");
        return 1;
    }
    uint32_t max_df = 0;
    for (size_t k = 0; k < a.terms; ++k)
        if (a.df(k) > max_df) max_df = a.df(k);
    size_t cap = max_df ? max_df : 1;
    uint32_t* x = static_cast<uint32_t*>(xrealloc(nullptr, cap * sizeof(uint32_t)));
    uint32_t* y = static_cast<uint32_t*>(xrealloc(nullptr, cap * sizeof(uint32_t)));
    uint64_t checked = 0, bad_terms = 0;
    for (size_t k = 0; k < a.terms; ++k) {
        uint16_t la, lb;
        const unsigned char* sa = a.str(get_u32(a.term_at(k)), la);
        const unsigned char* sb = b.str(get_u32(b.term_at(k)), lb);
        uint32_t n = a.df(k);
        bool same = la == lb && std::memcmp(sa, sb, la) == 0 && n == b.df(k) && a.read_list(k, x) &&
                    b.read_list(k, y) && std::memcmp(x, y, size_t(n) * sizeof(uint32_t)) == 0;
        if (!same && bad_terms++ < 10) std::printf("mismatch\t%zu\t%.*s\n", k, int(la), sa);
        checked += n;
    }
    uint64_t bad_docs = 0;
    for (uint32_t d = 0; d < a.docs; ++d) {
        const unsigned char* ra = a.doc_at(d);
        const unsigned char* rb = b.doc_at(d);
        uint16_t lta, ltb, lua, lub;
        const unsigned char* ta = a.str(get_u32(ra), lta);
        const unsigned char* tb = b.str(get_u32(rb), ltb);
        const unsigned char* ua = a.str(get_u32(ra + 4), lua);
        const unsigned char* ub = b.str(get_u32(rb + 4), lub);
        bool same = lta == ltb && lua == lub && std::memcmp(ta, tb, lta) == 0 && std::memcmp(ua, ub, lua) == 0 &&
                    get_u32(ra + 8) == get_u32(rb + 8);
        bad_docs += !same;
    }
    std::free(x);
    std::free(y);
    std::printf("same_shape\t1\n");
    std::printf("terms_checked\t%u\n", a.terms);
    std::printf("postings_checked\t%llu\n", (unsigned long long)checked);
    std::printf("docs_checked\t%u\n", a.docs);
    std::printf("term_mismatches\t%llu\n", (unsigned long long)bad_terms);
    std::printf("doc_mismatches\t%llu\n", (unsigned long long)bad_docs);
    return bad_terms || bad_docs ? 1 : 0;
}

int main(int argc, char** argv) {
    if (argc == 4 && std::strcmp(argv[1], "-v") == 0) return verify(argv[2], argv[3]);
    size_t repeat = 5;
    bool ok = argc == 2 || (argc == 4 && std::strcmp(argv[2], "-r") == 0 && parse_num(argv[3], repeat) && repeat > 0);
    if (!ok || argv[1][0] == '-') {
        std::fprintf(stderr,
                     "usage: listbench INDEX [-r REPEAT]   traverse document lists by df groups\n"
                     "       listbench -v INDEX_A INDEX_B  compare terms, lists and documents of two indexes\n");
        return 2;
    }
    return bench(argv[1], repeat);
}
