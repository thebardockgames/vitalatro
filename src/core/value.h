#pragma once
// Lua-like dynamic values for Balatro's game data (G.GAME, card abilities, prototypes, settings,
// saves). Engine objects (Node and subclasses) are separate C++ classes that can be referenced
// from values. Tables keep an array part (keys 1..n) and an insertion-ordered hash part.
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace vl {

// ------------------------------------------------------------------ intrusive refcounting
struct RefCounted {
    mutable int32_t refs = 0;
    virtual ~RefCounted() = default;
};

template <class T>
class Ref {
public:
    Ref() = default;
    Ref(T *p) : p_(p) { if (p_) p_->refs++; }
    Ref(const Ref &o) : p_(o.p_) { if (p_) p_->refs++; }
    Ref(Ref &&o) noexcept : p_(o.p_) { o.p_ = nullptr; }
    template <class U> Ref(const Ref<U> &o) : p_(o.get()) { if (p_) p_->refs++; }
    ~Ref() { release(); }
    Ref &operator=(const Ref &o) { if (o.p_) o.p_->refs++; release(); p_ = o.p_; return *this; }
    Ref &operator=(Ref &&o) noexcept { if (this != &o) { release(); p_ = o.p_; o.p_ = nullptr; } return *this; }
    Ref &operator=(T *p) { if (p) p->refs++; release(); p_ = p; return *this; }
    T *get() const { return p_; }
    T *operator->() const { return p_; }
    T &operator*() const { return *p_; }
    explicit operator bool() const { return p_ != nullptr; }
    bool operator==(const Ref &o) const { return p_ == o.p_; }
    bool operator!=(const Ref &o) const { return p_ != o.p_; }
    bool operator==(const T *p) const { return p_ == p; }
    bool operator!=(const T *p) const { return p_ != p; }

private:
    void release() {
        if (p_ && --p_->refs == 0) delete p_;
        p_ = nullptr;
    }
    T *p_ = nullptr;
};

// ------------------------------------------------------------------ strings
struct Str : RefCounted {
    std::string s;
    uint32_t hash;
    bool interned = false;
    explicit Str(std::string_view v);
};
using StrRef = Ref<Str>;

// Interned string (lives forever): use for table keys and enum-like values.
Str *intern(std::string_view s);
// Compile-time-ish helper: K("name") interns once per call site.
#define K(lit) ([]() -> ::vl::Str * { static ::vl::Str *s_ = ::vl::intern(lit); return s_; }())

// ------------------------------------------------------------------ values
class Table;
struct Object;  // engine object base (defined in engine/node.h), refcounted

enum class VT : uint8_t { Nil, Bool, Num, Str, Table, Obj };

class Value {
public:
    Value() : t_(VT::Nil) { u_.n = 0; }
    Value(std::nullptr_t) : Value() {}
    Value(bool b) : t_(VT::Bool) { u_.n = 0; u_.b = b; }
    Value(double n) : t_(VT::Num) { u_.n = n; }
    Value(float n) : Value((double)n) {}
    Value(int n) : Value((double)n) {}
    Value(int64_t n) : Value((double)n) {}
    Value(uint32_t n) : Value((double)n) {}
    Value(Str *s);
    Value(std::string_view s);
    Value(const char *s) : Value(std::string_view(s)) {}
    Value(const std::string &s) : Value(std::string_view(s)) {}
    Value(Table *t);
    Value(const Ref<Table> &t) : Value(t.get()) {}
    Value(Object *o);
    Value(const Value &o) : t_(o.t_), u_(o.u_) { retain(); }
    Value(Value &&o) noexcept : t_(o.t_), u_(o.u_) { o.t_ = VT::Nil; }
    ~Value() { release(); }
    Value &operator=(const Value &o) {
        if (this != &o) { Value tmp(o); swap(tmp); }
        return *this;
    }
    Value &operator=(Value &&o) noexcept {
        if (this != &o) { release(); t_ = o.t_; u_ = o.u_; o.t_ = VT::Nil; }
        return *this;
    }
    void swap(Value &o) noexcept { std::swap(t_, o.t_); std::swap(u_, o.u_); }

    VT type() const { return t_; }
    bool is_nil() const { return t_ == VT::Nil; }
    bool is_num() const { return t_ == VT::Num; }
    bool is_str() const { return t_ == VT::Str; }
    bool is_table() const { return t_ == VT::Table; }
    bool is_bool() const { return t_ == VT::Bool; }
    bool is_obj() const { return t_ == VT::Obj; }
    // Lua truthiness: everything but nil and false
    bool truthy() const { return !(t_ == VT::Nil || (t_ == VT::Bool && !u_.b)); }
    explicit operator bool() const { return truthy(); }

    double num(double def = 0) const { return t_ == VT::Num ? u_.n : def; }
    bool boolean() const { return t_ == VT::Bool && u_.b; }
    Str *str_ptr() const { return t_ == VT::Str ? u_.s : nullptr; }
    std::string_view str() const { return t_ == VT::Str ? std::string_view(u_.s->s) : std::string_view(); }
    Table *table() const { return t_ == VT::Table ? u_.tab : nullptr; }
    Object *obj() const { return t_ == VT::Obj ? u_.obj : nullptr; }
    template <class T> T *as() const { return t_ == VT::Obj ? dynamic_cast<T *>(u_.obj) : nullptr; }

    // Raw identity/equality as Lua's == (strings by content, tables/objects by identity)
    bool operator==(const Value &o) const;
    bool operator!=(const Value &o) const { return !(*this == o); }
    bool equals(std::string_view s) const { return t_ == VT::Str && u_.s->s == s; }

    // Field access on table values; nil when not a table.
    Value get(const Value &key) const;
    Value operator[](const Value &key) const { return get(key); }
    Value operator[](const char *key) const;
    Value operator[](int i) const;

    size_t hash() const;
    std::string to_string() const;  // Lua tostring() for numbers/strings/booleans

private:
    void retain();
    void release();
    VT t_;
    union {
        bool b;
        double n;
        Str *s;
        Table *tab;
        Object *obj;
    } u_;
};

extern const Value NIL;

// Lua's tostring(number): "%.14g"
std::string num_to_string(double n);

// ------------------------------------------------------------------ tables
class Table : public RefCounted {
public:
    Table() = default;

    Value get(const Value &key) const;
    Value get(Str *key) const;
    // the value at a string key without copying it (no refcount traffic): nullptr when absent
    const Value *find(Str *key) const;
    Value get(const char *key) const { return get(Value(intern(key))); }
    Value get(int i) const;
    void set(const Value &key, Value v);
    void set(const char *key, Value v) { set(Value(intern(key)), std::move(v)); }
    void set(int i, Value v);

    // '#' operator (border of the array part, like LuaJIT for tables built without holes)
    int len() const { return (int)arr_.size(); }
    void push(Value v) { set(len() + 1, std::move(v)); }
    void insert(int pos, Value v);  // table.insert(t, pos, v)
    Value remove(int pos);          // table.remove(t, pos)
    Value remove_last() { return remove(len()); }
    void clear() { arr_.clear(); hkeys_.clear(); hvals_.clear(); index_.clear(); }

    // Iteration like pairs(): array part 1..n, then hash part in insertion order.
    template <class F> void each(F &&f) const {
        for (size_t i = 0; i < arr_.size(); i++)
            if (!arr_[i].is_nil()) f(Value((int)(i + 1)), arr_[i]);
        for (size_t i = 0; i < hkeys_.size(); i++)
            if (!hvals_[i].is_nil()) f(hkeys_[i], hvals_[i]);
    }
    const std::vector<Value> &array() const { return arr_; }
    // next(t) == nil
    bool empty() const {
        for (const Value &v : arr_)
            if (!v.is_nil()) return false;
        for (const Value &v : hvals_)
            if (!v.is_nil()) return false;
        return true;
    }

    Ref<Table> deep_copy() const;  // copy_table()

private:
    int find_hash(const Value &key) const;
    int find_hash_str(const Str *key) const;
    void rehash();
    void migrate_from_hash();

    std::vector<Value> arr_;  // keys 1..n
    std::vector<Value> hkeys_, hvals_;
    std::vector<int32_t> index_;  // open addressing into hkeys_ (power of two, -1 empty)
};

using TableRef = Ref<Table>;
inline TableRef new_table() { return TableRef(new Table()); }

// Build tables inline: T({{"x", 1}, {"y", 2}}) / A({1, 2, 3}).
TableRef T(std::initializer_list<std::pair<const char *, Value>> kv);
TableRef A(std::initializer_list<Value> items);

// ------------------------------------------------------------------ STR_PACK format
// Parse "return {...}" / "{...}" Lua table literals (the subset produced by STR_PACK and by the
// data extractor). Returns nil on error.
Value parse_lua_literal(std::string_view src, std::string *error = nullptr);
// STR_PACK(data): same text format the game writes into its .jkr saves.
std::string str_pack(const Table &t);

}  // namespace vl
