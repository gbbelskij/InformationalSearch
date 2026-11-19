#include "index_builder.h"
#include "rank.h"
#include "unittest.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <random>
#include <set>
#include <vector>

typedef std::vector<uint32_t> Ids;

static void build(const std::vector<std::string>& texts, const std::string& path, bool stem, bool compress, bool tf) {
    IndexBuilder b(stem, compress, tf);
    for (size_t i = 0; i < texts.size(); ++i) {
        std::string title = "Doc " + std::to_string(i), url = "https://example.org/" + std::to_string(i);
        b.add_text(texts[i].data(), texts[i].size(), title.data(), title.size(), url.data(), url.size());
    }
    b.write(path.c_str());
}

static std::vector<std::string> words(const std::string& text) {
    std::vector<std::string> out;
    std::string w;
    for (size_t i = 0; i <= text.size(); ++i) {
        char c = i < text.size() ? text[i] : ' ';
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) w += c;
        else if (!w.empty()) {
            out.push_back(w);
            w.clear();
        }
    }
    return out;
}

struct Ref {
    std::vector<std::map<std::string, uint32_t>> tf;
    std::map<std::string, uint32_t> df;
    std::vector<double> norm;

    explicit Ref(const std::vector<std::string>& texts) {
        for (const std::string& t : texts) {
            std::map<std::string, uint32_t> m;
            for (const std::string& w : words(t)) ++m[w];
            double s = 0;
            for (const auto& kv : m) {
                ++df[kv.first];
                double x = 1 + std::log10(double(kv.second));
                s += x * x;
            }
            tf.push_back(m);
            norm.push_back(double(float(std::sqrt(s))));
        }
    }

    double score(const std::vector<std::string>& q, size_t d) const {
        std::map<std::string, uint32_t> qtf;
        for (const std::string& w : q) ++qtf[w];
        double n2 = 0, s = 0;
        std::map<std::string, double> qw;
        for (const auto& kv : qtf) {
            auto it = df.find(kv.first);
            if (it == df.end()) continue;
            double x = (1 + std::log10(double(kv.second))) * std::log10(double(tf.size()) / it->second);
            qw[kv.first] = x;
            n2 += x * x;
        }
        if (n2 == 0 || norm[d] == 0) return 0;
        for (const auto& kv : qw) {
            auto it = tf[d].find(kv.first);
            if (it != tf[d].end()) s += kv.second / std::sqrt(n2) * (1 + std::log10(double(it->second))) / norm[d];
        }
        return s;
    }
};

struct Run {
    std::string parsed;
    std::vector<rank::Hit> hits;
};

static Run ranked(const Index& ix, const std::string& q) {
    Run r;
    Node* root = parse_query(q.data(), q.size(), ix.flags & fmt::FLAG_STEMMED);
    bool fuzzy = rank::is_fuzzy(q.data(), q.size());
    rank::Ranker ranker(ix);
    Vec<rank::Hit> hits;
    ranker.run(root, fuzzy, hits);
    Vec<char> text;
    if (root && fuzzy) rank::show_fuzzy(root, text);
    else if (root) show(root, text);
    destroy(root);
    r.parsed = std::string(text.data ? text.data : "", text.size);
    r.hits.assign(hits.data, hits.data + hits.size);
    return r;
}

static std::string docs_of(const Run& r) {
    std::string out;
    for (size_t i = 0; i < r.hits.size(); ++i) out += (i ? "," : "") + std::to_string(r.hits[i].doc);
    return out;
}

static bool sorted_ok(const std::vector<rank::Hit>& h) {
    for (size_t i = 1; i < h.size(); ++i)
        if (rank::before(h[i], h[i - 1])) return false;
    return true;
}

