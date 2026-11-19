#include "index_builder.h"
#include "index_reader.h"
#include "porter.h"
#include "query.h"
#include "unittest.h"

#include <vector>

static std::string stem(const std::string& w) {
    char buf[256];
    std::memcpy(buf, w.data(), w.size());
    size_t n = porter::stem(buf, w.size());
    return std::string(buf, n);
}

static bool stems_ok(const char* const (*pairs)[2], size_t n) {
    bool ok = true;
    std::string line;
    for (size_t i = 0; i < n; ++i) {
        std::string got = stem(pairs[i][0]);
        if (got != pairs[i][1]) {
            ut::note("ожидалось %s -> %s, получено %s", pairs[i][0], pairs[i][1], got.c_str());
            ok = false;
        }
        line += std::string(i ? ", " : "") + pairs[i][0] + "->" + got;
    }
    ut::note("%s", line.c_str());
    return ok;
}

#define STEMS(...)                                          \
    do {                                                    \
        static const char* const p[][2] = {__VA_ARGS__};    \
        ok = stems_ok(p, sizeof p / sizeof p[0]);           \
    } while (0)

TEST("Шаг 1a: множественное число") {
    bool ok;
    STEMS({"caresses", "caress"}, {"ponies", "poni"}, {"ties", "ti"}, {"caress", "caress"}, {"cats", "cat"});
    CHECK(ok, "sses -> ss, ies -> i, ss без изменений, s отбрасывается");
}

TEST("Шаг 1b: -eed, -ed, -ing и восстановление основы") {
    bool ok;
    STEMS({"feed", "feed"}, {"agreed", "agre"}, {"plastered", "plaster"}, {"bled", "bled"}, {"motoring", "motor"},
          {"sing", "sing"}, {"conflated", "conflat"}, {"troubled", "troubl"}, {"sized", "size"},
          {"hopping", "hop"}, {"falling", "fall"}, {"filing", "file"}, {"hiking", "hike"});
    CHECK(ok, "окончание снимается, только если в основе есть гласная; at/bl/iz + e, двойная согласная, cvc + e");
}

TEST("Шаг 1c: y -> i") {
    bool ok;
    STEMS({"happy", "happi"}, {"sky", "sky"});
    CHECK(ok, "y заменяется на i, только если в основе есть гласная");
}

TEST("Шаги 2 и 3: двойные суффиксы при m > 0") {
    bool ok;
    STEMS({"relational", "relat"}, {"conditional", "condit"}, {"rational", "ration"}, {"digitizer", "digit"},
          {"operator", "oper"}, {"triplicate", "triplic"}, {"formative", "form"}, {"hopeful", "hope"},
          {"goodness", "good"}, {"generalization", "gener"});
    CHECK(ok, "ational -> ate, izer -> ize, icate -> ic, ative и ness отбрасываются");
}

TEST("Шаги 4 и 5: суффиксы при m > 1, конечная e и ll") {
    bool ok;
    STEMS({"revival", "reviv"}, {"allowance", "allow"}, {"adjustable", "adjust"}, {"adoption", "adopt"},
          {"communism", "commun"}, {"probate", "probat"}, {"rate", "rate"}, {"cease", "ceas"},
          {"controlling", "control"}, {"rolling", "roll"});
    CHECK(ok, "ion снимается только после s/t; e отбрасывается при m > 1 или m = 1 без cvc; ll -> l при m > 1");
}

TEST("Эталонный словарь: 77 слов из tests/porter_cases.tsv") {
    std::string data = ut::read_file("tests/porter_cases.tsv");
    size_t total = 0, bad = 0, pos = 0;
    while (pos < data.size()) {
        size_t nl = data.find('\n', pos);
        if (nl == std::string::npos) nl = data.size();
        std::string line = data.substr(pos, nl - pos);
        pos = nl + 1;
        size_t tab = line.find('\t');
        if (tab == std::string::npos) continue;
        ++total;
        std::string word = line.substr(0, tab), want = line.substr(tab + 1), got = stem(word);
        if (got != want) {
            ++bad;
            ut::note("ожидалось %s -> %s, получено %s", word.c_str(), want.c_str(), got.c_str());
        }
    }
    ut::note("слов %zu, расхождений %zu", total, bad);
    CHECK(total == 77 && bad == 0, "все 77 слов дают эталонные основы");
}

TEST("Что стеммер не меняет") {
    CHECK(stem("\xd0\xbc\xd0\xbe\xd1\x81\xd0\xba\xd0\xb2\xd0\xb0") == "\xd0\xbc\xd0\xbe\xd1\x81\xd0\xba\xd0\xb2\xd0\xb0",
          "кириллица (москва)");
    CHECK(stem("10am") == "10am" && stem("19th") == "19th", "токены с цифрами");
    CHECK(stem("caf\xc3\xa9") == "caf\xc3\xa9", "слова с диакритикой");
    CHECK(stem("is") == "is" && stem("a") == "a", "слова из 1–2 букв");
    CHECK(!porter::stemmable("Museums", 7) && porter::stemmable("museums", 7),
          "стеммируются только строчные латинские a–z (токены приходят уже в нижнем регистре)");
}

static const std::vector<std::string> DOCS = {"museum in town", "two museums", "skiing and skis", "news today",
                                              "new bridge"};

static void build(const std::string& path, bool stem_terms) {
    IndexBuilder b(stem_terms);
    for (size_t i = 0; i < DOCS.size(); ++i) {
        std::string title = "Doc " + std::to_string(i), url = "https://example.org/" + std::to_string(i);
        b.add_text(DOCS[i].data(), DOCS[i].size(), title.data(), title.size(), url.data(), url.size());
    }
    b.write(path.c_str());
}

