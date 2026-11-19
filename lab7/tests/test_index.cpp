#include "index_builder.h"
#include "index_reader.h"
#include "unittest.h"

#include <cmath>
#include <map>
#include <random>
#include <vector>

typedef std::map<std::string, std::vector<uint32_t>> Inverted;

struct Doc {
    std::string title, url, text;
};

struct Parsed {
    std::string magic;
    uint64_t version = 0, header = 0, flags = 0, n = 0, t = 0, doc_rec = 0, term_rec = 0, p = 0;
    uint64_t docs_off = 0, terms_off = 0, postings_off = 0, strings_off = 0, size = 0;
    std::vector<Doc> docs;
    std::vector<uint64_t> doc_tokens;
    std::vector<std::pair<std::string, std::vector<uint32_t>>> terms;
};

static uint64_t le(const std::string& b, uint64_t off, int bytes) {
    uint64_t v = 0;
    for (int i = bytes - 1; i >= 0; --i) v = (v << 8) | static_cast<unsigned char>(b[off + i]);
    return v;
}

static Parsed parse(const std::string& path) {
    std::string b = ut::read_file(path);
    Parsed x;
    x.size = b.size();
    if (b.size() < 64) return x;
    x.magic = b.substr(0, 4);
    x.version = le(b, 4, 2);
    x.header = le(b, 6, 2);
    x.flags = le(b, 8, 4);
    x.n = le(b, 12, 4);
    x.t = le(b, 16, 4);
    x.doc_rec = le(b, 20, 2);
    x.term_rec = le(b, 22, 2);
    x.p = le(b, 24, 8);
    x.docs_off = le(b, 32, 8);
    x.terms_off = le(b, 40, 8);
    x.postings_off = le(b, 48, 8);
    x.strings_off = le(b, 56, 8);
    auto str = [&](uint64_t off) { return b.substr(x.strings_off + off + 2, le(b, x.strings_off + off, 2)); };
    for (uint64_t i = 0; i < x.n; ++i) {
        uint64_t r = x.docs_off + i * x.doc_rec;
        x.docs.push_back({str(le(b, r, 4)), str(le(b, r + 4, 4)), ""});
        x.doc_tokens.push_back(le(b, r + 8, 4));
    }
    for (uint64_t i = 0; i < x.t; ++i) {
        uint64_t r = x.terms_off + i * x.term_rec;
        uint64_t df = le(b, r + 4, 4), first = le(b, r + 8, 8);
        std::vector<uint32_t> docs;
        for (uint64_t k = 0; k < df; ++k) docs.push_back(uint32_t(le(b, x.postings_off + 4 * (first + k), 4)));
        x.terms.push_back({str(le(b, r, 4)), docs});
    }
    return x;
}

static Inverted expected(const std::vector<Doc>& docs) {
    Inverted inv;
    for (size_t i = 0; i < docs.size(); ++i) {
        std::string word;
        for (size_t k = 0; k <= docs[i].text.size(); ++k) {
            char c = k < docs[i].text.size() ? docs[i].text[k] : ' ';
            if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
            if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
                word += c;
            } else if (!word.empty()) {
                std::vector<uint32_t>& v = inv[word];
                if (v.empty() || v.back() != i) v.push_back(uint32_t(i));
                word.clear();
            }
        }
    }
    return inv;
}

static IndexStats build(const std::vector<Doc>& docs, const std::string& path, bool* ok = nullptr) {
    IndexBuilder b(false);
    for (const Doc& d : docs)
        b.add_text(d.text.data(), d.text.size(), d.title.data(), d.title.size(), d.url.data(), d.url.size());
    bool written = b.write(path.c_str());
    if (ok) *ok = written;
    return b.stats;
}

static std::string show(const std::vector<std::pair<std::string, std::vector<uint32_t>>>& terms) {
    std::string out;
    for (size_t i = 0; i < terms.size(); ++i) {
        out += (i ? " " : "") + ut::escape(terms[i].first) + ":";
        for (size_t k = 0; k < terms[i].second.size(); ++k) out += (k ? "," : "") + std::to_string(terms[i].second[k]);
    }
    return out;
}