TEST("Веса lnc.ltc: пример из лекции (best car insurance)") {
    const uint32_t N = 1000000;
    double idf_auto = rank::idf(N, 5000), idf_best = rank::idf(N, 50000), idf_car = rank::idf(N, 10000),
           idf_ins = rank::idf(N, 1000);
    ut::note("idf: auto %.2f, best %.2f, car %.2f, insurance %.2f, the (df = N) %.2f", idf_auto, idf_best, idf_car,
             idf_ins, rank::idf(N, N));
    CHECK(std::fabs(idf_auto - 2.3) < 0.01 && std::fabs(idf_best - 1.3) < 0.01 && idf_car == 2.0 && idf_ins == 3.0 &&
              rank::idf(N, N) == 0 && rank::idf(N, 0) == 0,
          "idf = log10(N / df): 2,3; 1,3; 2,0; 3,0; у терма во всех документах 0");
    CHECK(rank::tf_weight(0) == 0 && rank::tf_weight(1) == 1 && std::fabs(rank::tf_weight(2) - 1.30) < 0.01 &&
              rank::tf_weight(10) == 2,
          "вес tf = 1 + log10(tf): 0, 1, 1,30, 2");
    double q[3] = {rank::tf_weight(1) * idf_best, rank::tf_weight(1) * idf_car, rank::tf_weight(1) * idf_ins};
    double qn = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]);
    double d[3] = {rank::tf_weight(1), rank::tf_weight(1), rank::tf_weight(2)};
    double dn = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    double score = q[1] / qn * d[1] / dn + q[2] / qn * d[2] / dn;
    ut::note("запрос: best %.2f car %.2f insurance %.2f; документ: auto %.2f car %.2f insurance %.2f; оценка %.3f",
             q[0] / qn, q[1] / qn, q[2] / qn, d[0] / dn, d[1] / dn, d[2] / dn, score);
    CHECK(std::fabs(score - 0.80) < 0.01, "оценка документа \"car insurance auto insurance\" = 0,27 + 0,53 = 0,80");
}

TEST("Что считается нечётким запросом") {
    const char* fuzzy[] = {"rose flower", "moscow aviation institute", "Hewlett-Packard", "  lviv  ", ""};
    const char* strict[] = {"rose && flower", "rose & flower", "rose || flower", "rose !thorn", "(rose flower)"};
    bool ok = true;
    for (const char* q : fuzzy) ok = ok && rank::is_fuzzy(q, std::strlen(q));
    for (const char* q : strict) ok = ok && !rank::is_fuzzy(q, std::strlen(q));
    CHECK(ok, "только слова и пробелы — нечёткий; любой из & | ! ( ) — булев");
}

static uint64_t le(const std::string& b, uint64_t off, int bytes) {
    uint64_t v = 0;
    for (int i = bytes - 1; i >= 0; --i) v = (v << 8) | static_cast<unsigned char>(b[off + i]);
    return v;
}

static uint64_t vbyte(const std::string& b, uint64_t& pos) {
    uint64_t v = 0;
    for (int shift = 0;; shift += 7) {
        unsigned char c = static_cast<unsigned char>(b[pos++]);
        v |= uint64_t(c & 0x7F) << shift;
        if (c & 0x80) return v;
    }
}

static std::string parse_tf(const std::string& b, std::vector<float>* norms) {
    uint64_t flags = le(b, 8, 4), n = le(b, 12, 4), t = le(b, 16, 4), doc_rec = le(b, 20, 2);
    uint64_t docs_off = le(b, 32, 8), terms_off = le(b, 40, 8), postings_off = le(b, 48, 8), strings_off = le(b, 56, 8);
    for (uint64_t i = 0; i < n; ++i) {
        uint32_t u = uint32_t(le(b, docs_off + i * doc_rec + 12, 4));
        float f;
        std::memcpy(&f, &u, 4);
        norms->push_back(f);
    }
    std::string out;
    for (uint64_t k = 0; k < t; ++k) {
        uint64_t r = terms_off + 16 * k, so = strings_off + le(b, r, 4);
        uint64_t df = le(b, r + 4, 4), first = le(b, r + 8, 8);
        out += (k ? " " : "") + b.substr(so + 2, le(b, so, 2)) + ":";
        uint64_t pos = postings_off + first, prev = 0;
        for (uint64_t i = 0; i < df; ++i) {
            uint64_t doc, tf;
            if (flags & fmt::FLAG_COMPRESSED) {
                doc = prev + vbyte(b, pos);
                tf = vbyte(b, pos);
                prev = doc;
            } else {
                doc = le(b, postings_off + 8 * (first + i), 4);
                tf = le(b, postings_off + 8 * (first + i) + 4, 4);
            }
            out += (i ? "," : "") + std::to_string(doc) + "x" + std::to_string(tf);
        }
    }
    return out;
}

static const std::vector<std::string> SMALL = {"rose rose rose garden", "rose flower", "garden"};

