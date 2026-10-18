#pragma once
#include "core/value.h"

namespace vl {

// Root of everything a Value can reference as an "object" (Node and all engine/game classes).
struct Object : RefCounted {
    ~Object() override = default;
};

}  // namespace vl
