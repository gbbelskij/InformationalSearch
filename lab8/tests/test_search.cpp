#include "index_builder.h"
#include "query.h"
#include "unittest.h"

#include <functional>
#include <random>
#include <set>
#include <vector>

typedef std::vector<uint32_t> Ids;

static const std::vector<std::string> SMALL = {"red car", "yellow car", "red bike", "green car", "yellow red bus"};

static void build(const std::vector<std::string>& texts, const std::string& path,
                  const std::vector<std::string>& titles = {}) {
    IndexBuilder b(false);
    for (size_t i = 0; i < texts.size(); ++i) {
        std::string title = i < titles.size() ? titles[i] : "Doc " + std::to_string(i);
        std::string url = "https://example.org/" + std::to_string(i);
        b.add_text(texts[i].data(), texts[i].size(), title.data(), title.size(), url.data(), url.size());
    }
    b.write(path.c_str());
}

static void fill(List& l, const Ids& v) {
    l.size = 0;
    for (uint32_t x : v) l.push(x);
}

static Ids ids(const List& l) {
    return Ids(l.data, l.data + l.size);
}

static std::string parsed(const char* q) {
    Node* root = parse_query(q, std::strlen(q), false);
    Vec<char> text;
    if (root) show(root, text);
    destroy(root);
    return std::string(text.data ? text.data : "", text.size);
}

static Ids search(const Index& ix, const std::string& q, std::string* how = nullptr) {
    Node* root = parse_query(q.data(), q.size(), false);
    List res;
    Vec<char> text;
    if (root) {
        eval(ix, root, res);
        show(root, text);
    }
    destroy(root);
    if (how) *how = std::string(text.data ? text.data : "", text.size);
    return ids(res);
}

static std::string str(const Ids& v) {
    std::string out = "[";
    for (size_t i = 0; i < v.size(); ++i) out += (i ? "," : "") + std::to_string(v[i]);
    return out + "]";
}

static void expect(const Index& ix, const std::string& q, const Ids& want, const char* want_parsed = nullptr) {
    std::string how;
    Ids got = search(ix, q, &how);
    std::string line = "[ " + ut::escape(q) + " ] -> " + str(got);
    bool ok = got == want;
    if (want_parsed) {
        line += ", разобран как [ " + how + " ]";
        ok = ok && how == want_parsed;
    }
    CHECK(ok, line.c_str());
}

TEST("Операции над списками: пересечение, объединение, разность") {
    List a, b, e, out;
    fill(a, {1, 3, 5, 7});
    fill(b, {3, 4, 5, 8});
    intersect(a, b, out);
    CHECK(ids(out) == Ids({3, 5}), "{1,3,5,7} И {3,4,5,8} = {3,5}");
    unite(a, b, out);
    CHECK(ids(out) == Ids({1, 3, 4, 5, 7, 8}), "{1,3,5,7} ИЛИ {3,4,5,8} = {1,3,4,5,7,8}");
    subtract(a, b, out);
    CHECK(ids(out) == Ids({1, 7}), "{1,3,5,7} без {3,4,5,8} = {1,7}");
    intersect(a, e, out);
    bool empty_and = out.size == 0;
    unite(a, e, out);
    bool empty_or = ids(out) == ids(a);
    subtract(e, a, out);
    CHECK(empty_and && empty_or && out.size == 0, "с пустым списком: И — пусто, ИЛИ — сам список, разность — пусто");
    List c, d;
    fill(c, {2, 4});
    fill(d, {1, 3, 5});
    intersect(c, d, out);
    bool disjoint = out.size == 0;
    unite(c, d, out);
    CHECK(disjoint && ids(out) == Ids({1, 2, 3, 4, 5}), "непересекающиеся списки: И пусто, ИЛИ — слияние по порядку");
    subtract(a, a, out);
    bool self = out.size == 0;
    intersect(a, a, out);
    CHECK(self && ids(out) == ids(a), "список с самим собой: разность пуста, пересечение — он же");
}

TEST("Разбор запроса: приоритеты и канонический вид") {
    CHECK(parsed("red car") == "red && car", "пробел — И");
    CHECK(parsed("red && car || bike") == "(red && car) || bike", "И связывает сильнее ИЛИ");
    CHECK(parsed("red && (car || bike)") == "red && (car || bike)", "скобки меняют порядок");
    CHECK(parsed("!red car") == "!red && car", "НЕ относится к ближайшему операнду");
    CHECK(parsed("!(red || car)") == "!(red || car)", "НЕ перед скобкой");
    CHECK(parsed("(a b) c") == "a && b && c", "вложенные однотипные операции сливаются");
    CHECK(parsed("Hewlett-Packard") == "hewlett && packard", "слово с дефисом — И частей, регистр понижен");
}

