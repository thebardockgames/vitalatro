#pragma once
#include <functional>

#include "core/value.h"

namespace vl {

// Root of everything a Value can reference as an "object" (Node and all engine/game classes).
struct Object : RefCounted {
    ~Object() override = default;
    // obj[key] for the typed fields UI definitions read by name (ref_table = card, ref_value = ...)
    virtual Value get_field(const Value &) const { return Value(); }
};

// A callable stored in a dynamic table (Lua function values such as config.release_func).
struct Func : Object {
    explicit Func(std::function<Value(const Value &)> f) : fn(std::move(f)) {}
    Value call(const Value &arg = Value()) { return fn(arg); }
    std::function<Value(const Value &)> fn;
};

}  // namespace vl