TEST("Формат индекса с частотами (флаг 4): свой разбор по описанию") {
    ut::TempDir tmp;
    build(SMALL, tmp.path("raw.bin"), false, false, true);
    build(SMALL, tmp.path("vb.bin"), false, true, true);
    build(SMALL, tmp.path("plain.bin"), false, false, false);
    std::string raw = ut::read_file(tmp.path("raw.bin")), vbf = ut::read_file(tmp.path("vb.bin"));
    std::vector<float> nr, nv;
    std::string lr = parse_tf(raw, &nr), lv = parse_tf(vbf, &nv);
    ut::note("без сжатия: %s; нормы %.4f %.4f %.4f", lr.c_str(), nr[0], nr[1], nr[2]);
    ut::note("со сжатием: %s", lv.c_str());
    CHECK(le(raw, 8, 4) == 4 && le(vbf, 8, 4) == 6 && le(raw, 20, 2) == 16 &&
              le(ut::read_file(tmp.path("plain.bin")), 20, 2) == 12,
          "flags: 4 (частоты), 6 (частоты и сжатие); запись документа 16 байт вместо 12");
    CHECK(lr == "flower:1x1 garden:0x1,2x1 rose:0x3,1x1" && lv == lr,
          "списки — пары (docID, tf): rose в документе 0 — 3 раза; одинаково в обоих вариантах");
    double n0 = std::sqrt(std::pow(1 + std::log10(3.0), 2) + 1), n1 = std::sqrt(2.0);
    CHECK(std::fabs(nr[0] - n0) < 1e-6 && std::fabs(nr[1] - n1) < 1e-6 && nr[2] == 1 && nv == nr,
          "длина вектора документа: sqrt((1 + log10 3)^2 + 1^2), sqrt(2), 1");
    CHECK(raw.size() - le(raw, 56, 8) == ut::read_file(tmp.path("plain.bin")).size() -
                                             le(ut::read_file(tmp.path("plain.bin")), 56, 8) &&
              le(raw, 56, 8) - le(raw, 48, 8) == 8 * 5,
          "без сжатия запись списка — 8 байт (docID и tf по 4), пул строк не меняется");
    std::vector<unsigned char> bad = {0x80, 0x80};
    uint32_t ids[1], tfs[1];
    CHECK(!vb::decode_pairs(bad.data(), bad.data() + 2, 1, ids, tfs), "распаковка отвергает tf = 0");
}

TEST("Индекс с частотами для булева поиска ЛР8 не отличается") {
    ut::TempDir tmp;
    std::vector<std::string> texts;
    std::mt19937 rnd(4);
    for (int d = 0; d < 3000; ++d) {
        std::string t;
        for (int w = 0; w < 30; ++w) t += "w" + std::to_string(rnd() % 50) + " ";
        texts.push_back(t);
    }
    build(texts, tmp.path("plain.bin"), false, false, false);
    build(texts, tmp.path("raw.bin"), false, false, true);
    build(texts, tmp.path("vb.bin"), false, true, true);
    Index a, b, c;
    bool loaded = a.load(tmp.path("plain.bin").c_str(), "test") && b.load(tmp.path("raw.bin").c_str(), "test") &&
                  c.load(tmp.path("vb.bin").c_str(), "test");
    bool same = loaded && a.terms == b.terms && b.terms == c.terms;
    for (uint32_t k = 0; same && k < a.terms; ++k) {
        Ids x(a.df(k)), y(a.df(k)), z(a.df(k));
        same = a.read_list(k, x.data()) && b.read_list(k, y.data()) && c.read_list(k, z.data()) && x == y && y == z;
    }
    const char* qs[] = {"w1 w2", "w3 || w4", "w5 !w6", "(w7 || w8) && w9"};
    for (const char* q : qs) {
        List la, lb;
        Node* root = parse_query(q, std::strlen(q), false);
        eval(a, root, la);
        eval(c, root, lb);
        destroy(root);
        same = same && la.size == lb.size && std::memcmp(la.data, lb.data, la.size * 4) == 0;
    }
    CHECK(loaded && b.has_tf && c.has_tf && !a.has_tf, "флаг частот читается");
    CHECK(same, "read_list и булев поиск ЛР8 дают одно и то же по индексам без частот и с ними (с и без сжатия)");
}

