#include "index_reader.h"
#include "tokenizer.h"

static void print_term(const Index& ix, const std::string& term) {
    long k = ix.find(term.data(), term.size());
    if (k < 0) {
        std::printf("%s\t0\t\n", term.c_str());
        return;
    }
    uint32_t df = ix.df(size_t(k));
    Vec<uint32_t> docs;
    docs.reserve(df);
    if (!ix.read_list(size_t(k), docs.data)) {
        std::printf("%s\t%u\tdamaged\n", term.c_str(), df);
        return;
    }
    std::printf("%s\t%u\t", term.c_str(), df);
    for (uint32_t i = 0; i < df; ++i) std::printf(i ? " %u" : "%u", docs[i]);
    std::printf("\n");
}

static void print_doc(const Index& ix, const char* line) {
    char* end;
    unsigned long id = std::strtoul(line, &end, 10);
    if (end == line || *end || id >= ix.docs) {
        std::printf("%s\tnot found\n", line);
        return;
    }
    const unsigned char* rec = ix.doc_at(uint32_t(id));
    uint16_t tl, ul;
    const unsigned char* title = ix.str(get_u32(rec), tl);
    const unsigned char* url = ix.str(get_u32(rec + 4), ul);
    std::printf("%lu\t%.*s\t%.*s\t%u\n", id, int(tl), title, int(ul), url, get_u32(rec + 8));
}

int main(int argc, char** argv) {
    bool doc_mode = argc == 3 && std::strcmp(argv[2], "-d") == 0;
    if (argc != 2 && !doc_mode) {
        std::fprintf(stderr, "usage: lookup INDEX < terms\n       lookup INDEX -d < doc_ids\n");
        return 2;
    }
    if (!tok::init()) {
        std::fprintf(stderr, "lookup: locale C.UTF-8 is not available\n");
        return 2;
    }
    Index ix;
    if (!ix.load(argv[1], "lookup")) return 1;
    char* line = nullptr;
    size_t cap = 0;
    ssize_t len;
    while ((len = getline(&line, &cap, stdin)) != -1) {
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = 0;
        if (doc_mode) {
            if (len) print_doc(ix, line);
        } else {
            tok::tokenize(line, size_t(len), [&](const std::string& t, size_t) { print_term(ix, t); });
        }
    }
    std::free(line);
    return 0;
}
