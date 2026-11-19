#include "index_builder.h"
#include "query.h"
#include "unittest.h"
#include "vbyte.h"

#include <functional>
#include <map>
#include <random>
#include <set>
#include <vector>

typedef std::vector<uint32_t> Ids;

static std::string hex(const unsigned char* p, size_t n) {
    std::string out;
    char buf[4];
    for (size_t i = 0; i < n; ++i) {
        std::snprintf(buf, sizeof buf, i ? " %02x" : "%02x", p[i]);
        out += buf;
    }
    return out;
}

static std::string encoded(uint32_t v) {
    unsigned char b[vb::MAX_BYTES];
    return hex(b, vb::encode(v, b));
}

static std::string encoded_list(const Ids& ids) {
    std::vector<unsigned char> b(ids.size() * vb::MAX_BYTES + 1);
    return hex(b.data(), vb::encode_list(ids.data(), ids.size(), b.data()));
}

static bool decodes_to(const std::vector<unsigned char>& bytes, size_t n, Ids* out = nullptr) {
    Ids ids(n + 1);
    const unsigned char* end = bytes.data() + bytes.size();
    const unsigned char* p = vb::decode_list(bytes.data(), end, n, ids.data());
    ids.resize(n);
    if (out) *out = ids;
    return p == end;
}

TEST("VByte: побитовая схема одного числа") {
    struct Case {
        uint32_t v;
        const char* bytes;
    };
    const Case cases[] = {{0, "80"},          {5, "85"},          {127, "ff"},
                          {128, "00 81"},     {130, "02 81"},     {824, "38 86"},
                          {16383, "7f ff"},   {16384, "00 00 81"}, {214577, "31 0c 8d"},
                          {UINT32_MAX, "7f 7f 7f 7f 8f"}};
    for (const Case& c : cases) {
        std::string got = encoded(c.v);
        std::string line = std::to_string(c.v) + " -> " + got;
        CHECK(got == c.bytes && vb::size(c.v) == (got.size() + 1) / 3, line.c_str());
    }
}

TEST("VByte: длина кода на границах и обратимость") {
    const size_t want[] = {1, 1, 2, 2, 3, 3, 4, 4, 5, 5};
    const uint32_t vals[] = {0, 127, 128, 16383, 16384, (1u << 21) - 1, 1u << 21, (1u << 28) - 1, 1u << 28,
                             UINT32_MAX};
    bool sizes = true;
    for (size_t i = 0; i < 10; ++i) sizes = sizes && vb::size(vals[i]) == want[i];
    CHECK(sizes, "границы 2^7, 2^14, 2^21, 2^28: 1, 2, 3, 4 и 5 байт");
    std::mt19937 rnd(11);
    size_t bad = 0;
    for (int i = 0; i < 1000000; ++i) {
        uint32_t v = rnd() >> (rnd() % 32);
        unsigned char b[vb::MAX_BYTES];
        size_t n = vb::encode(v, b);
        uint32_t back = 0;
        const unsigned char* p = vb::decode(b, b + n, back);
        bool stop_only_last = (b[n - 1] & 0x80) != 0;
        for (size_t k = 0; k + 1 < n; ++k) stop_only_last = stop_only_last && !(b[k] & 0x80);
        bad += !(p == b + n && back == v && n == vb::size(v) && stop_only_last);
    }
    CHECK(bad == 0, "1 000 000 случайных чисел: decode(encode(v)) = v, бит 7 стоит только в последнем байте");
}

TEST("Список документов: разности соседних docID") {
    Ids ids = {824, 829, 215406};
    std::string got = encoded_list(ids);
    ut::note("824, 829, 215406 -> разности 824, 5, 214577 -> %s", got.c_str());
    CHECK(got == "38 86 85 31 0c 8d" && vb::list_size(ids.data(), 3) == 6, "6 байт вместо 12");
    CHECK(encoded_list({0, 1, 2, 3}) == "80 81 81 81", "docID 0 и сплошной список — по байту на документ");
    std::mt19937 rnd(5);
    size_t bad = 0;
    for (int t = 0; t < 2000; ++t) {
        uint32_t step = 1 + rnd() % (t % 4 == 0 ? 3000000u : 300u);
        Ids src;
        uint32_t d = rnd() % 1000;
        for (size_t n = 1 + rnd() % 500; n && d < UINT32_MAX - step; --n, d += 1 + rnd() % step) src.push_back(d);
        if (t == 0) src = {UINT32_MAX - 1, UINT32_MAX};
        std::vector<unsigned char> b(src.size() * vb::MAX_BYTES);
        b.resize(vb::encode_list(src.data(), src.size(), b.data()));
        Ids back;
        bad += !(b.size() == vb::list_size(src.data(), src.size()) && decodes_to(b, src.size(), &back) && back == src);
    }
    CHECK(bad == 0, "2000 случайных списков (разности от 1 до 3 млн, docID до 2^32 - 1): упаковка и распаковка обратимы");
}