TEST("Ранжирование на маленьком корпусе") {
    ut::TempDir tmp;
    std::vector<std::string> texts = {"rose garden in spring with many plants and trees and a pond",
                                      "rose flower",
                                      "flower shop flower market flower delivery",
                                      "rose rose rose rose festival",
                                      "garden party",
                                      "thorn"};
    build(texts, tmp.path("i.bin"), false, true, true);
    Index ix;
    ix.load(tmp.path("i.bin").c_str(), "test");
    Ref ref(texts);
    Run a = ranked(ix, "rose flower"), b = ranked(ix, "rose && flower"), c = ranked(ix, "rose !garden"),
        d = ranked(ix, "rose || thorn");
    for (const Run* r : {&a, &b, &c, &d}) {
        std::string line;
        for (const rank::Hit& h : r->hits) line += std::to_string(h.doc) + ":" + std::to_string(h.score).substr(0, 5) + " ";
        ut::note("[%s] -> %s", r->parsed.c_str(), line.c_str());
    }
    CHECK(a.parsed == "any(rose flower)" && docs_of(a) == "1,2,3,0",
          "нечёткий [rose flower]: документы хотя бы с одним словом; оба слова (1) выше, короткий документ выше длинного");
    bool scores = true;
    for (const rank::Hit& h : a.hits) scores = scores && std::fabs(h.score - ref.score({"rose", "flower"}, h.doc)) < 1e-9;
    CHECK(scores, "оценки совпадают с эталонным косинусом lnc.ltc");
    CHECK(b.parsed == "rose && flower" && docs_of(b) == "1", "булев [rose && flower]: строго оба слова");
    CHECK(docs_of(c) == "3,1" && docs_of(d) == "5,3,1,0",
          "[rose !garden]: НЕ отсекает 0, порядок по весу rose; [rose || thorn]: редкий thorn весит больше");
    Run e = ranked(ix, "!rose");
    CHECK(docs_of(e) == "2,4,5" && e.hits[0].score == 0, "только НЕ: оценки 0, порядок по docID");
}

TEST("300 случайных запросов против эталона на STL") {
    ut::TempDir tmp;
    std::mt19937 rnd(9);
    std::vector<std::string> texts;
    for (int d = 0; d < 2000; ++d) {
        std::string t;
        int len = 1 + int(rnd() % 60);
        for (int w = 0; w < len; ++w) {
            uint32_t r = rnd() % 1000;
            t += "w" + std::to_string(r * r / 10000) + " ";
        }
        texts.push_back(t);
    }
    build(texts, tmp.path("raw.bin"), false, false, true);
    build(texts, tmp.path("vb.bin"), false, true, true);
    Index rx, vx;
    rx.load(tmp.path("raw.bin").c_str(), "test");
    vx.load(tmp.path("vb.bin").c_str(), "test");
    Ref ref(texts);
    auto word = [&]() { return "w" + std::to_string(rnd() % 100); };
    int bad = 0, nonempty = 0;
    for (int i = 0; i < 300; ++i) {
        std::string a = word(), b = word(), c = word(), q;
        int form = int(rnd() % 5);
        std::set<uint32_t> want;
        auto has = [&](size_t d, const std::string& w) { return ref.tf[d].count(w) > 0; };
        for (size_t d = 0; d < texts.size(); ++d) {
            bool in = form == 0 ? has(d, a) || has(d, b) || has(d, c)
                    : form == 1 ? has(d, a) && has(d, b)
                    : form == 2 ? has(d, a) || has(d, b)
                    : form == 3 ? has(d, a) && !has(d, b)
                                : (has(d, a) || has(d, b)) && has(d, c);
            if (in) want.insert(uint32_t(d));
        }
        q = form == 0 ? a + " " + b + " " + c
          : form == 1 ? a + " && " + b
          : form == 2 ? a + " || " + b
          : form == 3 ? a + " !" + b
                      : "(" + a + " || " + b + ") && " + c;
        std::vector<std::string> qwords = form == 0 ? std::vector<std::string>{a, b, c}
                                        : form == 3 ? std::vector<std::string>{a}
                                        : form == 4 ? std::vector<std::string>{a, b, c}
                                                    : std::vector<std::string>{a, b};
        Run r = ranked(rx, q), v = ranked(vx, q);
        std::set<uint32_t> got;
        bool ok = sorted_ok(r.hits) && r.hits.size() == v.hits.size();
        for (size_t k = 0; ok && k < r.hits.size(); ++k) {
            got.insert(r.hits[k].doc);
            ok = r.hits[k].doc == v.hits[k].doc && r.hits[k].score == v.hits[k].score &&
                 std::fabs(r.hits[k].score - ref.score(qwords, r.hits[k].doc)) < 1e-9;
        }
        ok = ok && got == want;
        bad += !ok;
        nonempty += !want.empty();
        if (!ok) ut::note("расхождение: [ %s ]", q.c_str());
        if (i < 5) ut::note("пример: [ %s ] -> найдено %zu, лучший %u (%.4f)", q.c_str(), r.hits.size(),
                            r.hits.empty() ? 0 : r.hits[0].doc, r.hits.empty() ? 0.0 : r.hits[0].score);
    }
    ut::note("непустая выдача у %d запросов", nonempty);
    CHECK(bad == 0 && nonempty > 200,
          "множество найденных, оценки и порядок совпадают с эталоном; индексы с частотами без сжатия и со сжатием дают одно и то же");
}

