// engine/text.lua
#include "engine/text.h"

#include <algorithm>
#include <cmath>

#include "engine/random.h"
#include "engine/ui.h"
#include "game/g.h"
#include "game/sound.h"

namespace vl {

namespace {
inline Value cfg(const TableRef &c, const char *k) { return c->get(Value(intern(k))); }
inline double cnum(const TableRef &c, const char *k, double d) {
    Value v = cfg(c, k);
    return v.is_num() ? v.num() : d;
}
}  // namespace

DynaText::DynaText(TableRef c) {
    if (!c) c = new_table();
    config = c;
    shadow = cfg(c, "shadow").truthy();
    scale = (float)cnum(c, "scale", 1);
    pop_in_rate = (float)cnum(c, "pop_in_rate", 3);
    bump_rate = (float)cnum(c, "bump_rate", 2.666);
    bump_amount = (float)cnum(c, "bump_amount", 1);
    FontDef *f = cfg(c, "font").as<FontDef>();
    font = f ? f : G.LANG->font;
    Value s = cfg(c, "string");
    if (s.truthy() && !s.is_table()) {
        c->set("string", Value(A({s})));
        s = cfg(c, "string");
    }
    text_offset = {(float)(font->TEXT_OFFSET.x * scale + cnum(c, "x_offset", 0)),
                   (float)(font->TEXT_OFFSET.y * scale + cnum(c, "y_offset", 0))};
    colours = cfg(c, "colours").truthy() ? cfg(c, "colours") : Value(A({G.colour("RED")}));
    created_time = G.TIMERS.REAL;
    silent = cfg(c, "silent").truthy();
    start_pop_in = cfg(c, "pop_in").truthy();

    c->set("W", 0);
    c->set("H", 0);
    focused_string = 1;

    update_text(true);
    if (cfg(c, "maxw").is_num() && cnum(c, "W", 0) > cfg(c, "maxw").num()) {
        start_pop_in = cfg(c, "pop_in").truthy();
        scale = (float)(scale * (cfg(c, "maxw").num() / cnum(c, "W", 0)));
        update_text(true);
    }
    if (strings.size() > 1) {
        pop_delay = cnum(c, "pop_delay", 1.5);
        has_pop_delay = true;
        pop_out(4);
    }

    Moveable::init((float)cnum(c, "X", 0), (float)cnum(c, "Y", 0), (float)cnum(c, "W", 0), (float)cnum(c, "H", 0));
    config = c;  // Node.init does not touch self.config in the original
    moveable_ready_ = true;
    T->r = (float)cnum(c, "text_rot", 0);

    states.hover.can = false;
    states.click.can = false;
    states.collide.can = false;
    states.drag.can = false;
    states.release_on.can = false;

    Bond weak = Bond::Weak;
    RoleArgs ra;
    ra.wh_bond = &weak;
    ra.scale_bond = &weak;
    set_role(ra);

    G.I.MOVEABLE.add(this);
}

void DynaText::update(double) {
    update_text();
    align_letters();
}

void DynaText::update_text(bool first_pass) {
    TableRef c = config;
    float W = 0, H = 0;
    Table *list = cfg(c, "string").table();
    int n = list ? (int)list->len() : 0;
    if ((int)strings.size() < n) strings.resize(n);
    float TS = G.TILESIZE, TSC = G.TILESCALE;

    for (int k = 1; k <= n; k++) {
        Value v = list->get(k);
        Table *vt = v.table();
        if ((vt && vt->get(K("ref_table")).truthy()) || first_pass) {
            size_t part_a = 0, part_b = 1000000;
            std::string new_string;
            Value outer_colour, inner_colour;
            float part_scale = 1;
            if (vt && (vt->get(K("ref_table")).truthy() || vt->get(K("string")).truthy())) {
                Value prefix = vt->get(K("prefix")), suffix = vt->get(K("suffix"));
                std::string pre = prefix.truthy() ? prefix.to_string() : "";
                std::string suf = suffix.truthy() ? suffix.to_string() : "";
                Value rt = vt->get(K("ref_table"));
                Value body = rt.truthy() && rt.get(vt->get(K("ref_value"))).truthy() ? rt.get(vt->get(K("ref_value")))
                                                                                     : vt->get(K("string"));
                new_string = pre + body.to_string() + suf;
                part_a = pre.size();
                part_b = new_string.size() - suf.size();
                if (vt->get(K("scale")).is_num()) part_scale = (float)vt->get(K("scale")).num();
                if (first_pass) {
                    outer_colour = vt->get(K("outer_colour"));
                    inner_colour = vt->get(K("colour"));
                }
            } else {
                new_string = v.to_string();
            }

            String &S = strings[k - 1];
            if (!S.has_string || S.string != new_string || first_pass) {
                if (start_pop_in) reset_pop_in = true;
                reset_pop_in = reset_pop_in || cfg(c, "reset_pop_in").truthy();
                if (!reset_pop_in) {
                    c->set("pop_out", Value());
                    c->set("pop_in", Value());
                } else {
                    if (!cfg(c, "pop_in").truthy()) c->set("pop_in", 0);
                    created_time = G.TIMERS.REAL;
                }
                S.string = new_string;
                S.has_string = true;
                std::vector<Letter> old_letters = std::move(S.letters);
                float tempW = 0, tempH = 0;
                S.letters.clear();
                double spacing = cnum(c, "spacing", 0);
                bool cfg_pop_in = cfg(c, "pop_in").truthy();
                size_t i = 0;
                int current_letter = 1;
                while (i < new_string.size()) {
                    size_t start = i;
                    utf8_next(new_string, i);
                    std::string ch = new_string.substr(start, i - start);
                    const Letter *old = current_letter <= (int)old_letters.size() ? &old_letters[current_letter - 1] : nullptr;
                    Letter L;
                    L.letter = new Text(Ref<Font>(font->font()), ch);
                    L.ch = ch;
                    L.scale = old ? old->scale : part_scale;
                    float tx = font->font()->width(ch) * scale * part_scale * TSC * font->FONTSCALE +
                               2.7f * (float)spacing * TSC * font->FONTSCALE;
                    float ty = font->font()->height() * scale * part_scale * TSC * font->FONTSCALE * font->TEXT_HEIGHT_SCALE;
                    L.offset = old ? old->offset : XY{0, 0};
                    L.dims = {tx / (font->FONTSCALE * TSC), ty / (font->FONTSCALE * TSC)};
                    L.pop_in = first_pass ? (old ? old->pop_in : (cfg_pop_in ? 0.f : 1.f)) : 1.f;
                    if ((size_t)current_letter <= part_a) L.prefix = outer_colour;
                    if ((size_t)current_letter > part_b) L.suffix = outer_colour;
                    L.colour = inner_colour;
                    if (k > 1) L.pop_in = 0;
                    tempW += tx / (TS * TSC);
                    tempH = std::max(ty / (TS * TSC), tempH);
                    S.letters.push_back(std::move(L));
                    current_letter++;
                }
                S.W = tempW;
                S.H = tempH;
            }
        }
        String &S = strings[k - 1];
        if (S.W > W) {
            W = S.W;
            S.W_offset = 0;
        }
        if (S.H > H) {
            H = S.H;
            S.H_offset = 0;
        }
    }
    c->set("W", (double)W);
    c->set("H", (double)H);

    if (moveable_ready_) {
        if ((T->w != W || T->h != H) && (!first_pass || reset_pop_in)) {
            set_var("ui_object_updated", true);
            set_var("non_recalc", cfg(c, "non_recalc"));
        }
        T->w = W;
        T->h = H;
    }
    reset_pop_in = false;
    start_pop_in = false;

    double offset_y = cnum(c, "offset_y", 0);
    for (auto &S : strings) {
        S.W_offset = 0.5f * (W - S.W);
        S.H_offset = (float)(0.5 * (H - S.H + offset_y));
    }
}

void DynaText::pop_out(double t) {
    config->set("pop_out", t);
    pop_out_time = G.TIMERS.REAL + (has_pop_delay ? pop_delay : 0);
}

void DynaText::pop_in(double t) {
    reset_pop_in = true;
    config->set("pop_out", Value());
    config->set("pop_in", t);
    created_time = G.TIMERS.REAL;
    for (auto &L : strings[focused_string - 1].letters) L.pop_in = 0;
    update_text();
}

void DynaText::align_letters() {
    TableRef c = config;
    int ns = (int)strings.size();
    if (ns == 0) return;
    if (pop_cycle) {
        if (cfg(c, "random_element").truthy()) focused_string = (int)lua_math.random(1, ns);
        else focused_string = focused_string == ns ? 1 : focused_string + 1;
        pop_cycle = false;
        for (auto &L : strings[focused_string - 1].letters) L.pop_in = 0;
        c->set("pop_in", 0.1);
        c->set("pop_out", Value());
        created_time = G.TIMERS.REAL;
    }
    String &S = strings[focused_string - 1];
    string = S.string;
    const double R = G.TIMERS.REAL;
    const double motion = G.reduced_motion() ? 0 : 1;
    const int nl = (int)S.letters.size();
    const double slen = (double)string.size();
    for (int k = 1; k <= nl; k++) {
        Letter &L = S.letters[k - 1];
        Value pop_out_v = cfg(c, "pop_out"), pop_in_v = cfg(c, "pop_in");
        Value mct = cfg(c, "min_cycle_time");
        if (pop_out_v.truthy()) {
            double m = mct.is_num() ? mct.num() : 1;
            L.pop_in = (float)std::min(1.0, std::max(m - (R - pop_out_time) * pop_out_v.num() / m, 0.0));
            L.pop_in = L.pop_in * L.pop_in;
            if (k == nl && L.pop_in <= 0 && ns > 1) pop_cycle = true;
        } else if (pop_in_v.truthy()) {
            float prev = L.pop_in;
            L.pop_in = (float)std::min(1.0, std::max((R - pop_in_v.num() - created_time) * slen * pop_in_rate - k + 1,
                                                     (mct.is_num() && mct.num() == 0) ? 1.0 : 0.0));
            L.pop_in = L.pop_in * L.pop_in;
            if (prev <= 0 && L.pop_in > 0 && !silent && (slen < 10 || k % 2 == 0)) {
                if (!(T->x > G.ROOM->T->w + 2 || T->y > G.ROOM->T->h + 2 || T->x < -2 || T->y < -2)) {
                    play_sound("paper1", (float)(0.45 + 0.05 * lua_math.random() + (0.3 / slen) * k + cnum(c, "pitch_shift", 0)));
                }
            }
            if (k == nl && L.pop_in >= 1) {
                if (ns > 1) {
                    pop_delay = R - pop_in_v.num() - created_time + cnum(c, "pop_delay", 1.5);
                    has_pop_delay = true;
                    pop_out(4);
                } else {
                    c->set("pop_in", Value());
                }
            }
        }
        L.r = 0;
        L.scale = 1;
        Value rot = cfg(c, "rotate");
        if (rot.truthy())
            L.r = (float)((rot.is_num() && rot.num() == 2 ? -1 : 1) *
                          (0.2 * (-nl / 2.0 - 0.5 + k) / nl + motion * 0.02 * std::sin(2 * R + k)));
        if (Table *p = cfg(c, "pulse").table()) {
            double width = p->get(K("width")).num(), amount = p->get(K("amount")).num();
            double start = p->get(K("start")).num(), speed = p->get(K("speed")).num();
            L.scale = (float)(L.scale + motion * (1 / width) * amount *
                                            std::max(std::min((start - R) * speed + k + width, (R - start) * speed - k + width + 2), 0.0) *
                                            (font->render_scale / (G.TILESIZE * 10)));
            L.r = (float)(L.r + motion * (L.scale - 1) * (0.02 * (-nl / 2.0 - 0.5 + k)));
            if (start > R + 2 * speed * nl) c->set("pulse", Value());
        }
        if (Table *q = cfg(c, "quiver").table()) {
            double amount = q->get(K("amount")).num(), speed = q->get(K("speed")).num();
            L.scale = (float)(L.scale + motion * (0.1 * amount));
            L.r = (float)(L.r + motion * 0.3 * amount *
                                    (std::sin(41.12342 * R * speed + k * 1223.2) +
                                     std::cos(63.21231 * R * speed + k * 1112.2) * std::sin(36.1231 * R * speed) +
                                     std::cos(95.123 * R * speed + k * 1233.2) - std::sin(30.133421 * R * speed + k * 123.2)));
        }
        if (cfg(c, "float").truthy())
            L.offset.y = (float)(motion * std::sqrt(scale) * (7 * font->render_scale / (G.TILESIZE * 10)) * (1.5 * std::sin(2.666 * R + 200 * k)));
        if (cfg(c, "bump").truthy())
            L.offset.y = (float)(motion * bump_amount * std::sqrt(scale) *
                                 ((7 * font->render_scale / (G.TILESIZE * 10)) *
                                  std::max(0.0, (5 + bump_rate) * std::sin(bump_rate * R + 200 * k) - 3 - bump_rate)));
    }
}

void DynaText::set_quiver(double amt) {
    config->set("quiver", Value(vl::T({{"speed", 0.5}, {"amount", amt}, {"silent", false}})));
}

void DynaText::pulse(double amt) {
    config->set("pulse", Value(vl::T({{"speed", 40}, {"width", 2.5}, {"start", G.TIMERS.REAL}, {"amount", amt}, {"silent", false}})));
}

void DynaText::draw() {
    if (Node *pe = children.get(K("particle_effect"))) pe->draw();
    if (strings.empty()) return;
    const String &S = strings[focused_string - 1];
    const float TS = G.TILESIZE, FS = font->FONTSCALE;
    Value mct = config->get(K("min_cycle_time"));
    bool instant = mct.is_num() && mct.num() == 0;
    Value spacing = config->get(K("spacing"));

    if (shadow) {
        prep_draw(this, 1);
        gfx::translate(S.W_offset + text_offset.x * FS / TS, S.H_offset + text_offset.y * FS / TS);
        if (spacing.truthy()) gfx::translate((float)spacing.num() * FS / TS, 0);
        Value sc = config->get(K("shadow_colour"));
        if (sc.truthy()) set_color(sc);
        else gfx::set_color({0, 0, 0, 0.3f * (float)colours[1][4].num(1)});
        for (const Letter &L : S.letters) {
            float p = instant ? 1 : L.pop_in;
            L.letter->draw(0.5f * (L.dims.x - L.offset.x) * FS / TS - shadow_parrallax->x * scale / TS,
                           0.5f * L.dims.y * FS / TS - shadow_parrallax->y * scale / TS, L.r, p * scale * FS / TS,
                           p * scale * FS / TS, 0.5f * L.dims.x / scale, 0.5f * L.dims.y / scale);
            gfx::translate(L.dims.x * FS / TS, 0);
        }
        gfx::pop();
    }

    prep_draw(this, 1);
    gfx::translate(S.W_offset + text_offset.x * FS / TS, S.H_offset + text_offset.y * FS / TS);
    if (spacing.truthy()) gfx::translate((float)spacing.num() * FS / TS, 0);
    float len = std::sqrt(shadow_parrallax->y * shadow_parrallax->y + shadow_parrallax->x * shadow_parrallax->x);
    XY norm{shadow_parrallax->x / len * FS / TS, shadow_parrallax->y / len * FS / TS};
    Table *cols = colours.table();
    int ncol = cols ? (int)cols->len() : 0;
    int k = 1;
    for (const Letter &L : S.letters) {
        float p = instant ? 1 : L.pop_in;
        if (L.prefix.truthy()) set_color(L.prefix);
        else if (L.suffix.truthy()) set_color(L.suffix);
        else if (L.colour.truthy()) set_color(L.colour);
        else if (ncol) set_color(cols->get(k % ncol + 1));
        L.letter->draw(0.5f * (L.dims.x - L.offset.x) * FS / TS + norm.x, 0.5f * (L.dims.y - L.offset.y) * FS / TS + norm.y, L.r,
                       p * L.scale * scale * FS / TS, p * L.scale * scale * FS / TS, 0.5f * L.dims.x / scale,
                       0.5f * L.dims.y / scale);
        gfx::translate(L.dims.x * FS / TS, 0);
        k++;
    }
    gfx::pop();

    add_to_drawhash(this);
    draw_boundingrect();
}

}  // namespace vl
