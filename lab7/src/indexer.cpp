#include "index_builder.h"

#include <sys/resource.h>

int main(int argc, char** argv) {
    bool stem_terms = false, compress = false, tf = false, ok = argc >= 2;
    for (int i = 1; ok && i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "-s") == 0) stem_terms = true;
        else if (std::strcmp(argv[i], "-c") == 0) compress = true;
        else if (std::strcmp(argv[i], "-t") == 0) tf = true;
        else ok = false;
    }
    if (!ok || argv[argc - 1][0] == '-') {
        std::fprintf(stderr, "usage: indexer [-s] [-c] [-t] INDEX < doclist\ndoclist line: path<TAB>title<TAB>url\n"
                             "-s: index Porter stems instead of tokens\n"
                             "-c: compress document lists (gaps in variable byte code)\n"
                             "-t: store term frequencies and document vector lengths (tf-idf ranking)\n");
        return 2;
    }
    const char* out_path = argv[argc - 1];
    if (!tok::init()) {
        std::fprintf(stderr, "indexer: locale C.UTF-8 is not available\n");
        return 2;
    }
    double start = now(), read_s = 0, index_s = 0;
    IndexBuilder* builder = new IndexBuilder(stem_terms, compress, tf);
    Vec<char> text;
    char* line = nullptr;
    size_t line_cap = 0, lineno = 0;
    ssize_t len;
    while ((len = getline(&line, &line_cap, stdin)) != -1) {
        ++lineno;
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = 0;
        if (len == 0) continue;
        char *path, *title, *url;
        if (!split_doc_line(line, path, title, url)) {
            std::fprintf(stderr, "indexer: line %zu: expected path<TAB>title<TAB>url\n", lineno);
            return 1;
        }
        double a = now();
        if (!read_file(path, text)) {
            std::fprintf(stderr, "indexer: cannot open %s\n", path);
            return 1;
        }
        double b = now();
        builder->add_text(text.data, text.size, title, size_t(url - title - 1), url, size_t(line + len - url));
        double e = now();
        read_s += b - a;
        index_s += e - b;
    }
    std::free(line);

    double sort_start = now();
    builder->sort();
    double write_start = now();
    if (!builder->write(out_path)) {
        std::fprintf(stderr, "indexer: cannot write %s\n", out_path);
        return 1;
    }
    double end = now();

    const IndexStats& s = builder->stats;
    rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    double total = end - start, kb = s.bytes / 1024.0;
    std::printf("docs\t%llu\n", (unsigned long long)s.docs);
    std::printf("stemmed\t%d\n", stem_terms ? 1 : 0);
    std::printf("compressed\t%d\n", compress ? 1 : 0);
    std::printf("tf\t%d\n", tf ? 1 : 0);
    std::printf("bytes\t%llu\n", (unsigned long long)s.bytes);
    std::printf("tokens\t%llu\n", (unsigned long long)s.tokens);
    std::printf("terms\t%llu\n", (unsigned long long)s.terms);
    std::printf("postings\t%llu\n", (unsigned long long)s.postings);
    std::printf("avg_token_len\t%.3f\n", s.tokens ? double(s.chars) / s.tokens : 0.0);
    std::printf("avg_term_len\t%.3f\n", s.terms ? double(s.term_chars) / s.terms : 0.0);
    std::printf("skipped_tokens\t%llu\n", (unsigned long long)s.skipped);
    std::printf("postings_bytes\t%llu\n", (unsigned long long)s.postings_bytes);
    std::printf("index_bytes\t%llu\n", (unsigned long long)s.index_bytes);
    std::printf("time_s\t%.3f\n", total);
    std::printf("read_s\t%.3f\n", read_s);
    std::printf("index_s\t%.3f\n", index_s);
    std::printf("sort_s\t%.3f\n", write_start - sort_start);
    std::printf("write_s\t%.3f\n", end - write_start);
    std::printf("ms_per_doc\t%.4f\n", s.docs ? total * 1e3 / s.docs : 0.0);
    std::printf("us_per_kb\t%.3f\n", kb > 0 ? total * 1e6 / kb : 0.0);
    std::printf("kb_per_s\t%.0f\n", total > 0 ? kb / total : 0.0);
    std::printf("peak_rss_mb\t%.1f\n", ru.ru_maxrss / 1024.0);
    return 0;
}