TEST("Операторы И, ИЛИ, НЕ, скобки на маленьком корпусе") {
    ut::TempDir tmp;
    build(SMALL, tmp.path("index.bin"));
    Index ix;
    ix.load(tmp.path("index.bin").c_str(), "test");
    ut::note("документы: 0 red car, 1 yellow car, 2 red bike, 3 green car, 4 yellow red bus");
    expect(ix, "red car", {0});
    expect(ix, "red && car", {0});
    expect(ix, "(red || yellow) car", {0, 1});
    expect(ix, "car !red", {1, 3});
    expect(ix, "red || bike", {0, 2, 4});
    expect(ix, "!car", {2, 4});
    expect(ix, "RED Car", {0});
    expect(ix, "red !yellow !bike", {0});
    expect(ix, "!red !yellow", {3});
    expect(ix, "missing", {});
    expect(ix, "!missing", {0, 1, 2, 3, 4});
    expect(ix, "red && missing", {});
}

TEST("Устойчивость разбора запроса") {
    ut::TempDir tmp;
    build(SMALL, tmp.path("index.bin"));
    Index ix;
    ix.load(tmp.path("index.bin").c_str(), "test");
    expect(ix, "  red   &&car ", {0}, "red && car");
    expect(ix, "red & car", {0}, "red && car");
    expect(ix, "red | yellow", {0, 1, 2, 4}, "red || yellow");
    expect(ix, "(red || yellow", {0, 1, 2, 4}, "red || yellow");
    expect(ix, "red) car", {0}, "red && car");
    expect(ix, "&& red ||", {0, 2, 4}, "red");
    expect(ix, "!!red", {0, 2, 4}, "red");
    expect(ix, "red !", {0, 2, 4}, "red");
    expect(ix, "()", {}, "");
    expect(ix, "", {}, "");
    expect(ix, "\xe2\x80\x94 , .", {}, "");
    expect(ix, "(red||yellow)car", {0, 1}, "(red || yellow) && car");
    expect(ix, "red && car || bike", {0, 2}, "(red && car) || bike");
    expect(ix, "!(red || yellow)", {3}, "!(red || yellow)");
    expect(ix, "Red-Car", {0}, "red && car");
}

TEST("Страницы выдачи и строка запроса веб-сервиса") {
    size_t b, e;
    page_range(120, 0, 50, b, e);
    bool p1 = b == 0 && e == 50;
    page_range(120, 100, 50, b, e);
    bool p3 = b == 100 && e == 120;
    page_range(120, 0, 0, b, e);
    bool all = b == 0 && e == 120;
    page_range(120, 500, 50, b, e);
    bool past = b == e;
    CHECK(p1 && p3 && all && past, "первая страница 1–50, третья 101–120, лимит 0 — все, смещение за концом — пусто");
    char r1[] = "100\t50\tred car", r2[] = "bad\tbad\tred", r3[] = "plain query";
    size_t off = 0, lim = 50;
    const char* q1 = parse_request(r1, off, lim);
    bool ok1 = off == 100 && lim == 50 && std::strcmp(q1, "red car") == 0;
    off = 7, lim = 50;
    const char* q2 = parse_request(r2, off, lim);
    bool ok2 = off == 0 && lim == 50 && std::strcmp(q2, "red") == 0;
    off = 0, lim = 50;
    const char* q3 = parse_request(r3, off, lim);
    CHECK(ok1, "\"100<TAB>50<TAB>red car\": смещение 100, лимит 50, запрос red car");
    CHECK(ok2, "нечисловые смещение и лимит — значения по умолчанию");
    CHECK(q3 == r3 && off == 0 && lim == 50, "строка без табуляций — весь текст как запрос");
}

struct Q {
    int kind;
    int word;
    std::vector<Q> kids;
};