static bool same(const Parsed& x, const Inverted& inv) {
    if (x.terms.size() != inv.size()) return false;
    size_t i = 0;
    for (const auto& kv : inv) {
        if (x.terms[i].first != kv.first || x.terms[i].second != kv.second) return false;
        ++i;
    }
    return true;
}

static std::vector<Doc> docs_of(const std::vector<std::string>& texts) {
    std::vector<Doc> docs;
    for (size_t i = 0; i < texts.size(); ++i)
        docs.push_back({"Doc " + std::to_string(i), "https://example.org/" + std::to_string(i), texts[i]});
    return docs;
}

TEST("Маленький корпус: словарь, списки документов, заголовок файла") {
    ut::TempDir tmp;
    std::vector<Doc> docs = docs_of({"The cat sat on the mat.", "A dog and a cat.", "Dogs: 2 dogs, 1 mat!"});
    bool ok;
    IndexStats st = build(docs, tmp.path("index.bin"), &ok);
    Parsed x = parse(tmp.path("index.bin"));
    Inverted inv = expected(docs);
    ut::note("индекс: %s", show(x.terms).c_str());
    CHECK(ok, "файл записан");
    CHECK(same(x, inv), "словарь и списки совпадают с эталоном, термы по возрастанию байтов");
    CHECK(x.magic == "BIDX" && x.version == 1 && x.header == 64 && x.flags == 0 && x.doc_rec == 12 &&
              x.term_rec == 16,
          "заголовок: BIDX, версия 1, размер 64, флаги 0, записи 12 и 16 байт");
    size_t p = 0;
    for (const auto& kv : inv) p += kv.second.size();
    CHECK(x.n == 3 && x.t == inv.size() && x.p == p, "N, T, P в заголовке");
    CHECK(x.docs_off == 64 && x.terms_off == 64 + 12 * 3 && x.postings_off == x.terms_off + 16 * x.t &&
              x.strings_off == x.postings_off + 4 * x.p,
          "разделы идут подряд по смещениям из заголовка");
    CHECK(st.terms == x.t && st.postings == x.p && st.index_bytes == x.size && st.docs == 3 && st.tokens == 16,
          "статистика сборщика совпадает с файлом");
}

TEST("Регистр, Юникод, повторы в документе") {
    ut::TempDir tmp;
    build(docs_of({"Paris PARIS paris Stra\xc3\x9f" "e",
                   "\xd0\x9c\xd0\xbe\xd1\x81\xd0\xba\xd0\xb2\xd0\xb0 \xd0\x9c\xd0\x9e\xd0\xa1\xd0\x9a\xd0\x92\xd0\x90",
                   "paris"}),
          tmp.path("index.bin"));
    Parsed x = parse(tmp.path("index.bin"));
    ut::note("индекс: %s", show(x.terms).c_str());
    CHECK(show(x.terms) == "paris:0,2 stra\xc3\x9f" "e:0 \xd0\xbc\xd0\xbe\xd1\x81\xd0\xba\xd0\xb2\xd0\xb0:1",
          "регистр понижен (латиница, кириллица), документ в списке один раз, порядок по байтам UTF-8");
}

