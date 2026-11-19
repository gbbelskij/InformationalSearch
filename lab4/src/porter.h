#pragma once

#include <cstddef>
#include <cstring>

namespace porter {

struct Rule {
    const char* suffix;
    const char* repl;
};

class Stemmer {
public:
    size_t stem(char* word, size_t n) {
        if (n <= 2) return n;
        b = word;
        k = int(n) - 1;
        step1a();
        step1b();
        step1c();
        step2();
        step3();
        step4();
        step5();
        return size_t(k + 1);
    }

private:
    char* b = nullptr;
    int k = 0;
    int j = 0;

    bool cons(int i) const {
        char c = b[i];
        if (c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u') return false;
        if (c == 'y') return i == 0 || !cons(i - 1);
        return true;
    }

    int measure(int end) const {
        int n = 0;
        bool prev_vowel = false;
        for (int i = 0; i <= end; ++i) {
            bool c = cons(i);
            if (c && prev_vowel) ++n;
            prev_vowel = !c;
        }
        return n;
    }

    bool has_vowel(int end) const {
        for (int i = 0; i <= end; ++i)
            if (!cons(i)) return true;
        return false;
    }

    bool double_cons(int i) const { return i >= 1 && b[i] == b[i - 1] && cons(i); }

    bool cvc(int i) const {
        if (i < 2 || !cons(i) || cons(i - 1) || !cons(i - 2)) return false;
        return b[i] != 'w' && b[i] != 'x' && b[i] != 'y';
    }

    bool ends(const char* s) {
        int len = int(std::strlen(s));
        if (len > k + 1 || std::memcmp(b + k - len + 1, s, size_t(len)) != 0) return false;
        j = k - len;
        return true;
    }

    void set_to(const char* s) {
        int len = int(std::strlen(s));
        std::memcpy(b + j + 1, s, size_t(len));
        k = j + len;
    }

    int longest(const Rule* rules, size_t count) {
        int best = -1, best_len = 0;
        for (size_t i = 0; i < count; ++i) {
            int len = int(std::strlen(rules[i].suffix));
            if (len > best_len && ends(rules[i].suffix)) {
                best = int(i);
                best_len = len;
            }
        }
        if (best >= 0) j = k - best_len;
        return best;
    }

    void step1a() {
        if (ends("sses")) set_to("ss");
        else if (ends("ies")) set_to("i");
        else if (ends("ss")) return;
        else if (ends("s")) set_to("");
    }

    void step1b() {
        if (ends("eed")) {
            if (measure(j) > 0) set_to("ee");
            return;
        }
        if (!((ends("ed") || ends("ing")) && has_vowel(j))) return;
        k = j;
        if (ends("at")) set_to("ate");
        else if (ends("bl")) set_to("ble");
        else if (ends("iz")) set_to("ize");
        else if (double_cons(k) && b[k] != 'l' && b[k] != 's' && b[k] != 'z') --k;
        else if (measure(k) == 1 && cvc(k)) {
            j = k;
            set_to("e");
        }
    }

    void step1c() {
        if (ends("y") && has_vowel(j)) b[k] = 'i';
    }

    void step2() {
        static const Rule rules[] = {
            {"ational", "ate"}, {"tional", "tion"}, {"enci", "ence"},  {"anci", "ance"},   {"izer", "ize"},
            {"abli", "able"},   {"alli", "al"},     {"entli", "ent"},  {"eli", "e"},       {"ousli", "ous"},
            {"ization", "ize"}, {"ation", "ate"},   {"ator", "ate"},   {"alism", "al"},    {"iveness", "ive"},
            {"fulness", "ful"}, {"ousness", "ous"}, {"aliti", "al"},   {"iviti", "ive"},   {"biliti", "ble"},
        };
        int r = longest(rules, sizeof rules / sizeof rules[0]);
        if (r >= 0 && measure(j) > 0) set_to(rules[r].repl);
    }

    void step3() {
        static const Rule rules[] = {
            {"icate", "ic"}, {"ative", ""}, {"alize", "al"}, {"iciti", "ic"}, {"ical", "ic"}, {"ful", ""}, {"ness", ""},
        };
        int r = longest(rules, sizeof rules / sizeof rules[0]);
        if (r >= 0 && measure(j) > 0) set_to(rules[r].repl);
    }

    void step4() {
        static const Rule rules[] = {
            {"al", ""},   {"ance", ""}, {"ence", ""}, {"er", ""},  {"ic", ""},  {"able", ""}, {"ible", ""},
            {"ant", ""},  {"ement", ""}, {"ment", ""}, {"ent", ""}, {"ion", ""}, {"ou", ""},   {"ism", ""},
            {"ate", ""},  {"iti", ""},  {"ous", ""},  {"ive", ""}, {"ize", ""},
        };
        int r = longest(rules, sizeof rules / sizeof rules[0]);
        if (r < 0 || measure(j) <= 1) return;
        if (std::strcmp(rules[r].suffix, "ion") == 0 && (j < 0 || (b[j] != 's' && b[j] != 't'))) return;
        k = j;
    }

    void step5() {
        if (b[k] == 'e') {
            int m = measure(k - 1);
            if (m > 1 || (m == 1 && !cvc(k - 1))) --k;
        }
        if (b[k] == 'l' && double_cons(k) && measure(k) > 1) --k;
    }
};

inline bool stemmable(const char* s, size_t n) {
    for (size_t i = 0; i < n; ++i)
        if (s[i] < 'a' || s[i] > 'z') return false;
    return true;
}

inline size_t stem(char* word, size_t n) {
    if (!stemmable(word, n)) return n;
    Stemmer st;
    return st.stem(word, n);
}

}
