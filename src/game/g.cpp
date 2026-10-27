#include "game/g.h"

#include "engine/sprite.h"
#include "game/card.h"

namespace vl {

Globals G;

bool Globals::paused() const {
    return SETTINGS && SETTINGS->get(K("paused")).truthy();
}

bool Globals::reduced_motion() const {
    return SETTINGS && SETTINGS->get(K("reduced_motion")).truthy();
}

bool Globals::shadows_on() const {
    if (!SETTINGS) return true;
    const Value *g = SETTINGS->find(K("GRAPHICS"));
    const Table *t = g ? g->table() : nullptr;
    const Value *v = t ? t->find(K("shadows")) : nullptr;
    return v && v->equals("On");
}

Value Globals::colour(std::string_view name) const {
    return C ? C->get(Value(intern(name))) : Value();
}

}  // namespace vl
