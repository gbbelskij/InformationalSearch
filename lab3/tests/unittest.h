#pragma once

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

namespace ut {

struct Case {
    const char* name;
    void (*fn)();
    Case* next;
};

inline Case*& first() {
    static Case* c = nullptr;
    return c;
}

inline Case*& last() {
    static Case* c = nullptr;
    return c;
}

inline int& failures() {
    static int n = 0;
    return n;
}

struct Registrar {
    explicit Registrar(Case* c) {
        if (last()) last()->next = c;
        else first() = c;
        last() = c;
    }
};

inline void check(bool ok, const char* what, const char* expr, const char* file, int line) {
    if (ok) {
        std::printf("  [ok] %s\n", what);
    } else {
        ++failures();
        std::printf("  [FAIL] %s  (%s:%d: %s)\n", what, file, line, expr);
    }
}

inline void note(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::printf("  ");
    std::vprintf(fmt, ap);
    std::printf("\n");
    va_end(ap);
}

inline std::string escape(const std::string& s) {
    std::string out;
    char buf[16];
    for (size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        size_t len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        bool valid = len != 0 && i + len <= s.size();
        for (size_t k = 1; valid && k < len; ++k) valid = (static_cast<unsigned char>(s[i + k]) & 0xC0) == 0x80;
        if (!valid) {
            std::snprintf(buf, sizeof buf, "\\x%02x", c);
            out += buf;
            ++i;
            continue;
        }
        uint32_t cp = len == 1 ? c : c & (0xFF >> (len + 1));
        for (size_t k = 1; k < len; ++k) cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        if (cp == '\t') out += "\\t";
        else if (cp == '\n') out += "\\n";
        else if (cp < 0x20 || cp == 0xA0) {
            std::snprintf(buf, sizeof buf, "\\x%02x", unsigned(cp));
            out += buf;
        } else if (cp < 0x180 || (cp >= 0x400 && cp < 0x530) || (cp >= 0x2000 && cp < 0x2070)) {
            out.append(s, i, len);
        } else {
            std::snprintf(buf, sizeof buf, "\\u%04x", unsigned(cp));
            out += buf;
        }
        i += len;
    }
    return out;
}

class TempDir {
public:
    TempDir() {
        std::strcpy(dir, "/tmp/irtestXXXXXX");
        if (!mkdtemp(dir)) {
            std::perror("mkdtemp");
            std::exit(2);
        }
    }
    ~TempDir() {
        std::string cmd = std::string("rm -rf '") + dir + "'";
        if (std::system(cmd.c_str()) != 0) std::fprintf(stderr, "cannot remove %s\n", dir);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    std::string path(const std::string& name) const { return std::string(dir) + "/" + name; }

    std::string write(const std::string& name, const std::string& data) const {
        std::string p = path(name);
        FILE* f = std::fopen(p.c_str(), "wb");
        if (!f || std::fwrite(data.data(), 1, data.size(), f) != data.size() || std::fclose(f) != 0) {
            std::perror(p.c_str());
            std::exit(2);
        }
        return p;
    }

private:
    char dir[32];
};

inline std::string run(const std::string& cmd, int* status = nullptr) {
    std::string out;
    if (status) *status = -1;
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return out;
    char buf[4096];
    size_t k;
    while ((k = std::fread(buf, 1, sizeof buf, p)) > 0) out.append(buf, k);
    int st = pclose(p);
    if (status) *status = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
    return out;
}

inline std::string read_file(const std::string& path) {
    std::string out;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return out;
    char buf[1 << 16];
    size_t k;
    while ((k = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, k);
    std::fclose(f);
    return out;
}

inline int run_all() {
    int n = 0, passed = 0;
    for (Case* c = first(); c; c = c->next) {
        ++n;
        int before = failures();
        std::printf("=== Тест %d. %s ===\n", n, c->name);
        std::fflush(stdout);
        c->fn();
        bool ok = failures() == before;
        passed += ok;
        std::printf("--- Тест %d: %s\n\n", n, ok ? "OK" : "FAIL");
        std::fflush(stdout);
    }
    std::printf("Итого: пройдено %d из %d\n", passed, n);
    return passed == n ? 0 : 1;
}

}

#define UT_JOIN2(a, b) a##b
#define UT_JOIN(a, b) UT_JOIN2(a, b)
#define TEST(title)                                                                       \
    static void UT_JOIN(ut_test_, __LINE__)();                                             \
    static ut::Case UT_JOIN(ut_case_, __LINE__) = {title, UT_JOIN(ut_test_, __LINE__), nullptr}; \
    static ut::Registrar UT_JOIN(ut_reg_, __LINE__)(&UT_JOIN(ut_case_, __LINE__));         \
    static void UT_JOIN(ut_test_, __LINE__)()
#define CHECK(cond, what) ut::check((cond), (what), #cond, __FILE__, __LINE__)