TEST("Распаковка повреждённых данных") {
    CHECK(!decodes_to({0x05}, 1), "нет байта с битом конца");
    CHECK(!decodes_to({0x01, 0x02, 0x03, 0x04, 0x05, 0x86}, 1), "число длиннее 5 байт");
    CHECK(!decodes_to({0x7f, 0x7f, 0x7f, 0x7f, 0x9f}, 1), "число больше 2^32 - 1");
    CHECK(!decodes_to({0x85, 0x80}, 2), "нулевая разность: повтор docID");
    CHECK(!decodes_to({0x7e, 0x7f, 0x7f, 0x7f, 0x8f, 0x82}, 2), "переполнение docID при сложении разностей");
    CHECK(!decodes_to({0x85, 0x85}, 1) && !decodes_to({0x85}, 2), "лишние байты или недостающие числа");
    CHECK(decodes_to({0x80, 0x81, 0x00, 0x81}, 3), "корректный список 0, 1, 129 читается");
}

static std::vector<std::string> random_texts(size_t docs, unsigned seed) {
    std::mt19937 rnd(seed);
    std::vector<std::string> texts;
    for (size_t d = 0; d < docs; ++d) {
        std::string t = "every";
        for (int w = 0; w < 6; ++w) {
            uint32_t r = rnd() % 1000;
            t += " w" + std::to_string(r * r / 1000);
        }
        if (d == 0 || d + 1 == docs) t += " edge";
        if (d % 20000 == 7) t += " sparse";
        texts.push_back(t);
    }
    return texts;
}

static void build(const std::vector<std::string>& texts, const std::string& path, bool stem, bool compress) {
    IndexBuilder b(stem, compress);
    for (size_t i = 0; i < texts.size(); ++i) {
        std::string title = "Doc " + std::to_string(i), url = "https://example.org/" + std::to_string(i);
        b.add_text(texts[i].data(), texts[i].size(), title.data(), title.size(), url.data(), url.size());
    }
    b.write(path.c_str());
}

static uint64_t le(const std::string& b, uint64_t off, int bytes) {
    uint64_t v = 0;
    for (int i = bytes - 1; i >= 0; --i) v = (v << 8) | static_cast<unsigned char>(b[off + i]);
    return v;
}

struct Parsed {
    bool ok = true;
    uint64_t flags = 0, n = 0, t = 0, p = 0, docs_off = 0, terms_off = 0, postings_off = 0, strings_off = 0;
    std::map<std::string, Ids> lists;
    std::vector<uint64_t> first, bytes;
};

static Parsed parse_compressed(const std::string& b) {
    Parsed x;
    x.flags = le(b, 8, 4);
    x.n = le(b, 12, 4);
    x.t = le(b, 16, 4);
    x.p = le(b, 24, 8);
    x.docs_off = le(b, 32, 8);
    x.terms_off = le(b, 40, 8);
    x.postings_off = le(b, 48, 8);
    x.strings_off = le(b, 56, 8);
    uint64_t total = x.strings_off - x.postings_off;
    for (uint64_t i = 0; i < x.t; ++i) {
        uint64_t r = x.terms_off + 16 * i;
        uint64_t so = x.strings_off + le(b, r, 4);
        std::string term = b.substr(so + 2, le(b, so, 2));
        uint64_t df = le(b, r + 4, 4), first = le(b, r + 8, 8);
        uint64_t next = i + 1 < x.t ? le(b, r + 16 + 8, 8) : total;
        uint64_t pos = x.postings_off + first, end = x.postings_off + next;
        Ids ids;
        uint64_t prev = 0;
        for (uint64_t k = 0; k < df; ++k) {
            uint64_t v = 0;
            int shift = 0;
            for (;;) {
                if (pos >= end) {
                    x.ok = false;
                    break;
                }
                unsigned char c = static_cast<unsigned char>(b[pos++]);
                v |= uint64_t(c & 0x7F) << shift;
                shift += 7;
                if (c & 0x80) break;
            }
            prev += v;
            ids.push_back(uint32_t(prev));
        }
        x.ok = x.ok && pos == end;
        x.first.push_back(first);
        x.bytes.push_back(next - first);
        x.lists[term] = ids;
    }
    return x;
}

