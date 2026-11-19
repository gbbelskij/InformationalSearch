#include "stats.h"
#include "tokenizer.h"
#include "unittest.h"

static std::string tokens(const std::string& text) {
    std::string out;
    tok::tokenize(text.data(), text.size(), [&](const std::string& t, size_t) {
        if (!out.empty()) out += ' ';
        out += t;
    });
    return out;
}

static bool expect_tokens(const std::string& text, const std::string& want) {
    std::string got = tokens(text);
    ut::note("вход:     \"%s\"", ut::escape(text).c_str());
    ut::note("получено: [%s]", ut::escape(got).c_str());
    return got == want;
}

static uint32_t decode(const char* s, size_t n, size_t* len) {
    uint32_t cp;
    *len = tok::decode(reinterpret_cast<const unsigned char*>(s), n, cp);
    return cp;
}

TEST("decode: корректные последовательности UTF-8") {
    size_t len;
    CHECK(decode("A", 1, &len) == 0x41 && len == 1, "A (1 байт) -> U+0041");
    CHECK(decode("\xc3\xa9", 2, &len) == 0xE9 && len == 2, "é (2 байта) -> U+00E9");
    CHECK(decode("\xd0\x96", 2, &len) == 0x416 && len == 2, "Ж (2 байта) -> U+0416");
    CHECK(decode("\xe4\xb8\xad", 3, &len) == 0x4E2D && len == 3, "иероглиф (3 байта) -> U+4E2D");
    CHECK(decode("\xf0\x9f\x98\x80", 4, &len) == 0x1F600 && len == 4, "эмодзи (4 байта) -> U+1F600");
}

TEST("decode: некорректные последовательности дают U+FFFD и длину 1") {
    size_t len;
    CHECK(decode("\x80", 1, &len) == 0xFFFD && len == 1, "одиночный байт продолжения 0x80");
    CHECK(decode("\xff", 1, &len) == 0xFFFD && len == 1, "недопустимый байт 0xFF");
    CHECK(decode("\xe2\x82", 2, &len) == 0xFFFD && len == 1, "оборванная трёхбайтовая последовательность");
    CHECK(decode("\xe2\x82 ", 3, &len) == 0xFFFD && len == 1, "второй байт продолжения заменён пробелом");
    CHECK(decode("\xc0\x80", 2, &len) == 0xFFFD && len == 1, "избыточная запись (overlong) нуля");
    CHECK(decode("\xed\xa0\x80", 3, &len) == 0xFFFD && len == 1, "суррогат U+D800");
    CHECK(decode("\xf4\x90\x80\x80", 4, &len) == 0xFFFD && len == 1, "код больше U+10FFFF");
}

TEST("encode: кодирование и обратное декодирование") {
    uint32_t cps[] = {0x41, 0x7F, 0x80, 0xE9, 0x7FF, 0x800, 0x416, 0x4E2D, 0xFFFF, 0x10000, 0x1F600, 0x10FFFF};
    bool ok = true;
    for (uint32_t cp : cps) {
        std::string s;
        tok::encode(cp, s);
        size_t len;
        ok = ok && decode(s.data(), s.size(), &len) == cp && len == s.size();
    }
    CHECK(ok, "12 кодов на границах длин 1–4 байта: decode(encode(cp)) == cp");
}

TEST("Английский текст, знаки препинания, регистр") {
    CHECK(expect_tokens("The Old Town (UNESCO, 1998) is great!", "the old town unesco 1998 is great"),
          "знаки препинания и скобки — разделители, регистр понижен");
}

TEST("Дефис и апостроф разделяют") {
    CHECK(expect_tokens("Hewlett-Packard, state-of-the-art, don't, Lviv\xe2\x80\x99s",
                        "hewlett packard state of the art don t lviv s"),
          "дефис, апостроф ' и типографский апостроф U+2019 — разделители");
}

TEST("Числа и буквы с цифрами") {
    CHECK(expect_tokens("3.14 1,000 B-52 10am 19th", "3 14 1 000 b 52 10am 19th"),
          "точка, запятая и дефис делят числа; буквы и цифры подряд — один токен");
}

TEST("Юникод: диакритика, кириллица, греческий") {
    CHECK(expect_tokens("S\xc3\xa3o Paulo, \xc3\x89" "COLE, \xd0\xa1\xd1\x82\xd0\xb0\xd1\x80\xd0\xb5 "
                        "\xd0\x9c\xd1\x96\xd1\x81\xd1\x82\xd0\xbe, \xce\xa9mega, \xc4\xb0stanbul",
                        "s\xc3\xa3o paulo \xc3\xa9" "cole \xd1\x81\xd1\x82\xd0\xb0\xd1\x80\xd0\xb5 "
                        "\xd0\xbc\xd1\x96\xd1\x81\xd1\x82\xd0\xbe \xcf\x89mega istanbul"),
          "буквы любых алфавитов остаются в токене, регистр понижен и у них");
}

