#pragma once

#include "index_reader.h"
#include "porter.h"
#include "tokenizer.h"

typedef Vec<uint32_t> List;


enum Kind { TERM, AND, OR, NOT };

struct Node {
    Kind kind = TERM;
    char* term = nullptr;
    size_t len = 0;
    Vec<Node*> kids;
};

inline Node* make(Kind k) {
    Node* n = new Node();
    n->kind = k;
    return n;
}

inline void destroy(Node* n) {
    if (!n) return;
    for (size_t i = 0; i < n->kids.size; ++i) destroy(n->kids[i]);
    std::free(n->term);
    delete n;
}

inline Node* combine(Kind k, Vec<Node*>& items) {
    Node* res = nullptr;
    if (items.size == 1) {
        res = items[0];
    } else if (items.size > 1) {
        res = make(k);
        for (size_t i = 0; i < items.size; ++i) {
            Node* x = items[i];
            if (x->kind == k) {
                res->kids.append(x->kids.data, x->kids.size);
                x->kids.size = 0;
                destroy(x);
            } else {
                res->kids.push(x);
            }
        }
    }
    items.size = 0;
    return res;
}

inline Node* word_node(const char* w, size_t len, bool stem) {
    Vec<Node*> items;
    tok::tokenize(w, len, [&](const std::string& t, size_t) {
        Node* x = make(TERM);
        x->term = static_cast<char*>(xrealloc(nullptr, t.size() + 1));
        std::memcpy(x->term, t.data(), t.size() + 1);
        x->len = stem ? porter::stem(x->term, t.size()) : t.size();
        x->term[x->len] = 0;
        items.push(x);
    });
    return combine(AND, items);
}

enum Tok { T_END, T_WORD, T_AND, T_OR, T_NOT, T_LP, T_RP };

struct Parser {
    const char* s;
    size_t n;
    size_t i = 0;
    Tok tok = T_END;
    const char* word = nullptr;
    size_t wlen = 0;

    bool stem;

    Parser(const char* str, size_t len, bool st = false) : s(str), n(len), stem(st) { next(); }

    void next() {
        while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) ++i;
        if (i >= n) {
            tok = T_END;
            return;
        }
        char c = s[i];
        if (c == '(' || c == ')' || c == '!') {
            ++i;
            tok = c == '(' ? T_LP : (c == ')' ? T_RP : T_NOT);
        } else if (c == '&' || c == '|') {
            while (i < n && s[i] == c) ++i;
            tok = c == '&' ? T_AND : T_OR;
        } else {
            size_t start = i;
            while (i < n && !std::strchr(" \t\r\n()!&|", s[i])) ++i;
            word = s + start;
            wlen = i - start;
            tok = T_WORD;
        }
    }

    Node* query() {
        Vec<Node*> items;
        while (tok != T_END) {
            Node* x = parse_or();
            if (x) items.push(x);
            if (tok == T_RP) next();
        }
        return combine(AND, items);
    }

    Node* parse_or() {
        Vec<Node*> items;
        Node* x = parse_and();
        if (x) items.push(x);
        while (tok == T_OR) {
            next();
            x = parse_and();
            if (x) items.push(x);
        }
        return combine(OR, items);
    }

    Node* parse_and() {
        Vec<Node*> items;
        for (;;) {
            if (tok == T_AND) {
                next();
                continue;
            }
            if (tok != T_WORD && tok != T_NOT && tok != T_LP) break;
            Node* x = parse_unary();
            if (x) items.push(x);
        }
        return combine(AND, items);
    }

    Node* parse_unary() {
        if (tok != T_NOT) return parse_primary();
        next();
        Node* x = parse_unary();
        if (!x) return nullptr;
        if (x->kind == NOT) {
            Node* y = x->kids[0];
            x->kids.size = 0;
            destroy(x);
            return y;
        }
        Node* res = make(NOT);
        res->kids.push(x);
        return res;
    }

    Node* parse_primary() {
        if (tok == T_LP) {
            next();
            Node* x = parse_or();
            if (tok == T_RP) next();
            return x;
        }
        if (tok == T_WORD) {
            const char* w = word;
            size_t len = wlen;
            next();
            return word_node(w, len, stem);
        }
        return nullptr;
    }
};

inline void show(const Node* n, Vec<char>& out) {
    if (n->kind == TERM) {
        out.append(n->term, n->len);
        return;
    }
    if (n->kind == NOT) {
        const Node* c = n->kids[0];
        bool paren = c->kind == AND || c->kind == OR;
        out.push('!');
        if (paren) out.push('(');
        show(c, out);
        if (paren) out.push(')');
        return;
    }
    const char* sep = n->kind == AND ? " && " : " || ";
    for (size_t i = 0; i < n->kids.size; ++i) {
        if (i) out.append(sep, 4);
        bool paren = n->kids[i]->kind == AND || n->kids[i]->kind == OR;
        if (paren) out.push('(');
        show(n->kids[i], out);
        if (paren) out.push(')');
    }
}