static std::map<std::string, Ids> lists_of(const Index& ix, bool* ok = nullptr) {
    std::map<std::string, Ids> out;
    bool good = true;
    for (uint32_t k = 0; k < ix.terms; ++k) {
        uint16_t len;
        const unsigned char* s = ix.str(get_u32(ix.term_at(k)), len);
        Ids ids(ix.df(k));
        good = ix.read_list(k, ids.data()) && good;
        out[std::string(reinterpret_cast<const char*>(s), len)] = ids;
    }
    if (ok) *ok = good;
    return out;
}

static const std::vector<std::string> SMALL = {"red car", "yellow car", "red bike", "green car", "yellow red bus"};

TEST("Файл сжатого индекса по побитовой схеме (свой разбор в тесте)") {
    ut::TempDir tmp;
    build(SMALL, tmp.path("raw.bin"), false, false);
    build(SMALL, tmp.path("vb.bin"), false, true);
    std::string raw = ut::read_file(tmp.path("raw.bin")), vbf = ut::read_file(tmp.path("vb.bin"));
    Parsed x = parse_compressed(vbf);
    std::string got;
    for (const auto& kv : x.lists) {
        got += (got.empty() ? "" : " ") + kv.first + ":";
        for (size_t i = 0; i < kv.second.size(); ++i) got += (i ? "," : "") + std::to_string(kv.second[i]);
    }
    ut::note("списки: %s", got.c_str());
    ut::note("раздел списков: %s", hex(reinterpret_cast<const unsigned char*>(vbf.data()) + x.postings_off,
                                       x.strings_off - x.postings_off).c_str());
    CHECK(x.flags == fmt::FLAG_COMPRESSED && le(vbf, 4, 2) == 1 && x.n == 5 && x.t == 6 && x.p == 11,
          "заголовок: версия 1, флаг 2 (списки сжаты), N = 5, T = 6, P = 11 записей");
    CHECK(x.ok && got == "bike:2 bus:4 car:0,1,3 green:3 red:0,2,4 yellow:1,4",
          "списки читаются по схеме: first — смещение в байтах, длина — до first следующего терма");
    bool offsets = true;
    for (size_t i = 0; i < x.first.size(); ++i) offsets = offsets && x.first[i] == (i ? x.first[i - 1] + x.bytes[i - 1] : 0);
    CHECK(offsets && x.strings_off - x.postings_off == 11, "списки идут подряд, 11 записей — 11 байт вместо 44");
    bool same_rest = raw.size() - vbf.size() == 33 && raw.compare(0, 8, vbf, 0, 8) == 0 &&
                     raw.compare(12, 44, vbf, 12, 44) == 0 &&
                     raw.compare(64, x.terms_off - 64, vbf, 64, x.terms_off - 64) == 0 &&
                     raw.compare(le(raw, 56, 8), std::string::npos, vbf, x.strings_off, std::string::npos) == 0;
    for (uint64_t i = 0; i < x.t; ++i)
        same_rest = same_rest && raw.compare(x.terms_off + 16 * i, 8, vbf, x.terms_off + 16 * i, 8) == 0;
    CHECK(same_rest, "заголовок (кроме flags и strings_off), таблица документов, поля term и df, пул строк не меняются");
}

TEST("Index::read_list: сжатый индекс = несжатый на случайном корпусе") {
    ut::TempDir tmp;
    std::vector<std::string> texts = random_texts(50000, 3);
    build(texts, tmp.path("raw.bin"), false, false);
    build(texts, tmp.path("vb.bin"), false, true);
    Index r, c;
    bool loaded = r.load(tmp.path("raw.bin").c_str(), "test") && c.load(tmp.path("vb.bin").c_str(), "test");
    bool ok_r = false, ok_c = false;
    std::map<std::string, Ids> lr, lc;
    if (loaded) {
        lr = lists_of(r, &ok_r);
        lc = lists_of(c, &ok_c);
    }
    uint64_t bytes = 0;
    for (uint32_t k = 0; loaded && k < c.terms; ++k) bytes += c.list_bytes(k);
    ut::note("термов %u, записей %llu: без сжатия %llu байт, со сжатием %llu байт", c.terms,
             (unsigned long long)c.postings, (unsigned long long)r.postings_bytes(),
             (unsigned long long)c.postings_bytes());
    CHECK(loaded && !r.compressed && c.compressed, "оба индекса загружаются, флаг сжатия читается");
    CHECK(ok_r && ok_c && lr == lc && lc["every"].size() == 50000 && lc["edge"] == Ids({0, 49999}) &&
              lc["sparse"] == Ids({7, 20007, 40007}),
          "все списки совпадают, включая терм во всех документах и разности 20000 и 49999 (3 байта)");
    CHECK(bytes == c.postings_bytes() && c.list_bytes(c.find("every", 5)) == 50000 &&
              c.list_bytes(c.find("edge", 4)) == 4,
          "длины списков в байтах: every — 50000 (по байту на разность 1), edge — 1 + 3 байта");
}