TEST("Сортировка выдачи (своя, слиянием) = std::stable_sort") {
    std::mt19937 rnd(1);
    std::vector<rank::Hit> h;
    for (uint32_t i = 0; i < 100000; ++i) h.push_back({uint32_t(rnd() % 50000), double(rnd() % 100) / 7});
    std::vector<rank::Hit> want = h;
    std::stable_sort(want.begin(), want.end(), rank::before);
    rank::sort_hits(h.data(), h.size());
    bool same = true;
    for (size_t i = 0; i < h.size(); ++i) same = same && h[i].doc == want[i].doc && h[i].score == want[i].score;
    CHECK(same && sorted_ok(h), "100 000 оценок с повторами: по убыванию оценки, при равенстве — по возрастанию docID");
}

TEST("Стемминг, сжатие и частоты вместе (флаги 7)") {
    ut::TempDir tmp;
    std::vector<std::string> texts = {"museum in town", "two museums and a museum", "skiing"};
    build(texts, tmp.path("all.bin"), true, true, true);
    Index ix;
    bool loaded = ix.load(tmp.path("all.bin").c_str(), "test");
    Run r = loaded ? ranked(ix, "Museums town") : Run();
    ut::note("[%s] -> %s", r.parsed.c_str(), docs_of(r).c_str());
    CHECK(loaded && ix.flags == 7, "flags = 7");
    CHECK(r.parsed == "any(museum town)" && docs_of(r) == "0,1", "museums -> museum; документ с обоими словами первый");
}

static std::string line_of(const std::string& out, const std::string& prefix) {
    size_t p = out.find(prefix);
    if (p == std::string::npos) return "";
    return out.substr(p, out.find('\n', p) - p);
}

TEST("Утилиты командной строки: indexer -t, search, lookup") {
    ut::TempDir tmp;
    std::vector<std::string> texts = {"rose garden", "rose flower", "flower flower shop", "thorn"};
    std::string list;
    for (size_t i = 0; i < texts.size(); ++i)
        list += tmp.write(std::to_string(i) + ".txt", texts[i]) + "\tDoc " + std::to_string(i) + "\thttps://x/" +
                std::to_string(i) + "\n";
    std::string docs = tmp.write("docs.tsv", list), plain = tmp.path("plain.bin"), tfi = tmp.path("tf.bin");
    int s1 = -1, s2 = -1, s3 = -1, s4 = -1, s5 = -1, s6 = -1;
    std::string o1 = ut::run("../lab7/build/indexer " + plain + " < " + docs, &s1);
    std::string o2 = ut::run("../lab7/build/indexer -c -t " + tfi + " < " + docs, &s2);
    std::string q = tmp.write("q.txt", "rose flower\nrose && flower\n");
    std::string f1 = ut::run("../lab8/build/search " + tfi + " -n 0 < " + q + " 2>&1", &s3);
    std::string f2 = ut::run("../lab8/build/search " + plain + " -n 0 < " + q + " 2>/dev/null", &s4);
    std::string l1 = ut::run("echo flower | ../lab7/build/lookup " + tfi, &s5);
    std::string w = ut::run("printf '0\\t1\\trose flower\\n' | ../lab8/build/search " + tfi + " -p 2>/dev/null", &s6);
    ut::note("search (tf-idf): %s", ut::escape(f1).c_str());
    CHECK(s1 == 0 && s2 == 0 && o1.find("tf\t0\n") != std::string::npos && o2.find("tf\t1\n") != std::string::npos,
          "indexer -t: в статистике tf 1");
    CHECK(s3 == 0 && f1.find("(tf-idf)") != std::string::npos && line_of(f1, "parsed: ") == "parsed: any(rose flower)" &&
              f1.find("found: 3\n") != std::string::npos && f1.find("\n1\t1\tDoc 1\thttps://x/1\t") != std::string::npos &&
              f1.find("parsed: rose && flower\nfound: 1\n") != std::string::npos,
          "search по индексу с частотами: нечёткий запрос (3 документа, первым документ 1), булев (1), пятый столбец — оценка");
    CHECK(s4 == 0 && f2.find("found: 1\n") != std::string::npos && f2.find("\n1\t1\tDoc 1\thttps://x/1\n") != std::string::npos,
          "по индексу без частот — прежний булев поиск ЛР8 (пробел = И, 4 столбца)");
    CHECK(s5 == 0 && l1 == "flower\t2\t1 2\n" && s6 == 0 && w.find("found: 3\n") != std::string::npos,
          "lookup читает списки индекса с частотами; режим -p веб-сервиса работает");
}

int main() {
    if (!tok::init()) {
        std::fprintf(stderr, "locale C.UTF-8 is not available\n");
        return 2;
    }
    return ut::run_all();
}
