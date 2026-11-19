#include "stats.h"

#include <cstring>
#include <iostream>

static int print_tokens() {
    std::string text, out;
    tok::read_stream(stdin, text);
    tok::tokenize(text.data(), text.size(), [&](const std::string& t, size_t) {
        out += t;
        out += '\n';
    });
    std::fwrite(out.data(), 1, out.size(), stdout);
    return 0;
}

static int print_stats() {
    std::string path, text;
    tok::Stats st;
    tok::Clock::time_point start = tok::Clock::now();
    while (std::getline(std::cin, path)) {
        if (path.empty()) continue;
        if (!tok::count_file(path.c_str(), st, text)) {
            std::fprintf(stderr, "tokenizer: cannot open %s\n", path.c_str());
            return 1;
        }
    }
    double total = tok::seconds(start, tok::Clock::now());
    double kb = st.bytes / 1024.0;
    std::printf("files\t%zu\n", st.files);
    std::printf("bytes\t%zu\n", st.bytes);
    std::printf("tokens\t%zu\n", st.tokens);
    std::printf("chars\t%zu\n", st.chars);
    std::printf("avg_len\t%.3f\n", st.avg_len());
    std::printf("time_s\t%.3f\n", total);
    std::printf("read_s\t%.3f\n", st.read_s);
    std::printf("tokenize_s\t%.3f\n", st.tok_s);
    std::printf("us_per_kb\t%.3f\n", kb > 0 ? total * 1e6 / kb : 0.0);
    std::printf("kb_per_s\t%.0f\n", total > 0 ? kb / total : 0.0);
    return 0;
}

int main(int argc, char** argv) {
    if (!tok::init()) {
        std::fprintf(stderr, "tokenizer: locale C.UTF-8 is not available\n");
        return 2;
    }
    if (argc == 1) return print_tokens();
    if (argc == 2 && std::strcmp(argv[1], "-s") == 0) return print_stats();
    std::fprintf(stderr, "usage: tokenizer < text > tokens\n       tokenizer -s < file_list > stats\n");
    return 2;
}