static std::string dump(const Index& ix) {
    std::string out;
    for (uint32_t k = 0; k < ix.terms; ++k) {
        uint16_t len;
        const unsigned char* t = ix.str(get_u32(ix.term_at(k)), len);
        out += std::string(k ? " " : "") + std::string(reinterpret_cast<const char*>(t), len) + ":";
        const unsigned char* p = ix.postings_of(k);
        for (uint32_t i = 0; i < ix.df(k); ++i) out += (i ? "," : "") + std::to_string(get_u32(p + 4 * i));
    }
    return out;
}

TEST("Индекс со стеммингом (IndexBuilder с флагом stem)") {
    ut::TempDir tmp;
    build(tmp.path("stem.bin"), true);
    build(tmp.path("plain.bin"), false);
    Index s, p;
    bool loaded = s.load(tmp.path("stem.bin").c_str(), "test") && p.load(tmp.path("plain.bin").c_str(), "test");
    std::string ds = loaded ? dump(s) : "", dp = loaded ? dump(p) : "";
    ut::note("со стеммингом: %s", ds.c_str());
    ut::note("без стемминга: %s", dp.c_str());
    CHECK(loaded && s.flags == fmt::FLAG_STEMMED && p.flags == 0, "флаг 1 в заголовке индекса со стеммингом, 0 — без");
    CHECK(ds == "and:2 bridg:4 in:0 museum:0,1 new:3,4 ski:2 todai:3 town:0 two:1",
          "museum/museums, skiing/skis, news/new — по одному терму, списки объединены");
    CHECK(dp.find("museum:0 museums:1") != std::string::npos && dp.find("news:3") != std::string::npos,
          "без стемминга формы — разные термы");
}

static std::string search(const Index& ix, const char* q, std::string* parsed) {
    Node* root = parse_query(q, std::strlen(q), ix.flags & fmt::FLAG_STEMMED);
    List res;
    Vec<char> text;
    if (root) {
        eval(ix, root, res);
        show(root, text);
    }
    destroy(root);
    *parsed = std::string(text.data ? text.data : "", text.size);
    std::string out;
    for (size_t i = 0; i < res.size; ++i) out += (i ? "," : "") + std::to_string(res[i]);
    return out;
}

TEST("Поиск: стемминг запроса по флагу индекса") {
    ut::TempDir tmp;
    build(tmp.path("stem.bin"), true);
    build(tmp.path("plain.bin"), false);
    Index s, p;
    s.load(tmp.path("stem.bin").c_str(), "test");
    p.load(tmp.path("plain.bin").c_str(), "test");
    std::string pa, pb, pc, pd, pe, pf;
    std::string a = search(s, "Museums", &pa), b = search(s, "skiing", &pb), c = search(s, "news", &pc);
    std::string d = search(s, "(skis || bridge) !museum", &pd);
    std::string e = search(p, "Museums", &pe), f = search(p, "news", &pf);
    ut::note("со стеммингом: [%s] -> %s, [%s] -> %s, [%s] -> %s, [%s] -> %s", pa.c_str(), a.c_str(), pb.c_str(),
             b.c_str(), pc.c_str(), c.c_str(), pd.c_str(), d.c_str());
    ut::note("без стемминга: [%s] -> %s, [%s] -> %s", pe.c_str(), e.c_str(), pf.c_str(), f.c_str());
    CHECK(pa == "museum" && a == "0,1" && pb == "ski" && b == "2" && pc == "new" && c == "3,4",
          "слово запроса приводится к основе и находит все формы");
    CHECK(pd == "(ski || bridg) && !museum" && d == "2,4", "операторы булева поиска работают и со стеммингом");
    CHECK(pe == "museums" && e == "1" && pf == "news" && f == "3", "по индексу без стемминга — только точная форма");
}

TEST("Утилиты командной строки: stem, indexer -s, search") {
    ut::TempDir tmp;
    int st1 = -1, st2 = -1, st3 = -1;
    std::string out1 = ut::run("printf 'Skiing museums 10am' | build/stem", &st1);
    std::string list;
    for (size_t i = 0; i < DOCS.size(); ++i)
        list += tmp.write(std::to_string(i) + ".txt", DOCS[i]) + "\tDoc " + std::to_string(i) + "\thttps://x/\n";
    std::string docs = tmp.write("docs.tsv", list), index = tmp.path("stem.bin");
    std::string out2 = ut::run("../lab7/build/indexer -s " + index + " < " + docs, &st2);
    std::string out3 = ut::run("echo museums | ../lab8/build/search " + index + " 2>/dev/null", &st3);
    ut::note("stem: %s", ut::escape(out1).c_str());
    CHECK(st1 == 0 && out1 == "skiing\tski\nmuseums\tmuseum\n10am\t10am\n", "build/stem: токен и его основа");
    CHECK(st2 == 0 && out2.find("stemmed\t1\n") != std::string::npos, "indexer -s: в статистике stemmed 1");
    CHECK(st3 == 0 && out3.find("parsed: museum\n") != std::string::npos && out3.find("found: 2\n") != std::string::npos,
          "search по индексу со стеммингом: museums -> museum, найдено 2");
}

int main() {
    if (!tok::init()) {
        std::fprintf(stderr, "locale C.UTF-8 is not available\n");
        return 2;
    }
    return ut::run_all();
}