TEST("Прямой индекс: заголовки, ссылки, число токенов") {
    ut::TempDir tmp;
    std::vector<Doc> docs = {{"Krak\xc3\xb3w", "https://en.wikivoyage.org/wiki/Krak%C3%B3w", "Krak\xc3\xb3w is a city."},
                             {"\xd0\x9c\xd0\xbe\xd1\x81\xd0\xba\xd0\xb2\xd0\xb0", "https://ru.wikipedia.org/wiki/x",
                              "\xd0\x9c\xd0\xbe\xd1\x81\xd0\xba\xd0\xb2\xd0\xb0 \xe2\x80\x94 \xd1\x81\xd1\x82\xd0\xbe\xd0\xbb\xd0\xb8\xd1\x86\xd0\xb0"},
                             {"Empty", "https://x/empty", ""}};
    build(docs, tmp.path("index.bin"));
    Parsed x = parse(tmp.path("index.bin"));
    Index ix;
    bool loaded = ix.load(tmp.path("index.bin").c_str(), "test");
    bool reader_ok = loaded && ix.docs == 3;
    for (uint32_t i = 0; reader_ok && i < 3; ++i) {
        uint16_t tl, ul;
        const unsigned char* rec = ix.doc_at(i);
        const char* t = reinterpret_cast<const char*>(ix.str(get_u32(rec), tl));
        const char* u = reinterpret_cast<const char*>(ix.str(get_u32(rec + 4), ul));
        reader_ok = std::string(t, tl) == docs[i].title && std::string(u, ul) == docs[i].url &&
                    get_u32(rec + 8) == x.doc_tokens[i];
    }
    ut::note("документы: %s | %s | %s", ut::escape(x.docs[0].title).c_str(), ut::escape(x.docs[1].title).c_str(),
             x.docs[2].title.c_str());
    CHECK(x.docs.size() == 3 && x.docs[0].title == docs[0].title && x.docs[1].url == docs[1].url &&
              x.doc_tokens[0] == 4 && x.doc_tokens[1] == 2 && x.doc_tokens[2] == 0,
          "заголовки и ссылки в UTF-8, число токенов (пустой документ — 0)");
    CHECK(reader_ok, "Index::doc_at и Index::str читают те же данные");
}

TEST("Чтение индекса: двоичный поиск терма (Index::find)") {
    ut::TempDir tmp;
    build(docs_of({"Paris PARIS paris", "zoo", "paris zoo apple"}), tmp.path("index.bin"));
    Index ix;
    bool loaded = ix.load(tmp.path("index.bin").c_str(), "test");
    long paris = ix.find("paris", 5), zoo = ix.find("zoo", 3), apple = ix.find("apple", 5);
    CHECK(loaded && paris >= 0 && ix.df(size_t(paris)) == 2 && get_u32(ix.postings_of(size_t(paris))) == 0 &&
              get_u32(ix.postings_of(size_t(paris)) + 4) == 2,
          "paris: df 2, документы 0 и 2");
    CHECK(zoo >= 0 && ix.df(size_t(zoo)) == 2 && apple == 0, "zoo: df 2; apple — первый терм таблицы");
    CHECK(ix.find("missing", 7) == -1 && ix.find("", 0) == -1 && ix.find("parisx", 6) == -1 &&
              ix.find("pari", 4) == -1,
          "нет терма (и его префикса или продолжения) — -1");
}

TEST("Случайный корпус: перестройка хеш-таблицы, длинные списки") {
    ut::TempDir tmp;
    std::mt19937 rnd(1);
    std::uniform_real_distribution<double> u(0.0, 1.0);
    std::vector<std::string> texts;
    for (int i = 0; i < 400; ++i) {
        std::string t;
        for (int k = 0; k < 300; ++k) t += "w" + std::to_string(int(1.0 / std::pow(1.0 - u(rnd), 1.0 / 0.3))) + " ";
        texts.push_back(t + "doc" + std::to_string(i));
    }
    std::vector<Doc> docs = docs_of(texts);
    build(docs, tmp.path("index.bin"));
    Parsed x = parse(tmp.path("index.bin"));
    Inverted inv = expected(docs);
    size_t longest = 0;
    for (const auto& kv : inv) longest = kv.second.size() > longest ? kv.second.size() : longest;
    Index ix;
    bool find_ok = ix.load(tmp.path("index.bin").c_str(), "test");
    for (const auto& kv : inv) {
        long k = ix.find(kv.first.data(), kv.first.size());
        find_ok = find_ok && k >= 0 && ix.df(size_t(k)) == kv.second.size();
    }
    ut::note("термов %llu, записей %llu, самый длинный список %zu", (unsigned long long)x.t,
             (unsigned long long)x.p, longest);
    CHECK(x.t > 8192, "термов больше 8192: таблица из 4096 ячеек перестраивалась 3 раза (при заполнении на 50 %)");
    CHECK(same(x, inv), "весь индекс совпадает с эталоном");
    CHECK(find_ok, "Index::find находит каждый терм с правильным df");
}