static Ids search(const Index& ix, const std::string& q) {
    Node* root = parse_query(q.data(), q.size(), ix.flags & fmt::FLAG_STEMMED);
    List res;
    if (root) eval(ix, root, res);
    destroy(root);
    return Ids(res.data, res.data + res.size);
}

TEST("Поиск: 500 случайных запросов, выдача по сжатому индексу = по несжатому") {
    ut::TempDir tmp;
    std::vector<std::string> texts = random_texts(20000, 9);
    build(texts, tmp.path("raw.bin"), false, false);
    build(texts, tmp.path("vb.bin"), false, true);
    Index r, c;
    r.load(tmp.path("raw.bin").c_str(), "test");
    c.load(tmp.path("vb.bin").c_str(), "test");
    std::mt19937 rnd(17);
    auto word = [&]() {
        uint32_t x = rnd() % 1000;
        return rnd() % 10 == 0 ? std::string("every") : "w" + std::to_string(x * x / 1000);
    };
    std::function<std::string(int)> gen = [&](int depth) -> std::string {
        if (depth == 0 || rnd() % 3 == 0) return word();
        static const char* ops[] = {" ", " && ", " || "};
        std::string op = ops[rnd() % 3];
        std::string s = gen(depth - 1);
        for (int i = 1 + rnd() % 2; i > 0; --i) s += op + (rnd() % 4 == 0 ? "!" : "") + "(" + gen(depth - 1) + ")";
        return s;
    };
    int bad = 0, nonempty = 0;
    size_t total = 0;
    for (int i = 0; i < 500; ++i) {
        std::string q = gen(3);
        Ids a = search(r, q), b = search(c, q);
        bad += a != b;
        nonempty += !a.empty();
        total += a.size();
        if (i < 3) ut::note("пример: [ %s ] -> найдено %zu", q.c_str(), b.size());
    }
    ut::note("непустая выдача у %d запросов, всего %zu документов", nonempty, total);
    CHECK(bad == 0 && nonempty > 250, "выдача совпала во всех 500 запросах");
}

TEST("Стемминг и сжатие вместе (флаги 1 + 2)") {
    ut::TempDir tmp;
    build({"museum in town", "two museums", "skiing and skis"}, tmp.path("both.bin"), true, true);
    Index ix;
    bool loaded = ix.load(tmp.path("both.bin").c_str(), "test");
    Ids a = loaded ? search(ix, "Museums") : Ids(), b = loaded ? search(ix, "skiing !town") : Ids();
    CHECK(loaded && ix.flags == 3 && ix.compressed, "flags = 3");
    CHECK(a == Ids({0, 1}) && b == Ids({2}), "museums -> museum: 0, 1; skiing !town: 2");
}

static void patch(const std::string& path, uint64_t off, unsigned char v) {
    std::string b = ut::read_file(path);
    b[off] = char(v);
    FILE* f = std::fopen(path.c_str(), "wb");
    std::fwrite(b.data(), 1, b.size(), f);
    std::fclose(f);
}

TEST("Повреждённый сжатый индекс") {
    ut::TempDir tmp;
    build(SMALL, tmp.path("a.bin"), false, true);
    std::string b = ut::read_file(tmp.path("a.bin"));
    uint64_t terms_off = le(b, 40, 8), postings_off = le(b, 48, 8);
    std::string p1 = tmp.write("stop.bin", b), p2 = tmp.write("flags.bin", b), p3 = tmp.write("order.bin", b);
    patch(p1, postings_off, b[postings_off] & 0x7F);
    patch(p2, 8, 0x0A);
    patch(p3, terms_off + 16 + 8, 0x09);
    Index a, f, o;
    bool la = a.load(p1.c_str(), "test");
    Ids out(8);
    bool read_bad = la && !a.read_list(0, out.data());
    Ids found = la ? search(a, "bike || car") : Ids();
    std::string err = ut::run("echo bike | ../lab8/build/search " + p1 + " 2>&1 >/dev/null");
    std::string err2 = ut::run("../lab8/build/search " + p2 + " < /dev/null 2>&1");
    CHECK(read_bad && found == Ids({0, 1, 3}), "снят бит конца у числа: список bike не читается, остальные — да");
    CHECK(err.find("document list of bike is damaged") != std::string::npos, "search сообщает о повреждении");
    CHECK(!f.load(p2.c_str(), "test") && err2.find("unknown flags 10") != std::string::npos,
          "неизвестный флаг — индекс не открывается");
    CHECK(!o.load(p3.c_str(), "test"), "смещения first не по возрастанию — индекс не открывается");
}