inline void swap_lists(List& a, List& b) {
    uint32_t* d = a.data;
    size_t s = a.size, c = a.cap;
    a.data = b.data;
    a.size = b.size;
    a.cap = b.cap;
    b.data = d;
    b.size = s;
    b.cap = c;
}

inline void intersect(const List& a, const List& b, List& out) {
    out.size = 0;
    out.reserve(a.size < b.size ? a.size : b.size);
    size_t i = 0, j = 0;
    while (i < a.size && j < b.size) {
        if (a[i] < b[j]) ++i;
        else if (a[i] > b[j]) ++j;
        else {
            out.data[out.size++] = a[i];
            ++i;
            ++j;
        }
    }
}

inline void unite(const List& a, const List& b, List& out) {
    out.size = 0;
    out.reserve(a.size + b.size);
    size_t i = 0, j = 0;
    while (i < a.size || j < b.size) {
        if (j == b.size || (i < a.size && a[i] < b[j])) out.data[out.size++] = a[i++];
        else if (i == a.size || b[j] < a[i]) out.data[out.size++] = b[j++];
        else {
            out.data[out.size++] = a[i++];
            ++j;
        }
    }
}

inline void subtract(const List& a, const List& b, List& out) {
    out.size = 0;
    out.reserve(a.size);
    size_t j = 0;
    for (size_t i = 0; i < a.size; ++i) {
        while (j < b.size && b[j] < a[i]) ++j;
        if (j == b.size || b[j] != a[i]) out.data[out.size++] = a[i];
    }
}

inline void all_docs(const Index& ix, List& out) {
    out.size = 0;
    out.reserve(ix.docs);
    for (uint32_t d = 0; d < ix.docs; ++d) out.data[out.size++] = d;
}

inline void eval(const Index& ix, const Node* n, List& out) {
    out.size = 0;
    List tmp;
    if (n->kind == TERM) {
        long k = ix.find(n->term, n->len);
        if (k < 0) return;
        uint32_t df = ix.df(size_t(k));
        out.reserve(df);
        if (ix.read_list(size_t(k), out.data)) out.size = df;
        else std::fprintf(stderr, "search: document list of %s is damaged\n", n->term);
    } else if (n->kind == NOT) {
        List all, sub;
        all_docs(ix, all);
        eval(ix, n->kids[0], sub);
        subtract(all, sub, out);
    } else if (n->kind == OR) {
        List part;
        for (size_t i = 0; i < n->kids.size; ++i) {
            eval(ix, n->kids[i], part);
            unite(out, part, tmp);
            swap_lists(out, tmp);
        }
    } else {
        Vec<List*> pos;
        for (size_t i = 0; i < n->kids.size; ++i) {
            if (n->kids[i]->kind == NOT) continue;
            List* l = new List();
            eval(ix, n->kids[i], *l);
            pos.push(l);
        }
        for (size_t i = 1; i < pos.size; ++i)
            for (size_t j = i; j > 0 && pos[j]->size < pos[j - 1]->size; --j) {
                List* t = pos[j];
                pos[j] = pos[j - 1];
                pos[j - 1] = t;
            }
        if (pos.size == 0) all_docs(ix, out);
        else swap_lists(out, *pos[0]);
        for (size_t i = 1; i < pos.size && out.size; ++i) {
            intersect(out, *pos[i], tmp);
            swap_lists(out, tmp);
        }
        List part;
        for (size_t i = 0; i < n->kids.size && out.size; ++i) {
            if (n->kids[i]->kind != NOT) continue;
            eval(ix, n->kids[i]->kids[0], part);
            subtract(out, part, tmp);
            swap_lists(out, tmp);
        }
        for (size_t i = 0; i < pos.size; ++i) delete pos[i];
    }
}

inline Node* parse_query(const char* q, size_t len, bool stem) {
    Parser p(q, len, stem);
    return p.query();
}

inline bool parse_count(const char* s, size_t& out) {
    char* end;
    unsigned long long v = std::strtoull(s, &end, 10);
    if (end == s || *end) return false;
    out = size_t(v);
    return true;
}

inline void page_range(size_t found, size_t offset, size_t limit, size_t& begin, size_t& end) {
    begin = offset < found ? offset : found;
    end = limit && offset + limit < found ? offset + limit : found;
    if (end < begin) end = begin;
}

inline const char* parse_request(char* line, size_t& offset, size_t& limit) {
    char* t1 = std::strchr(line, '\t');
    char* t2 = t1 ? std::strchr(t1 + 1, '\t') : nullptr;
    if (!t2) return line;
    size_t default_limit = limit;
    *t1 = *t2 = 0;
    if (!parse_count(line, offset)) offset = 0;
    if (!parse_count(t1 + 1, limit)) limit = default_limit;
    return t2 + 1;
}