TEST("Разбор строки списка документов") {
    char good[] = "a/1.txt\tTitle with spaces\thttps://x/1";
    char one_tab[] = "a/1.txt\tTitle";
    char no_tab[] = "only-path";
    char *path, *title, *url;
    bool ok = split_doc_line(good, path, title, url);
    CHECK(ok && std::strcmp(path, "a/1.txt") == 0 && std::strcmp(title, "Title with spaces") == 0 &&
              std::strcmp(url, "https://x/1") == 0,
          "путь, заголовок, URL через табуляцию");
    CHECK(!split_doc_line(one_tab, path, title, url) && !split_doc_line(no_tab, path, title, url),
          "строка без URL или без заголовка — ошибка");
}

TEST("Пустой корпус и ошибки") {
    ut::TempDir tmp;
    bool ok;
    build({}, tmp.path("empty.bin"), &ok);
    Parsed x = parse(tmp.path("empty.bin"));
    Index empty, junk, missing;
    bool empty_loaded = empty.load(tmp.path("empty.bin").c_str(), "test");
    std::string junk_path = tmp.write("junk.bin", std::string(120, 'x'));
    std::fflush(stdout);
    bool junk_loaded = junk.load(junk_path.c_str(), "test");
    bool missing_loaded = missing.load(tmp.path("none.bin").c_str(), "test");
    bool bad_write;
    build(docs_of({"a"}), tmp.path("no-such-dir/index.bin"), &bad_write);
    CHECK(ok && x.n == 0 && x.t == 0 && x.p == 0 && empty_loaded && empty.find("word", 4) == -1,
          "пустой список документов — корректный пустой индекс");
    CHECK(!junk_loaded && !missing_loaded, "чужой и несуществующий файл не открываются");
    CHECK(!bad_write, "запись в несуществующий каталог — ошибка");
}

TEST("Утилиты командной строки: indexer и lookup") {
    ut::TempDir tmp;
    std::string list = tmp.write("0.txt", "Paris PARIS paris") + "\tParis\thttps://x/0\n" +
                       tmp.write("1.txt", "\xd0\x9c\xd0\xbe\xd1\x81\xd0\xba\xd0\xb2\xd0\xb0") + "\tMoscow\thttps://x/1\n" +
                       tmp.write("2.txt", "paris zoo") + "\tZoo\thttps://x/2\n";
    std::string docs = tmp.write("docs.tsv", list), index = tmp.path("index.bin");
    int s1 = -1, s2 = -1, s3 = -1, s4 = -1, s5 = -1;
    std::string out1 = ut::run("build/indexer " + index + " < " + docs, &s1);
    std::string out2 = ut::run("printf 'PARIS \xd0\xbc\xd0\xbe\xd1\x81\xd0\xba\xd0\xb2\xd0\xb0 missing' | build/lookup " + index, &s2);
    std::string out3 = ut::run("printf '2\\n9\\n' | build/lookup " + index + " -d", &s3);
    std::string bad = tmp.write("bad.tsv", tmp.path("none.txt") + "\tt\tu\n");
    ut::run("build/indexer " + tmp.path("x.bin") + " < " + bad + " 2>/dev/null", &s4);
    std::string malformed = tmp.write("malformed.tsv", "only-path\n");
    ut::run("build/indexer " + tmp.path("y.bin") + " < " + malformed + " 2>/dev/null", &s5);
    ut::note("lookup: %s", ut::escape(out2).c_str());
    CHECK(s1 == 0 && out1.find("docs\t3\n") != std::string::npos && out1.find("terms\t3\n") != std::string::npos,
          "indexer: 3 документа, 3 терма в статистике");
    CHECK(s2 == 0 && out2 == "paris\t2\t0 2\n\xd0\xbc\xd0\xbe\xd1\x81\xd0\xba\xd0\xb2\xd0\xb0\t1\t1\nmissing\t0\t\n",
          "lookup: слово токенизируется как текст; df и номера документов; нет терма — df 0");
    CHECK(s3 == 0 && out3 == "2\tZoo\thttps://x/2\t2\n9\tnot found\n", "lookup -d: заголовок, URL, число токенов");
    CHECK(s4 == 1 && s5 == 1, "нет файла документа или строка без полей — код 1");
}

int main() {
    if (!tok::init()) {
        std::fprintf(stderr, "locale C.UTF-8 is not available\n");
        return 2;
    }
    return ut::run_all();
}