static std::string without_time(const std::string& s) {
    std::string out;
    size_t pos = 0;
    while (pos < s.size()) {
        size_t e = s.find('\n', pos);
        if (e == std::string::npos) e = s.size();
        if (s.compare(pos, 8, "time_us:") != 0) out.append(s, pos, e - pos + 1);
        pos = e + 1;
    }
    return out;
}

TEST("Утилиты командной строки: indexer -c, lookup, search, listbench") {
    ut::TempDir tmp;
    std::vector<std::string> texts = random_texts(3000, 21);
    std::string list;
    for (size_t i = 0; i < texts.size(); ++i)
        list += tmp.write(std::to_string(i) + ".txt", texts[i]) + "\tDoc " + std::to_string(i) + "\thttps://x/\n";
    std::string docs = tmp.write("docs.tsv", list), raw = tmp.path("raw.bin"), vbi = tmp.path("vb.bin");
    int s1 = -1, s2 = -1, s3 = -1, s4 = -1, s5 = -1, s6 = -1, s7 = -1, s8 = -1, s9 = -1;
    std::string o1 = ut::run("../lab7/build/indexer " + raw + " < " + docs, &s1);
    std::string o2 = ut::run("../lab7/build/indexer -c " + vbi + " < " + docs, &s2);
    std::string q = tmp.write("q.txt", "every\nw0 && !w1\nedge || w998 || w10\n");
    std::string l1 = ut::run("echo 'w5 edge zzz' | ../lab7/build/lookup " + raw, &s3);
    std::string l2 = ut::run("echo 'w5 edge zzz' | ../lab7/build/lookup " + vbi, &s4);
    std::string f1 = ut::run("../lab8/build/search " + raw + " -n 0 < " + q + " 2>/dev/null", &s5);
    std::string f2 = ut::run("../lab8/build/search " + vbi + " -n 0 < " + q + " 2>&1", &s6);
    std::string b1 = ut::run("build/listbench " + vbi + " -r 1", &s7);
    std::string v1 = ut::run("build/listbench -v " + raw + " " + vbi, &s8);
    ut::run("build/listbench 2>/dev/null", &s9);
    uint64_t size_raw = ut::read_file(raw).size(), size_vb = ut::read_file(vbi).size();
    ut::note("indexer -c: %s", ut::escape(o2.substr(0, o2.find("avg_token_len"))).c_str());
    ut::note("размер: без сжатия %llu байт, со сжатием %llu", (unsigned long long)size_raw,
             (unsigned long long)size_vb);
    ut::note("listbench -v: %s", ut::escape(v1).c_str());
    CHECK(s1 == 0 && s2 == 0 && o1.find("compressed\t0\n") != std::string::npos &&
              o2.find("compressed\t1\n") != std::string::npos && size_vb < size_raw,
          "indexer -c: в статистике compressed 1, файл меньше");
    CHECK(s3 == 0 && s4 == 0 && l1 == l2 && l1.find("edge\t2\t0 2999\n") != std::string::npos,
          "lookup: одинаковые df и списки по обоим индексам");
    CHECK(s5 == 0 && s6 == 0 && f2.find("(compressed)") != std::string::npos &&
              without_time(f1) == without_time(f2.substr(f2.find('\n') + 1)),
          "search: одинаковая выдача (кроме времени), при загрузке пишет (compressed)");
    CHECK(s7 == 0 && b1.find("compressed\t1\n") != std::string::npos && b1.find("group\thigh\t") != std::string::npos,
          "listbench: замер по группам термов");
    CHECK(s8 == 0 && v1.find("term_mismatches\t0\n") != std::string::npos && s9 == 2,
          "listbench -v: 0 расхождений; без аргументов — код 2");
}

int main() {
    if (!tok::init()) {
        std::fprintf(stderr, "locale C.UTF-8 is not available\n");
        return 2;
    }
    return ut::run_all();
}
