#include "porter.h"
#include "tokenizer.h"

#include <cstdio>
#include <cstdlib>

int main(int argc, char**) {
    if (argc != 1) {
        std::fprintf(stderr, "usage: stem < text > \"token<TAB>stem\" lines\n");
        return 2;
    }
    if (!tok::init()) {
        std::fprintf(stderr, "stem: locale C.UTF-8 is not available\n");
        return 2;
    }
    char* line = nullptr;
    size_t cap = 0;
    ssize_t len;
    char buf[1 << 16];
    while ((len = getline(&line, &cap, stdin)) != -1) {
        tok::tokenize(line, size_t(len), [&](const std::string& t, size_t) {
            size_t n = t.size() < sizeof buf ? t.size() : sizeof buf;
            std::memcpy(buf, t.data(), n);
            n = porter::stem(buf, n);
            std::printf("%s\t%.*s\n", t.c_str(), int(n), buf);
        });
    }
    std::free(line);
    return 0;
}