TEST("300 случайных запросов против вычисления на множествах") {
    ut::TempDir tmp;
    std::mt19937 rnd(7);
    auto chance = [&](double p) { return std::uniform_real_distribution<double>(0, 1)(rnd) < p; };
    auto pick = [&](int n) { return int(rnd() % unsigned(n)); };
    std::vector<std::set<uint32_t>> sets(12);
    std::vector<std::string> texts;
    for (uint32_t d = 0; d < 300; ++d) {
        std::string t;
        for (int w = 0; w < 12; ++w)
            if (chance(0.3)) {
                t += "w" + std::to_string(w) + " ";
                sets[w].insert(d);
            }
        texts.push_back(t + "doc" + std::to_string(d));
    }
    build(texts, tmp.path("index.bin"));
    Index ix;
    ix.load(tmp.path("index.bin").c_str(), "test");

    std::function<Q(int)> gen = [&](int depth) {
        if (depth == 0 || chance(0.3)) return Q{0, pick(12), {}};
        int op = 1 + pick(3);
        Q q{op, 0, {}};
        int n = op == 3 ? 1 : 2 + pick(2);
        for (int i = 0; i < n; ++i) q.kids.push_back(gen(depth - 1));
        return q;
    };
    std::function<std::string(const Q&)> render;
    auto wrap = [&](const Q& q) {
        if (q.kind == 0) return render(q);
        return std::string(chance(0.5) ? "(" : "( ") + render(q) + (chance(0.5) ? ")" : " )");
    };
    render = [&](const Q& q) -> std::string {
        if (q.kind == 0) return (chance(0.2) ? "W" : "w") + std::to_string(q.word);
        if (q.kind == 3) return "!" + wrap(q.kids[0]);
        static const char* and_seps[] = {" ", " && ", "&&", " & "};
        static const char* or_seps[] = {" || ", "||", " | "};
        std::string out;
        for (size_t i = 0; i < q.kids.size(); ++i) {
            if (i) out += q.kind == 1 ? and_seps[pick(4)] : or_seps[pick(3)];
            out += wrap(q.kids[i]);
        }
        return out;
    };
    std::set<uint32_t> every;
    for (uint32_t d = 0; d < 300; ++d) every.insert(d);
    std::function<std::set<uint32_t>(const Q&)> ref = [&](const Q& q) {
        if (q.kind == 0) return sets[q.word];
        if (q.kind == 3) {
            std::set<uint32_t> inner = ref(q.kids[0]), out;
            for (uint32_t d : every)
                if (!inner.count(d)) out.insert(d);
            return out;
        }
        std::set<uint32_t> acc = ref(q.kids[0]);
        for (size_t i = 1; i < q.kids.size(); ++i) {
            std::set<uint32_t> part = ref(q.kids[i]), out;
            for (uint32_t d : every)
                if (q.kind == 1 ? acc.count(d) && part.count(d) : acc.count(d) || part.count(d)) out.insert(d);
            acc = out;
        }
        return acc;
    };
    int bad = 0;
    for (int i = 0; i < 300; ++i) {
        Q q = gen(3);
        std::string text = render(q);
        std::string how;
        Ids got = search(ix, text, &how);
        std::set<uint32_t> want = ref(q);
        if (got != Ids(want.begin(), want.end())) {
            ++bad;
            ut::note("расхождение: [ %s ]", text.c_str());
        }
        if (i < 3) ut::note("пример: [ %s ] -> разобран как [ %s ], найдено %zu", text.c_str(), how.c_str(), got.size());
    }
    CHECK(bad == 0, "выдача совпала с вычислением на множествах во всех 300 запросах");
}

static std::string block_line(const std::string& out, const char* key) {
    size_t p = out.find(key);
    if (p == std::string::npos) return "";
    size_t e = out.find('\n', p);
    return out.substr(p + std::strlen(key), e - p - std::strlen(key));
}

TEST("Утилита командной строки build/search") {
    ut::TempDir tmp;
    std::vector<std::string> texts;
    for (int i = 0; i < 120; ++i) texts.push_back("common word" + std::to_string(i));
    texts.push_back("Krak\xc3\xb3w old town");
    std::vector<std::string> titles(120, "Doc");
    titles.push_back("Krak\xc3\xb3w");
    build(texts, tmp.path("index.bin"), titles);
    std::string index = tmp.path("index.bin");
    int s1 = -1, s2 = -1, s3 = -1, s4 = -1, s5 = -1, s6 = -1;
    std::string out1 = ut::run("echo common | build/search " + index + " 2>/dev/null", &s1);
    std::string out2 = ut::run("echo common | build/search " + index + " -o 100 2>/dev/null", &s2);
    std::string out3 = ut::run("printf '0\\t1\\ttown\\n' | build/search " + index + " -p 2>/dev/null", &s3);
    ut::run("build/search " + tmp.path("none.bin") + " < /dev/null 2>/dev/null", &s4);
    ut::run("build/search 2>/dev/null", &s5);
    ut::run("build/search " + index + " -n many < /dev/null 2>/dev/null", &s6);
    size_t lines1 = 0, lines2 = 0;
    for (char c : out1) lines1 += c == '\n';
    for (char c : out2) lines2 += c == '\n';
    ut::note("-p: %s", ut::escape(out3).c_str());
    CHECK(s1 == 0 && block_line(out1, "found: ") == "120" && block_line(out1, "parsed: ") == "common" &&
              lines1 == 4 + 50 + 1 && out1.find("\n1\t0\tDoc\thttps://example.org/0\n") != std::string::npos,
          "блок выдачи: query/parsed/found/time_us, 50 строк «ранг, docID, заголовок, URL», пустая строка");
    CHECK(s2 == 0 && lines2 == 4 + 20 + 1 && out2.find("\n101\t100\t") != std::string::npos,
          "-o 100: результаты 101–120");
    CHECK(s3 == 0 && out3.find("\n1\t120\tKrak\xc3\xb3w\thttps://example.org/120\n") != std::string::npos,
          "режим -p: смещение и лимит в строке запроса, заголовок и URL в UTF-8");
    CHECK(s4 == 1 && s5 == 2 && s6 == 2, "нет файла индекса — код 1; без аргументов и с нечисловым лимитом — код 2");
}

int main() {
    if (!tok::init()) {
        std::fprintf(stderr, "locale C.UTF-8 is not available\n");
        return 2;
    }
    return ut::run_all();
}
