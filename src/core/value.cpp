#include "core/value.h"

#include <cmath>
#include <cstdio>
#include <unordered_map>

#include "core/object.h"

namespace vl {

const Value NIL;

// ------------------------------------------------------------------ strings
static uint32_t hash_bytes(std::string_view s) {
    uint32_t h = 2166136261u;  // FNV-1a
    for (unsigned char c : s) { h ^= c; h *= 16777619u; }
    return h;
}

Str::Str(std::string_view v) : s(v), hash(hash_bytes(v)) {}

Str *intern(std::string_view s) {
    static std::unordered_map<std::string_view, Str *> *pool = new std::unordered_map<std::string_view, Str *>();
    auto it = pool->find(s);
    if (it != pool->end()) return it->second;
    Str *str = new Str(s);
    str->interned = true;
    str->refs = 1 << 30;  // never freed
    (*pool)[std::string_view(str->s)] = str;
    return str;
}

// ------------------------------------------------------------------ values
Value::Value(Str *s) : t_(s ? VT::Str : VT::Nil) { u_.s = s; retain(); }
Value::Value(std::string_view s) : t_(VT::Str) { u_.s = new Str(s); retain(); }
Value::Value(Table *t) : t_(t ? VT::Table : VT::Nil) { u_.tab = t; retain(); }
Value::Value(Object *o) : t_(o ? VT::Obj : VT::Nil) { u_.obj = o; retain(); }

void Value::retain() {
    switch (t_) {
    case VT::Str: u_.s->refs++; break;
    case VT::Table: u_.tab->refs++; break;
    case VT::Obj: u_.obj->refs++; break;
    default: break;
    }
}

void Value::release() {
    switch (t_) {
    case VT::Str: if (--u_.s->refs == 0) delete u_.s; break;
    case VT::Table: if (--u_.tab->refs == 0) delete u_.tab; break;
    case VT::Obj: if (--u_.obj->refs == 0) delete u_.obj; break;
    default: break;
    }
    t_ = VT::Nil;
}

bool Value::operator==(const Value &o) const {
    if (t_ != o.t_) return false;
    switch (t_) {
    case VT::Nil: return true;
    case VT::Bool: return u_.b == o.u_.b;
    case VT::Num: return u_.n == o.u_.n;
    case VT::Str: return u_.s == o.u_.s || (u_.s->hash == o.u_.s->hash && u_.s->s == o.u_.s->s);
    case VT::Table: return u_.tab == o.u_.tab;
    case VT::Obj: return u_.obj == o.u_.obj;
    }
    return false;
}

Value Value::get(const Value &key) const { return t_ == VT::Table ? u_.tab->get(key) : Value(); }
Value Value::operator[](const char *key) const { return t_ == VT::Table ? u_.tab->get(key) : Value(); }
Value Value::operator[](int i) const { return t_ == VT::Table ? u_.tab->get(i) : Value(); }

size_t Value::hash() const {
    switch (t_) {
    case VT::Bool: return u_.b ? 1 : 2;
    case VT::Num: {
        double d = u_.n;
        if (d == 0) d = 0;  // -0 == 0
        uint64_t bits;
        std::memcpy(&bits, &d, 8);
        return (size_t)(bits ^ (bits >> 29));
    }
    case VT::Str: return u_.s->hash;
    case VT::Table: return (size_t)(uintptr_t)u_.tab >> 3;
    case VT::Obj: return (size_t)(uintptr_t)u_.obj >> 3;
    default: return 0;
    }
}

std::string num_to_string(double n) {
    if (std::isinf(n)) return n > 0 ? "inf" : "-inf";
    if (std::isnan(n)) return "nan";
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.14g", n);
    return buf;
}

std::string Value::to_string() const {
    switch (t_) {
    case VT::Nil: return "nil";
    case VT::Bool: return u_.b ? "true" : "false";
    case VT::Num: return num_to_string(u_.n);
    case VT::Str: return u_.s->s;
    case VT::Table: return "table";
    case VT::Obj: return "object";
    }
    return "";
}

// ------------------------------------------------------------------ tables
static bool as_array_index(const Value &k, int &i) {
    if (!k.is_num()) return false;
    double d = k.num();
    if (d < 1 || d > 2147483647.0 || d != std::floor(d)) return false;
    i = (int)d;
    return true;
}

int Table::find_hash(const Value &key) const {
    if (index_.empty()) return -1;
    size_t mask = index_.size() - 1;
    for (size_t h = key.hash() & mask;; h = (h + 1) & mask) {
        int32_t slot = index_[h];
        if (slot < 0) return -1;
        if (hkeys_[slot] == key) return slot;
    }
}

void Table::rehash() {
    // drop deleted (nil) entries, keep insertion order
    size_t w = 0;
    for (size_t r = 0; r < hkeys_.size(); r++) {
        if (hvals_[r].is_nil()) continue;
        if (w != r) { hkeys_[w] = std::move(hkeys_[r]); hvals_[w] = std::move(hvals_[r]); }
        w++;
    }
    hkeys_.resize(w);
    hvals_.resize(w);
    size_t cap = 8;
    while (cap < hkeys_.size() * 2 + 2) cap *= 2;
    index_.assign(cap, -1);
    size_t mask = cap - 1;
    for (size_t i = 0; i < hkeys_.size(); i++) {
        size_t h = hkeys_[i].hash() & mask;
        while (index_[h] >= 0) h = (h + 1) & mask;
        index_[h] = (int32_t)i;
    }
}

Value Table::get(int i) const {
    if (i >= 1 && i <= (int)arr_.size()) return arr_[i - 1];
    if (hkeys_.empty()) return Value();
    int s = find_hash(Value(i));
    return s >= 0 ? hvals_[s] : Value();
}

Value Table::get(Str *key) const {
    if (hkeys_.empty()) return Value();
    int s = find_hash(Value(key));
    return s >= 0 ? hvals_[s] : Value();
}

Value Table::get(const Value &key) const {
    int i;
    if (as_array_index(key, i)) return get(i);
    if (key.is_nil() || hkeys_.empty()) return Value();
    int s = find_hash(key);
    return s >= 0 ? hvals_[s] : Value();
}

// After the array part grows, pull following integer keys out of the hash part.
void Table::migrate_from_hash() {
    if (hkeys_.empty()) return;
    for (;;) {
        int s = find_hash(Value((int)arr_.size() + 1));
        if (s < 0 || hvals_[s].is_nil()) return;
        arr_.push_back(std::move(hvals_[s]));
        hvals_[s] = Value();
    }
}

void Table::set(int i, Value v) {
    if (i >= 1 && i <= (int)arr_.size()) {
        arr_[i - 1] = std::move(v);
        if (i == (int)arr_.size()) {
            while (!arr_.empty() && arr_.back().is_nil()) arr_.pop_back();
        } else if (arr_[i - 1].is_nil()) {
            // a hole: move the tail to the hash part to keep '#' a border
            for (size_t j = i; j < arr_.size(); j++)
                if (!arr_[j].is_nil()) {
                    Value k((int)(j + 1));
                    hkeys_.push_back(k);
                    hvals_.push_back(std::move(arr_[j]));
                }
            arr_.resize(i - 1);
            rehash();
        }
        return;
    }
    if (i == (int)arr_.size() + 1 && !v.is_nil()) {
        arr_.push_back(std::move(v));
        int s = hkeys_.empty() ? -1 : find_hash(Value(i));
        if (s >= 0) hvals_[s] = Value();
        migrate_from_hash();
        return;
    }
    set(Value(i), std::move(v));
}

void Table::set(const Value &key, Value v) {
    int i;
    if (as_array_index(key, i) && (i <= (int)arr_.size() || (i == (int)arr_.size() + 1 && !v.is_nil()))) {
        set(i, std::move(v));
        return;
    }
    if (key.is_nil()) return;
    int s = find_hash(key);
    if (s >= 0) {
        hvals_[s] = std::move(v);
        return;
    }
    if (v.is_nil()) return;
    hkeys_.push_back(key);
    hvals_.push_back(std::move(v));
    if (index_.empty() || hkeys_.size() * 2 + 2 > index_.size()) {
        rehash();
    } else {
        size_t mask = index_.size() - 1;
        size_t h = key.hash() & mask;
        while (index_[h] >= 0) h = (h + 1) & mask;
        index_[h] = (int32_t)(hkeys_.size() - 1);
    }
}

void Table::insert(int pos, Value v) {
    int n = len();
    if (pos < 1 || pos > n + 1) pos = n + 1;
    arr_.insert(arr_.begin() + (pos - 1), std::move(v));
    migrate_from_hash();
}

Value Table::remove(int pos) {
    int n = len();
    if (n == 0 || pos < 1 || pos > n) return Value();
    Value v = std::move(arr_[pos - 1]);
    arr_.erase(arr_.begin() + (pos - 1));
    return v;
}

static Value copy_value(const Value &v) {
    if (v.is_table()) return Value(v.table()->deep_copy());
    return v;
}

TableRef Table::deep_copy() const {
    TableRef t = new_table();
    t->arr_.reserve(arr_.size());
    for (const Value &v : arr_) t->arr_.push_back(copy_value(v));
    for (size_t i = 0; i < hkeys_.size(); i++)
        if (!hvals_[i].is_nil()) t->set(copy_value(hkeys_[i]), copy_value(hvals_[i]));
    return t;
}

TableRef T(std::initializer_list<std::pair<const char *, Value>> kv) {
    TableRef t = new_table();
    for (auto &p : kv) t->set(Value(intern(p.first)), p.second);
    return t;
}

TableRef A(std::initializer_list<Value> items) {
    // like a Lua constructor {a, nil, b}: positional, so b stays at index 3
    TableRef t = new_table();
    int i = 1;
    for (auto &v : items) {
        if (!v.is_nil()) t->set(i, v);
        i++;
    }
    return t;
}

// ------------------------------------------------------------------ literal parser
namespace {
struct Parser {
    std::string_view s;
    size_t p = 0;
    std::string err;

