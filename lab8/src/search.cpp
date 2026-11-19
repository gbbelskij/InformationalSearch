#include "rank.h"

static void run_query(const Index& ix, rank::Ranker* ranker, const char* q, size_t len, size_t offset, size_t limit,
                      int repeat, bool stem) {
    List res;
    Vec<rank::Hit> hits;
    Node* root = nullptr;
    bool fuzzy = ranker && rank::is_fuzzy(q, len);
    double start = now();
    for (int r = 0; r < repeat; ++r) {
        destroy(root);
        root = parse_query(q, len, stem);
        res.size = 0;
        if (ranker) ranker->run(root, fuzzy, hits);
        else if (root) eval(ix, root, res);
    }
    double us = (now() - start) * 1e6 / repeat;
    size_t found = ranker ? hits.size : res.size;
    Vec<char> parsed;
    if (root && fuzzy) rank::show_fuzzy(root, parsed);
    else if (root) show(root, parsed);
    std::printf("query: %.*s\n", int(len), q);
    std::printf("parsed: %.*s\n", int(parsed.size), parsed.data);
    std::printf("found: %zu\n", found);
    std::printf("time_us: %.1f\n", us);
    size_t begin, end;
    page_range(found, offset, limit, begin, end);
    for (size_t i = begin; i < end; ++i) {
        uint32_t id = ranker ? hits[i].doc : res[i];
        const unsigned char* rec = ix.doc_at(id);
        uint16_t tl, ul;
        const unsigned char* title = ix.str(get_u32(rec), tl);
        const unsigned char* url = ix.str(get_u32(rec + 4), ul);
        std::printf("%zu\t%u\t%.*s\t%.*s", i + 1, id, int(tl), title, int(ul), url);
        if (ranker) std::printf("\t%.4f", hits[i].score);
        std::printf("\n");
    }
    std::printf("\n");
    std::fflush(stdout);
    destroy(root);
}

int main(int argc, char** argv) {
    size_t limit = 50, offset = 0, repeat = 1;
    bool protocol = false, ok = argc >= 2;
    for (int i = 2; ok && i < argc; ++i) {
        if (std::strcmp(argv[i], "-p") == 0) protocol = true;
        else if (i + 1 < argc && std::strcmp(argv[i], "-n") == 0) ok = parse_count(argv[++i], limit);
        else if (i + 1 < argc && std::strcmp(argv[i], "-o") == 0) ok = parse_count(argv[++i], offset);
        else if (i + 1 < argc && std::strcmp(argv[i], "-r") == 0) ok = parse_count(argv[++i], repeat) && repeat > 0;
        else ok = false;
    }
    if (!ok) {
        std::fprintf(stderr,
                     "usage: search INDEX [-n LIMIT] [-o OFFSET] [-r REPEAT] < queries\n"
                     "       search INDEX -p < requests   (offset<TAB>limit<TAB>query)\n"
                     "LIMIT 0 means all results\n");
        return 2;
    }
    if (!tok::init()) {
        std::fprintf(stderr, "search: locale C.UTF-8 is not available\n");
        return 2;
    }
    double start = now();
    Index ix;
    if (!ix.load(argv[1], "search")) return 1;
    bool stem = ix.flags & fmt::FLAG_STEMMED;
    std::fprintf(stderr, "search: index loaded: %u docs, %u terms%s%s%s, %.1f ms\n", ix.docs, ix.terms,
                 stem ? " (stemmed)" : "", ix.compressed ? " (compressed)" : "", ix.has_tf ? " (tf-idf)" : "",
                 (now() - start) * 1e3);
    rank::Ranker* ranker = ix.has_tf ? new rank::Ranker(ix) : nullptr;
    char* line = nullptr;
    size_t cap = 0;
    ssize_t len;
    while ((len = getline(&line, &cap, stdin)) != -1) {
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = 0;
        size_t off = offset, lim = limit;
        const char* q = protocol ? parse_request(line, off, lim) : line;
        run_query(ix, ranker, q, size_t(line + len - q), off, lim, int(repeat), stem);
    }
    std::free(line);
    delete ranker;
    return 0;
}
