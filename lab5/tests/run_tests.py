#!/usr/bin/env python3
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ZIPF = os.path.join(HERE, "..", "scripts", "zipf.py")


def make_corpus(tmp, files):
    corpus = os.path.join(tmp, "corpus")
    for name, text in files.items():
        path = os.path.join(corpus, name)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8") as f:
            f.write(text)
    return corpus


def run(corpus, out):
    return subprocess.run([sys.executable, ZIPF, corpus, out], capture_output=True, text=True)


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def report(no, name, checks):
    ok = True
    for cond, text in checks:
        print(f"  [{'ok' if cond else 'FAIL'}] {text}")
        ok = ok and cond
    print(f"--- Тест {no}: {'OK' if ok else 'FAIL'}\n")
    return ok


def test_freq(no, tmp):
    print(f"=== Тест {no}. Частоты и ранги на маленьком корпусе ===")
    corpus = make_corpus(tmp, {"a/1.txt": "The cat and the dog.\n", "b/2.txt": "THE END: the cat\n",
                               "b/notes.md": "ignored ignored\n"})
    out = os.path.join(tmp, "out")
    res = run(corpus, out)
    print("  " + res.stdout.strip().replace("\n", "\n  "))
    freq = read(os.path.join(out, "freq.tsv")) if res.returncode == 0 else ""
    print("  freq.tsv: " + freq.strip().replace("\n", " | "))
    want = "rank\tterm\tfreq\n1\tthe\t4\n2\tcat\t2\n3\tand\t1\n4\tdog\t1\n5\tend\t1\n"
    return report(no, "", [
        (res.returncode == 0, "код возврата 0"),
        (freq == want, "freq.tsv: по убыванию частоты, равные частоты по алфавиту, регистр понижен, .md не читается"),
        ("tokens\t9\n" in res.stdout and "terms\t5\n" in res.stdout and "hapax\t3\n" in res.stdout
         and "C\t4\n" in res.stdout, "tokens = 9, terms = 5, hapax = 3, C = f(1) = 4"),
    ])


def test_plot(no, tmp):
    print(f"=== Тест {no}. Точки графика zipf.dat ===")
    words = " ".join(f"w{i} " * (200 // i) for i in range(1, 201))
    corpus = make_corpus(tmp, {"1.txt": words})
    out = os.path.join(tmp, "out")
    res = run(corpus, out)
    lines = read(os.path.join(out, "zipf.dat")).split("\n") if res.returncode == 0 else ["", ""]
    rows = [[float(x) for x in line.split()] for line in lines[1:] if line]
    print(f"  {len(rows)} точек: первая {rows[0] if rows else None}, последняя {rows[-1] if rows else None}")
    ranks = [r[0] for r in rows]
    return report(no, "", [
        (lines[0] == "rank freq zipf", "заголовок rank freq zipf"),
        (bool(rows) and rows[0] == [1, 200, 200], "первая точка: ранг 1, f = C = 200"),
        (bool(rows) and rows[-1][0] == 200, "последняя точка: ранг 200 = число терминов"),
        (ranks == sorted(set(ranks)), "ранги строго возрастают"),
        (all(abs(z - 200 / r) < 1e-3 for r, _, z in rows), "zipf = C / r в каждой точке"),
        (all(f == 200 // r for r, f, _ in rows), "f совпадает с заданной частотой слова w_r = 200 div r"),
    ])


def test_boundary(no, tmp):
    print(f"=== Тест {no}. Граница файлов без перевода строки ===")
    corpus = make_corpus(tmp, {"1.txt": "foo", "2.txt": "bar"})
    out = os.path.join(tmp, "out")
    res = run(corpus, out)
    freq = read(os.path.join(out, "freq.tsv")) if res.returncode == 0 else ""
    print("  freq.tsv: " + freq.strip().replace("\n", " | "))
    return report(no, "", [(freq == "rank\tterm\tfreq\n1\tbar\t1\n2\tfoo\t1\n",
                            "последний токен файла не склеивается с первым токеном следующего")])


def test_errors(no, tmp):
    print(f"=== Тест {no}. Ошибки: пустой каталог, корпус без токенов ===")
    empty = os.path.join(tmp, "empty")
    os.makedirs(empty)
    res1 = run(empty, os.path.join(tmp, "out1"))
    res2 = run(make_corpus(tmp, {"1.txt": "... --- !!!\n"}), os.path.join(tmp, "out2"))
    print(f"  stderr: {res1.stderr.strip()!r}, {res2.stderr.strip()!r}")
    return report(no, "", [
        (res1.returncode != 0 and "no .txt files" in res1.stderr, "пустой каталог: ошибка, сообщение в stderr"),
        (res2.returncode != 0 and "no tokens" in res2.stderr, "нет токенов: ошибка, сообщение в stderr"),
    ])


def main():
    results = []
    for no, test in enumerate([test_freq, test_plot, test_boundary, test_errors], 1):
        with tempfile.TemporaryDirectory() as tmp:
            results.append(test(no, tmp))
    print(f"Итого: пройдено {sum(results)} из {len(results)}")
    sys.exit(0 if all(results) else 1)


if __name__ == "__main__":
    main()