TEST("Пробельные символы и типографские знаки") {
    CHECK(expect_tokens("a\tb\nc\xc2\xa0" "d\xe2\x80\x94" "e \xc2\xab" "f\xc2\xbb g\xe2\x80\xa6h", "a b c d e f g h"),
          "табуляция, перевод строки, неразрывный пробел, тире, ёлочки, многоточие — разделители");
}

TEST("Нет токенов") {
    CHECK(tokens("").empty(), "пустой текст");
    CHECK(tokens(" \n-- ... !!! \xe2\x80\x94 ").empty(), "только пробелы и знаки препинания");
}

TEST("Иероглифы без пробелов — один токен") {
    CHECK(expect_tokens("\xe6\x9d\xb1\xe4\xba\xac\xe9\x83\xbd is Tokyo", "\xe6\x9d\xb1\xe4\xba\xac\xe9\x83\xbd is tokyo"),
          "известный недостаток: фраза без пробелов не делится");
}

TEST("Некорректный UTF-8 — разделитель") {
    CHECK(expect_tokens("ab\xff" "cd \xe2\x82 ef\xc3", "ab cd ef"), "битые байты делят токены, обработка не падает");
}

TEST("Длина токена — в символах, а не в байтах") {
    size_t chars[3] = {0, 0, 0}, bytes[3] = {0, 0, 0}, k = 0;
    std::string text = "\xc3\x89" "cole \xe6\x9d\xb1\xe4\xba\xac abc";
    tok::tokenize(text.data(), text.size(), [&](const std::string& t, size_t c) {
        if (k < 3) {
            chars[k] = c;
            bytes[k] = t.size();
        }
        ++k;
    });
    CHECK(k == 3 && chars[0] == 5 && bytes[0] == 6, "école: 5 символов, 6 байт");
    CHECK(chars[1] == 2 && bytes[1] == 6, "два иероглифа: 2 символа, 6 байт");
    CHECK(chars[2] == 3 && bytes[2] == 3, "abc: 3 символа, 3 байта");
}

TEST("Статистика по файлам (режим -s)") {
    ut::TempDir tmp;
    std::string a = tmp.write("a.txt", "Hello, world!\n");
    std::string b = tmp.write("b.txt", "\xd0\x9c\xd0\xbe\xd1\x81\xd0\xba\xd0\xb2\xd0\xb0 \xe2\x80\x94 city 42\n");
    tok::Stats st;
    std::string buf;
    bool ok = tok::count_file(a.c_str(), st, buf) && tok::count_file(b.c_str(), st, buf);
    ut::note("files %zu, bytes %zu, tokens %zu, chars %zu, avg_len %.3f", st.files, st.bytes, st.tokens, st.chars,
             st.avg_len());
    CHECK(ok && st.files == 2, "прочитаны 2 файла");
    CHECK(st.bytes == 39, "39 байт");
    CHECK(st.tokens == 5 && st.chars == 22, "5 токенов (hello world москва city 42), 22 символа");
    CHECK(st.avg_len() == 4.4, "средняя длина 22/5 = 4,4");
    CHECK(!tok::count_file(tmp.path("missing.txt").c_str(), st, buf) && st.files == 2,
          "несуществующий файл: ошибка, статистика не меняется");
}

TEST("Утилита командной строки build/tokenizer") {
    ut::TempDir tmp;
    std::string a = tmp.write("a.txt", "Hello, world!\n");
    std::string list = tmp.write("list.txt", a + "\n");
    std::string missing = tmp.write("missing.txt", tmp.path("none.txt") + "\n");
    int st1 = -1, st2 = -1, st3 = -1, st4 = -1;
    std::string out1 = ut::run("printf 'The Old Town, 1998!' | build/tokenizer", &st1);
    std::string out2 = ut::run("build/tokenizer -s < " + list, &st2);
    ut::run("build/tokenizer -s < " + missing + " 2>/dev/null", &st3);
    ut::run("build/tokenizer -x 2>/dev/null", &st4);
    ut::note("вывод: %s", ut::escape(out1).c_str());
    CHECK(st1 == 0 && out1 == "the\nold\ntown\n1998\n", "токены по одному на строку");
    CHECK(st2 == 0 && out2.find("files\t1\n") != std::string::npos && out2.find("tokens\t2\n") != std::string::npos,
          "-s: статистика по списку файлов");
    CHECK(st3 == 1, "-s с несуществующим файлом: код 1");
    CHECK(st4 == 2, "неверный ключ: код 2");
}

int main() {
    if (!tok::init()) {
        std::fprintf(stderr, "locale C.UTF-8 is not available\n");
        return 2;
    }
    return ut::run_all();
}