    void ws() {
        for (;;) {
            while (p < s.size() && (s[p] == ' ' || s[p] == '\t' || s[p] == '\n' || s[p] == '\r')) p++;
            if (p + 1 < s.size() && s[p] == '-' && s[p + 1] == '-') {  // comment
                while (p < s.size() && s[p] != '\n') p++;
                continue;
            }
            return;
        }
    }
    bool eat(char c) {
        ws();
        if (p < s.size() && s[p] == c) { p++; return true; }
        return false;
    }
    bool word(std::string_view w) {
        ws();
        if (s.substr(p, w.size()) == w && (p + w.size() >= s.size() || !(std::isalnum((unsigned char)s[p + w.size()]) || s[p + w.size()] == '_'))) {
            p += w.size();
            return true;
        }
        return false;
    }
    Value fail(const char *m) {
        if (err.empty()) err = std::string(m) + " at " + std::to_string(p);
        return Value();
    }
    Value string_lit() {
        char q = s[p++];
        std::string out;
        while (p < s.size() && s[p] != q) {
            char c = s[p++];
            if (c != '\\') { out += c; continue; }
            if (p >= s.size()) break;
            char e = s[p++];
            switch (e) {
            case 'n': out += '\n'; break;
            case 't': out += '\t'; break;
            case 'r': out += '\r'; break;
            case 'a': out += '\a'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'v': out += '\v'; break;
            case '\n': out += '\n'; break;
            case '\r': out += '\n'; if (p < s.size() && s[p] == '\n') p++; break;
            default:
                if (std::isdigit((unsigned char)e)) {
                    int v = e - '0';
                    for (int k = 0; k < 2 && p < s.size() && std::isdigit((unsigned char)s[p]); k++) v = v * 10 + (s[p++] - '0');
                    out += (char)v;
                } else {
                    out += e;
                }
            }
        }
        if (p >= s.size()) return fail("unterminated string");
        p++;
        return Value(out);
    }
    Value number() {
        size_t st = p;
        if (s[p] == '-' || s[p] == '+') p++;
        if (s.substr(p, 3) == "inf") { p += 3; return Value(s[st] == '-' ? -INFINITY : INFINITY); }
        if (s.substr(p, 3) == "nan") { p += 3; return Value(NAN); }
        if (p + 1 < s.size() && s[p] == '0' && (s[p + 1] == 'x' || s[p + 1] == 'X')) {
            p += 2;
            size_t hs = p;
            while (p < s.size() && std::isxdigit((unsigned char)s[p])) p++;
            double v = (double)std::strtoull(std::string(s.substr(hs, p - hs)).c_str(), nullptr, 16);
            return Value(s[st] == '-' ? -v : v);
        }
        while (p < s.size() && (std::isdigit((unsigned char)s[p]) || s[p] == '.' || s[p] == 'e' || s[p] == 'E' ||
                                ((s[p] == '-' || s[p] == '+') && (s[p - 1] == 'e' || s[p - 1] == 'E'))))
            p++;
        std::string num(s.substr(st, p - st));
        char *end = nullptr;
        double v = std::strtod(num.c_str(), &end);
        if (!end || *end) return fail("bad number");
        return Value(v);
    }
    Value value() {
        ws();
        if (p >= s.size()) return fail("unexpected end");
        char c = s[p];
        if (c == '{') return table();
        if (c == '"' || c == '\'') return string_lit();
        if (c == '-' && p + 1 < s.size() && s[p + 1] == '-') { ws(); return value(); }
        if (std::isdigit((unsigned char)c) || c == '-' || c == '.' || c == '+') return number();
        if (word("true")) return Value(true);
        if (word("false")) return Value(false);
        if (word("nil")) return Value();
        if (word("math.huge")) return Value(INFINITY);
        return fail("unexpected token");
    }
    Value table() {
        p++;  // '{'
        TableRef t = new_table();
        int next_index = 1;
        for (;;) {
            ws();
            if (eat('}')) break;
            if (p < s.size() && s[p] == '[') {
                p++;
                Value k = value();
                if (!eat(']') || !eat('=')) return fail("bad key");
                Value v = value();
                t->set(k, v);
            } else if (p < s.size() && (std::isalpha((unsigned char)s[p]) || s[p] == '_') &&
                       !(word_ahead("true") || word_ahead("false") || word_ahead("nil"))) {
                size_t st = p;
                while (p < s.size() && (std::isalnum((unsigned char)s[p]) || s[p] == '_')) p++;
                std::string_view name = s.substr(st, p - st);
                if (!eat('=')) return fail("expected =");
                Value v = value();
                t->set(Value(intern(name)), v);
            } else {
                Value v = value();
                t->set(next_index++, v);
            }
            if (!err.empty()) return Value();
            ws();
            if (eat(',') || eat(';')) continue;
            if (eat('}')) break;
            return fail("expected , or }");
        }
        return Value(t);
    }
    bool word_ahead(std::string_view w) {
        return s.substr(p, w.size()) == w &&
               (p + w.size() >= s.size() || !(std::isalnum((unsigned char)s[p + w.size()]) || s[p + w.size()] == '_'));
    }
};
}  // namespace

Value parse_lua_literal(std::string_view src, std::string *error) {
    Parser ps{src};
    ps.word("return");
    Value v = ps.value();
    if (!ps.err.empty()) {
        if (error) *error = ps.err;
        return Value();
    }
    return v;
}

// ------------------------------------------------------------------ STR_PACK
static void quote(std::string &out, std::string_view s) {
    // string.format("%q", s) as LuaJIT does it
    out += '"';
    for (size_t i = 0; i < s.size(); i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"' || c == '\\') { out += '\\'; out += (char)c; }
        else if (c == '\n') out += "\\\n";
        else if (c == '\r') out += "\\r";
        else if (c == 0) out += (i + 1 < s.size() && std::isdigit((unsigned char)s[i + 1])) ? "\\000" : "\\0";
        else if (c < 32 || c == 127) {
            char buf[8];
            bool digit_next = i + 1 < s.size() && std::isdigit((unsigned char)s[i + 1]);
            std::snprintf(buf, sizeof buf, digit_next ? "\\%03d" : "\\%d", c);
            out += buf;
        } else out += (char)c;
    }
    out += '"';
}

static void pack(std::string &out, const Table &t) {
    out += '{';
    t.each([&](const Value &k, const Value &v) {
        if (k.is_str()) { out += '['; quote(out, k.str()); out += ']'; }
        else { out += '['; out += k.to_string(); out += ']'; }
        out += '=';
        switch (v.type()) {
        case VT::Table: pack(out, *v.table()); break;
        case VT::Str: quote(out, v.str()); break;
        case VT::Bool: out += v.boolean() ? "true" : "false"; break;
        case VT::Num: out += num_to_string(v.num()); break;
        case VT::Obj: out += "\"MANUAL_REPLACE\""; break;
        default: out += "nil"; break;
        }
        out += ',';
    });
    out += '}';
}

std::string str_pack(const Table &t) {
    std::string out = "return ";
    pack(out, t);
    return out;
}

}  // namespace vl
