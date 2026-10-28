// engine/ui.lua
#include "engine/ui.h"

#include "core/time.h"

#include <cmath>
#include <cstdio>

#include "engine/controller.h"
#include "engine/font.h"
#include "game/g.h"
#include "game/sound.h"
#include "game/ui_definitions.h"
#include "gfx/gfx.h"
#include "platform/platform.h"

namespace vl {

// ------------------------------------------------------------------ small helpers
namespace {
inline Value cv(const TableRef &c, Str *k) { return c->get(k); }
inline float cf(const TableRef &c, Str *k, float d) {
    const Value *v = c->find(k);
    return v && v->is_num() ? (float)v->num() : d;
}
inline bool cb(const TableRef &c, Str *k) {
    const Value *v = c->find(k);
    return v && v->truthy();
}
inline Value gcol(Str *a, Str *b = nullptr) {
    const Value *v = G.C->find(a);
    if (!v) return Value();
    if (!b) return *v;
    const Table *t = v->table();
    const Value *w = t ? t->find(b) : nullptr;
    return w ? *w : Value();
}
Bond parse_bond(const Value &v, Bond def) {
    if (v.equals("Strong")) return Bond::Strong;
    if (v.equals("Weak")) return Bond::Weak;
    return def;
}
}  // namespace

Color colour_of(const Value &c) {
    Table *t = c.table();
    if (!t) return {1, 1, 1, 1};
    return {(float)t->get(1).num(), (float)t->get(2).num(), (float)t->get(3).num(), (float)t->get(4).num(1)};
}
void set_color(const Value &c) { gfx::set_color(colour_of(c)); }

TableRef mix_colours(const Value &c1, const Value &c2, float p) {
    auto ch = [&](int i, double def) {
        Value a = c1[i], b = c2[i];
        return (a.is_num() ? a.num() : def) * p + (b.is_num() ? b.num() : def) * (1 - p);
    };
    return A({ch(1, 0.5), ch(2, 0.5), ch(3, 0.5), ch(4, 1)});
}
TableRef darken(const Value &c, float p) {
    return A({c[1].num() * (1 - p), c[2].num() * (1 - p), c[3].num() * (1 - p), c[4]});
}
TableRef lighten(const Value &c, float p) {
    return A({c[1].num() * (1 - p) + p, c[2].num() * (1 - p) + p, c[3].num() * (1 - p) + p, c[4]});
}
TableRef adjust_alpha(const Value &c, float a) { return A({c[1], c[2], c[3], (double)a}); }

TableRef HEX(std::string_view hex) {
    std::string h(hex);
    if (h.size() <= 6) h += "FF";
    auto byte = [&](int i) { return (double)std::strtol(h.substr(i * 2, 2).c_str(), nullptr, 16) / 255.0; };
    return A({byte(0), byte(1), byte(2), byte(3)});
}

static void call_func(const Value &name, UIElement *e) {
    if (!name.is_str()) return;
    auto it = G.FUNCS.find(std::string(name.str()));
    if (it != G.FUNCS.end()) it->second(e);
    else {
        static std::map<std::string, bool> warned;
        if (!warned[std::string(name.str())]) {
            warned[std::string(name.str())] = true;
            std::fprintf(stderr, "[ui] missing G.FUNCS.%s\n", std::string(name.str()).c_str());
        }
    }
}

// ------------------------------------------------------------------ Node popups (node.lua)
void Node::drag(const XY *) {
    if (config && cb(config, K("d_popup"))) {
        if (!children.get(K("d_popup"))) {
            UIBox *p = new UIBox(cv(config, K("d_popup")).table(), cv(config, K("d_popup_config")).table());
            children.set(K("d_popup"), p);
            if (Node *h = children.get(K("h_popup"))) h->states.collide.can = false;
            G.I.POPUP.add(p);
            p->states.drag.can = true;
        }
    }
}

void Node::stop_drag() {
    if (Node *d = children.get(K("d_popup"))) {
        G.I.POPUP.remove(d);
        d->remove();
        children.set(K("d_popup"), nullptr);
    }
}

void Node::hover() {
    if (config && cb(config, K("h_popup"))) {
        if (!children.get(K("h_popup"))) {
            TableRef pc = cv(config, K("h_popup_config")).table();
            if (pc) pc->set("instance_type", "POPUP");
            UIBox *p = new UIBox(cv(config, K("h_popup")).table(), pc);
            children.set(K("h_popup"), p);
            p->states.collide.can = false;
            p->states.drag.can = true;
        }
    }
}

void Node::stop_hover() {
    if (Node *h = children.get(K("h_popup"))) {
        h->remove();
        children.set(K("h_popup"), nullptr);
    }
}

// ------------------------------------------------------------------ UIBox
UIBox::UIBox(TableRef def, TableRef cfg, const Transform *T0) {
    Args a;
    if (T0) a.T = *T0;
    Moveable::init(a);
    states.drag.can = false;
    definition = def;
    if (cfg) {
        has_config = true;
        config = cfg;
        if (!cb(config, K("major"))) config->set(K("major"), cb(config, K("parent")) ? cv(config, K("parent")) : Value(this));
        AlignArgs al;
        al.major = cv(config, K("major")).as<Moveable>();
        Bond bond = parse_bond(cv(config, K("bond")), Bond::Strong);
        al.bond = &bond;
        std::string type = cv(config, K("align")).is_str() ? std::string(cv(config, K("align")).str())
                           : cv(config, K("type")).is_str() ? std::string(cv(config, K("type")).str()) : "";
        al.type = type.c_str();
        XY off{0, 0};
        Value o = cv(config, K("offset"));
        if (o.is_table() && o["x"].is_num() && o["y"].is_num()) off = {(float)o["x"].num(), (float)o["y"].num()};
        al.offset = &off;
        al.lr_clamp = cb(config, K("lr_clamp"));
        set_alignment(al);
        Bond xy = parse_bond(cv(config, K("xy_bond")), role.xy_bond);
        Bond rb = parse_bond(cv(config, K("r_bond")), role.r_bond);
        Bond wh = parse_bond(cv(config, K("wh_bond")), Bond::Weak);
        Bond sc = parse_bond(cv(config, K("scale_bond")), Bond::Weak);
        RoleArgs ra;
        ra.xy_bond = &xy;
        ra.r_bond = &rb;
        ra.wh_bond = &wh;
        ra.scale_bond = &sc;
        set_role(ra);
        Value cc = cv(config, K("can_collide"));
        states.collide.can = cc.is_nil() ? true : cc.truthy();
        parent = cv(config, K("parent")).as<Node>();
    }
    set_parent_child(definition, nullptr);
    // self.Mid = self.Mid or self.UIRoot: Moveable.init already set Mid = self, so it stays self
    // unless a node of the definition has config.mid (set_parent_child)
    calculate_xywh(UIRoot.get(), *T);
    T->w = UIRoot->T->w;
    T->h = UIRoot->T->h;
    UIRoot->set_wh();
    UIRoot->set_alignments();
    align_to_major();
    VT.x = T->x;
    VT.y = T->y;
    VT.w = T->w;
    VT.h = T->h;
    if (alignment.lr_clamp) lr_clamp();
    UIRoot->initialize_VT();
    Value it = config ? cv(config, K("instance_type")) : Value();
    if (it.equals("POPUP")) G.I.POPUP.add(this);
    else if (it.equals("ALERT")) G.I.ALERT.add(this);
    else if (it.equals("CARD")) G.I.CARD.add(this);
    else if (it.equals("NODE")) G.I.NODE.add(this);
    else if (it.equals("MOVEABLE")) G.I.MOVEABLE.add(this);
    else G.I.UIBOX.add(this);
}

UIElement *UIBox::get_UIE_by_ID(std::string_view id, UIElement *node) {
    if (!node) node = UIRoot.get();
    if (!node) return nullptr;
    if (cv(node->config, K("id")).equals(id)) return node;
    UIElement *found = nullptr;
    node->children.each([&](Str *, Node *c) {
        if (found) return;
        UIElement *v = dynamic_cast<UIElement *>(c);
        if (!v) return;
        UIElement *r = get_UIE_by_ID(id, v);
        if (r) { found = r; return; }
        if (UIBox *ob = cv(v->config, K("object")).as<UIBox>()) {
            r = ob->get_UIE_by_ID(id, nullptr);
            if (r) found = r;
        }
    });
    return found;
}

void UIBox::calculate_xywh(UIElement *node, Transform &T0, bool recalculate, float scale, bool has_scale) {
    Transform nt;
    float padding = cf(node->config, K("padding"), 0);  // G.UIT.padding = 0
    int uit = node->UIT;
    if (uit == UIT_B || uit == UIT_T || uit == UIT_O) {
        Moveable *obj = cv(node->config, K("object")).as<Moveable>();
        nt.x = T0.x;
        nt.y = T0.y;
        nt.w = cf(node->config, K("w"), obj ? obj->T->w : 0);
        nt.h = cf(node->config, K("h"), obj ? obj->T->h : 0);
        if (uit == UIT_T) {
            node->config->set(K("text_drawable"), Value());
            float sc = cf(node->config, K("scale"), 1);
            Value rt = cv(node->config, K("ref_table")), rv = cv(node->config, K("ref_value"));
            if (rt.truthy() && rv.truthy()) {
                node->config->set(K("text"), Value(rt.get(rv).to_string()));
                if (cb(node->config, K("func")) && !recalculate) call_func(cv(node->config, K("func")), node);
            }
            // text = 1 (a number) prints like tostring, as LOVE does
            if (cv(node->config, K("text")).is_num()) node->config->set(K("text"), Value(cv(node->config, K("text")).to_string()));
            if (!cb(node->config, K("text"))) node->config->set(K("text"), "[UI ERROR]");
            if (!cb(node->config, K("lang"))) node->config->set(K("lang"), Value((Object *)G.LANG));
            LangDef *lang = cv(node->config, K("lang")).as<LangDef>();
            FontDef &f = *lang->font;
            float tx = f.font()->width(cv(node->config, K("text")).str()) * f.squish * sc * G.TILESCALE * f.FONTSCALE;
            float ty = f.font()->height() * sc * G.TILESCALE * f.FONTSCALE * f.TEXT_HEIGHT_SCALE;
            if (cb(node->config, K("vert"))) std::swap(tx, ty);
            nt.w = tx / (G.TILESIZE * G.TILESCALE);
            nt.h = ty / (G.TILESIZE * G.TILESCALE);
            node->content_dimensions.w = T0.w;
            node->content_dimensions.h = T0.h;
        } else {
            node->content_dimensions.w = nt.w;
            node->content_dimensions.h = nt.h;
        }
        node->set_values(nt, recalculate);
        T0.w = nt.w;  // return values
        T0.h = nt.h;
        return;
    }
    // containers: laid out like a column/row, a second pass shrinks to maxw/maxh
    Transform ct;
    for (int pass = 1; pass <= 2; pass++) {
        Value maxw = cv(node->config, K("maxw")), maxh = cv(node->config, K("maxh"));
        if (pass == 2 && !((maxw.is_num() && ct.w > maxw.num()) || (maxh.is_num() && ct.h > maxh.num()))) break;
        float fac = has_scale ? scale : 1;
        if (pass == 2) {
            double restriction = maxw.is_num() ? maxw.num() : maxh.num();
            fac = (float)(fac * restriction / (maxw.is_num() ? ct.w : ct.h));
        }
        nt.x = T0.x;
        nt.y = T0.y;
        nt.w = cf(node->config, K("minw"), 0);
        nt.h = cf(node->config, K("minh"), 0);
        if (uit == UIT_ROOT) {
            nt.x = 0;
            nt.y = 0;
        }
        ct.x = nt.x + padding;
        ct.y = nt.y + padding;
        ct.w = 0;
        ct.h = 0;
        for (int i = 0; i < node->children.count(); i++) {
            UIElement *v = dynamic_cast<UIElement *>(node->children.at(i));
            if (!v) continue;
            Value vs = cv(v->config, K("scale"));
            if (vs.is_num()) v->config->set(K("scale"), vs.num() * fac);
            Transform ctc = ct;
            calculate_xywh(v, ctc, recalculate, fac, true);
            float tw = ctc.w, th = ctc.h;
            float emboss = cf(v->config, K("emboss"), 0);
            bool has_emboss = cv(v->config, K("emboss")).is_num();
            if (v->UIT == UIT_R) {
                ct.h = ct.h + th + padding;
                ct.y = ct.y + th + padding;
                if (tw + padding > ct.w) ct.w = tw + padding;
                if (has_emboss) {
                    ct.h += emboss;
                    ct.y += emboss;
                }
            } else {
                ct.w = ct.w + tw + padding;
                ct.x = ct.x + tw + padding;
                if (th + padding > ct.h) ct.h = th + padding;
                if (has_emboss) ct.h += emboss;
            }
        }
    }
    node->content_dimensions.w = ct.w + padding;
    node->content_dimensions.h = ct.h + padding;
    nt.w = std::max(ct.w + padding, nt.w);
    nt.h = std::max(ct.h + padding, nt.h);
    node->set_values(nt, recalculate);
    T0.w = nt.w;
    T0.h = nt.h;
}

bool UIBox::remove_group(UIElement *node, const Value &group) {
    if (!node) node = UIRoot.get();
    for (int i = node->children.count() - 1; i >= 0; i--) {
        UIElement *v = dynamic_cast<UIElement *>(node->children.at(i));
        if (v && remove_group(v, group)) node->children.remove_index(i);
    }
    if (cv(node->config, K("group")).truthy() && cv(node->config, K("group")) == group) {
        node->remove();
        return true;
    }
    if (!UIRoot) return false;
    Transform t = *T;
    calculate_xywh(UIRoot.get(), t, true);
    UIRoot->set_wh();
    UIRoot->set_alignments();  // (the original calls set_alignment, a typo that errors only when reached)
    return false;
}

void UIBox::get_group(UIElement *node, const Value &group, std::vector<UIElement *> &out) {
    if (!node) node = UIRoot.get();
    node->children.each([&](Str *, Node *c) {
        if (UIElement *v = dynamic_cast<UIElement *>(c)) get_group(v, group, out);
    });
    if (cv(node->config, K("group")).truthy() && cv(node->config, K("group")) == group) out.push_back(node);
}

void UIBox::set_parent_child(const TableRef &node, UIElement *par) {
    TableRef ncfg = node->get(K("config")).table();
    UIElement *uie = new UIElement(par, this, (int)node->get(K("n")).num(), ncfg);
    if (par && cb(par->config, K("group"))) uie->config->set(K("group"), cv(par->config, K("group")));
    if (par && cb(par->config, K("button"))) uie->config->set(K("button_UIE"), Value(par));
    if (par && cb(par->config, K("button_UIE"))) uie->config->set(K("button_UIE"), cv(par->config, K("button_UIE")));
    if (uie->UIT == UIT_O && cb(uie->config, K("button"))) {
        if (Node *o = cv(uie->config, K("object")).as<Node>()) o->states.click.can = false;
    }
    int n = (int)node->get(K("n")).num();
    if ((n == UIT_C || n == UIT_R || n == UIT_ROOT) && node->get(K("nodes")).is_table()) {
        node->get(K("nodes")).table()->each([&](const Value &, const Value &v) {
            if (v.is_table()) set_parent_child(TableRef(v.table()), uie);
        });
    }
    if (!par) {
        UIRoot = uie;
        uie->parent = this;
    } else {
        par->children.push(uie);
    }
    if (ncfg && cb(ncfg, K("mid"))) Mid = uie;
}

void UIBox::remove() {
    if (this == G.OVERLAY_MENU.get()) G.REFRESH_ALERTS = true;
    if (UIRoot) UIRoot->remove();
    Value it = config ? cv(config, K("instance_type")) : Value();
    if (it.equals("POPUP")) G.I.POPUP.remove(this);
    else if (it.equals("ALERT")) G.I.ALERT.remove(this);
    else if (it.equals("CARD")) G.I.CARD.remove(this);
    else G.I.UIBOX.remove(this);
    remove_all(children);
    Moveable::remove();
    UIRoot = nullptr;  // break the box <-> root cycle
}

namespace {
// the first text in a UI definition: names a box in diagnostics
std::string first_text(const Value &n, int depth = 0) {
    if (!n.is_table() || depth > 12) return std::string();
    Value cfg = n.get(Value(intern("config")));
    Value t = cfg.get(Value(intern("text")));
    if (t.is_str()) return t.to_string();
    Value id = cfg.get(Value(intern("id")));
    std::string r;
    if (Table *nodes = n.get(Value(intern("nodes"))).table())
        nodes->each([&](const Value &, const Value &c) {
            if (r.empty()) r = first_text(c, depth + 1);
        });
    if (r.empty() && id.is_str()) r = "#" + id.to_string();
    return r;
}
}  // namespace

void UIBox::draw() {
    if (!UIRoot) {
        // a removed box something still draws (seen once on the Vita): skip it and say which
        static int reported = 0;
        if (reported++ < 5)
            platform::log("draw: removed UIBox '%s' (parent %p, major %p)", first_text(Value(definition)).c_str(), (void *)parent,
                          (void *)role.major.get());
        return;
    }
    if (FRAME.DRAW >= G.FRAMES.DRAW && !G.OVERLAY_TUTORIAL) return;
    FRAME.DRAW = G.FRAMES.DRAW;
    children.each([](Str *name, Node *c) {
        if (name != K("h_popup") && name != K("alert")) c->draw();
    });
    if (states.visible && UIRoot) {
        add_to_drawhash(this);
        UIRoot->draw_self();
        UIRoot->draw_children();
        for (int i = 1; draw_layers.count(i); i++) {
            UIElement *v = draw_layers[i];
            v->draw_self();
            v->draw_children();
        }
    }
    if (Node *al = children.get(K("alert"))) al->draw();
    draw_boundingrect();
}

void UIBox::recalculate() {
    // a removed box: the original keeps its (removed) UIRoot and recalculates it harmlessly
    if (!UIRoot) return;
    Transform t = *T;
    calculate_xywh(UIRoot.get(), t, true);
    UIRoot->set_wh();
    UIRoot->set_alignments();
    T->w = UIRoot->T->w;
    T->h = UIRoot->T->h;
    G.REFRESH_FRAME_MAJOR_CACHE++;
    UIRoot->initialize_VT();
    G.REFRESH_FRAME_MAJOR_CACHE = G.REFRESH_FRAME_MAJOR_CACHE > 1 ? G.REFRESH_FRAME_MAJOR_CACHE - 1 : 0;
}

void UIBox::move(double dt) {
    Moveable::move(dt);
    if (UIRoot) UIRoot->Moveable::move(dt);
}

void UIBox::drag(const XY *offset) { Moveable::drag(offset); }

void UIBox::add_child(const TableRef &node, UIElement *par) {
    set_parent_child(node, par);
    recalculate();
}

void UIBox::set_container(Node *c) {
    if (UIRoot) UIRoot->set_container(c);
    Node::set_container(c);
}

// ------------------------------------------------------------------ UIElement
UIElement::UIElement(UIElement *par, UIBox *b, int uit, TableRef cfg) {
    parent = par;
    UIT = uit;
    box = b;
    config = cfg ? cfg : new_table();
    if (Node *o = cv(config, K("object")).as<Node>()) o->parent = this;
}

Moveable *UIElement::object() const { return cv(config, K("object")).as<Moveable>(); }

void UIElement::set_values(const Transform &Tn, bool recalculate) {
    if (!recalculate || !initialized) {
        Args a;
        a.T = Tn;
        Moveable::init(a);
        initialized = true;
        states.click.can = false;
        states.drag.can = false;
        static_rotation = true;
    } else {
        T->x = Tn.x;
        T->y = Tn.y;
        T->w = Tn.w;
        T->h = Tn.h;
    }
    if (cb(config, K("button_UIE"))) {
        states.collide.can = true;
        states.hover.can = false;
        states.click.can = true;
    }
    if (cb(config, K("button"))) {
        states.collide.can = true;
        states.click.can = true;
    }
    if (cb(config, K("on_demand_tooltip")) || cb(config, K("tooltip")) || cb(config, K("detailed_tooltip")))
        states.collide.can = true;

    {
        RoleType minor = RoleType::Minor;
        XY off{Tn.x, Tn.y};
        Bond weak = Bond::Weak;
        RoleArgs ra;
        ra.role_type = &minor;
        ra.major = box;
        ra.offset = &off;
        ra.wh_bond = &weak;
        ra.scale_bond = &weak;
        set_role(ra);
    }
    if (cv(config, K("draw_layer")).is_num()) box->draw_layers[(int)cv(config, K("draw_layer")).num()] = this;
    if (cb(config, K("collideable"))) states.collide.can = true;
    Value cc = cv(config, K("can_collide"));
    if (!cc.is_nil()) {
        states.collide.can = cc.truthy();
        if (Node *o = cv(config, K("object")).as<Node>()) o->states.collide.can = states.collide.can;
    }
    if (UIT == UIT_O && !cb(config, K("no_role"))) {
        if (Moveable *o = object()) {
            Value r = cv(config, K("role"));
            RoleArgs ra;
            RoleType rt;
            Bond xy, wh, sc, rb;
            XY roff;
            if (Table *rtab = r.table()) {
                Value t = rtab->get(K("role_type"));
                if (t.is_str()) {
                    rt = t.equals("Major") ? RoleType::Major : t.equals("Glued") ? RoleType::Glued : RoleType::Minor;
                    ra.role_type = &rt;
                }
                ra.major = rtab->get(K("major")).as<Moveable>();
                if (rtab->get(K("xy_bond")).is_str()) { xy = parse_bond(rtab->get(K("xy_bond")), Bond::Strong); ra.xy_bond = &xy; }
                if (rtab->get(K("wh_bond")).is_str()) { wh = parse_bond(rtab->get(K("wh_bond")), Bond::Strong); ra.wh_bond = &wh; }
                if (rtab->get(K("scale_bond")).is_str()) { sc = parse_bond(rtab->get(K("scale_bond")), Bond::Strong); ra.scale_bond = &sc; }
                if (rtab->get(K("r_bond")).is_str()) { rb = parse_bond(rtab->get(K("r_bond")), Bond::Strong); ra.r_bond = &rb; }
                Value o2 = rtab->get(K("offset"));
                if (o2.is_table() && o2["x"].is_num() && o2["y"].is_num()) {
                    roff = {(float)o2["x"].num(), (float)o2["y"].num()};
                    ra.offset = &roff;
                }
            } else {
                rt = RoleType::Minor;
                xy = Bond::Strong;
                wh = Bond::Weak;
                sc = Bond::Weak;
                ra.role_type = &rt;
                ra.major = this;
                ra.xy_bond = &xy;
                ra.wh_bond = &wh;
                ra.scale_bond = &sc;
            }
            o->set_role(ra);
        }
    }
    if (cb(config, K("ref_value")) && cb(config, K("ref_table")))
        config->set(K("prev_value"), cv(config, K("ref_table")).get(cv(config, K("ref_value"))));
    if (UIT == UIT_T) static_rotation = true;

    if (cb(config, K("juice"))) {
        if (UIT == UIT_O) {
            if (Moveable *o = object()) o->juice_up(0.5f);
        } else {
            Moveable::juice_up();
        }
        config->set(K("juice"), false);
    }
    if (!cb(config, K("colour"))) {
        if (UIT == UIT_ROOT) config->set(K("colour"), gcol(K("UI"), K("BACKGROUND_DARK")));
        if (UIT == UIT_T) config->set(K("colour"), gcol(K("UI"), K("TEXT_LIGHT")));
        if (UIT == UIT_O) config->set(K("colour"), gcol(K("WHITE")));
        if (UIT == UIT_B || UIT == UIT_C || UIT == UIT_R) config->set(K("colour"), gcol(K("CLEAR")));
    }
    if (!cb(config, K("outline_colour"))) {
        if (UIT == UIT_O) config->set(K("colour"), gcol(K("UI"), K("OUTLINE_LIGHT")));  // (sic) sets colour
        else config->set(K("outline_colour"), gcol(K("UI"), K("OUTLINE_LIGHT")));
    }

    Value fa = cv(config, K("focus_args"));
    if (Table *f = fa.table()) {
        if (!f->get(K("registered")).truthy()) {
            if (f->get(K("button")).is_str()) {
                Node *target = cv(config, K("button_UIE")).truthy() ? cv(config, K("button_UIE")).as<Node>() : this;
                G.CONTROLLER->add_to_registry(target, std::string(f->get(K("button")).str()));
            }
            if (f->get(K("snap_to")).truthy()) G.CONTROLLER->snap_to(this);
            if (f->get(K("funnel_to")).truthy()) {
                UIElement *p = parent_uie();
                while (p) {
                    Value pf = cv(p->config, K("focus_args"));
                    if (pf.is_table() && pf["funnel_from"].truthy()) {
                        pf.table()->set(K("funnel_from"), Value(this));
                        f->set(K("funnel_to"), Value(p));
                        break;
                    }
                    p = p->parent_uie();
                }
            }
            f->set(K("registered"), true);
        }
    }
    if (cb(config, K("force_focus"))) states.collide.can = true;
    if (cv(config, K("button_delay")).is_num() && !cb(config, K("button_delay_start"))) {
        config->set(K("button_delay_start"), G.TIMERS.REAL);
        config->set(K("button_delay_end"), G.TIMERS.REAL + cv(config, K("button_delay")).num());
        config->set(K("button_delay_progress"), 0);
    }
    if (cb(config, K("func")) &&
        (((cb(config, K("button_UIE")) || cb(config, K("button"))) && !cv(config, K("func")).equals("set_button_pip")) ||
         cb(config, K("insta_func"))))
        call_func(cv(config, K("func")), this);
}

void UIElement::initialize_VT() {
    move_with_major(0);
    calculate_parrallax();
    children.each([](Str *, Node *c) {
        if (UIElement *u = dynamic_cast<UIElement *>(c)) u->initialize_VT();
    });
    VT.w = T->w;
    VT.h = T->h;
    if (UIT == UIT_T) update_text();
    if (Moveable *o = object()) {
        if (!cb(config, K("no_role"))) {
            o->hard_set_T(T->x, T->y, T->w, T->h);
            o->move_with_major(0);
            o->alignment.prev_type = "";
            o->align_to_major();
        }
        if (UIBox *ob = dynamic_cast<UIBox *>(o)) ob->recalculate();
    }
}

void UIElement::juice_up(float amount, const float *rot_amt) {
    if (UIT == UIT_O) {
        if (Moveable *o = object()) o->juice_up(amount, rot_amt);
    } else {
        Moveable::juice_up(amount, rot_amt);
    }
}

Node *UIElement::can_drag() {
    if (states.drag.can) return this;
    return box->can_drag();
}

void UIElement::draw_children() {
    if (!states.visible) return;
    children.each([](Str *name, Node *c) {
        UIElement *v = dynamic_cast<UIElement *>(c);
        if (name == K("h_popup") || name == K("alert")) return;
        if (v) {
            if (cb(v->config, K("draw_layer"))) return;
            // (dev profile: draw_self time and count by element type; children are measured on their own)
            const double t0 = time_now();
            if (!cb(v->config, K("draw_after"))) v->draw_self();
            const int slot = v->UIT == UIT_T ? 0 : v->UIT == UIT_O ? 2 : 1;
            G.ui_prof[slot] += (time_now() - t0) * 1000;
            G.ui_prof_n[slot]++;
            v->draw_children();
            if (cb(v->config, K("draw_after"))) v->draw_self();
        } else {
            c->draw();
            c->draw();  // (sic) non-UIE children are drawn by both branches of the original
        }
    });
}

XY UIElement::set_wh() {
    float padding = cf(config, K("padding"), 0);
    float mw = 0, mh = 0;
    if (children.empty() || cb(config, K("no_fill"))) return {T->w, T->h};
    children.each([&](Str *, Node *c) {
        UIElement *w = dynamic_cast<UIElement *>(c);
        if (!w) return;
        XY s = w->set_wh();
        if (s.x > mw) mw = s.x;
        if (s.y > mh) mh = s.y;
    });
    (void)padding;
    children.each([&](Str *, Node *c) {
        UIElement *w = dynamic_cast<UIElement *>(c);
        if (!w) return;
        if (w->UIT == UIT_R) w->T->w = mw;
        if (w->UIT == UIT_C) w->T->h = mh;
    });
    return {T->w, T->h};
}

void UIElement::align(float x, float y) {
    role.offset.y += y;
    role.offset.x += x;
    children.each([&](Str *, Node *c) {
        if (UIElement *v = dynamic_cast<UIElement *>(c)) v->align(x, y);
    });
}

void UIElement::set_alignments() {
    Value alv = cv(config, K("align"));
    std::string al = alv.is_str() ? std::string(alv.str()) : "";
    children.each([&](Str *, Node *c) {
        UIElement *v = dynamic_cast<UIElement *>(c);
        if (!v) return;
        if (!al.empty()) {
            float padding = cf(config, K("padding"), 0);
            if (al.find('c') != std::string::npos) {
                if (v->UIT == UIT_T || v->UIT == UIT_B || v->UIT == UIT_O)
                    v->align(0, 0.5f * (T->h - 2 * padding - v->T->h));
                else
                    v->align(0, 0.5f * (T->h - content_dimensions.h));
            }
            if (al.find('m') != std::string::npos) v->align(0.5f * (T->w - content_dimensions.w), 0);
            if (al.find('b') != std::string::npos) v->align(0, T->h - content_dimensions.h);
            if (al.find('r') != std::string::npos) v->align(T->w - content_dimensions.w, 0);
        }
        v->set_alignments();
    });
}

void UIElement::update_text() {
    if (cb(config, K("text")) && !cb(config, K("text_drawable"))) {
        if (!cb(config, K("lang"))) config->set(K("lang"), Value((Object *)G.LANG));
        LangDef *lang = cv(config, K("lang")).as<LangDef>();
        config->set(K("text_drawable"), Value((Object *)new Text(lang->font->font(), cv(config, K("text")).str())));
    }
    Value rt = cv(config, K("ref_table"));
    if (rt.truthy()) {
        Value cur = rt.get(cv(config, K("ref_value")));
        Value prev = cv(config, K("prev_value"));
        if (cur != prev) {
            std::string txt = cur.to_string();
            config->set(K("text"), Value(txt));
            if (Text *td = cv(config, K("text_drawable")).as<Text>()) td->set(txt);
            if (!cb(config, K("no_recalc")) && prev.truthy() && prev.to_string().size() != txt.size())
                box->recalculate();
            config->set(K("prev_value"), cur);
        }
    }
}

void UIElement::update_object() {
    Value rt = cv(config, K("ref_table")), rv = cv(config, K("ref_value"));
    if (rt.truthy() && rv.truthy() && rt.get(rv) != cv(config, K("object"))) {
        config->set(K("object"), rt.get(rv));
        box->recalculate();
    }
    if (Moveable *o = object()) {
        o->refresh_movement = true;
        if (o->states.hover.is && !states.hover.is) {
            hover();
            states.hover.is = true;
        }
        if (!o->states.hover.is && states.hover.is) {
            stop_hover();
            states.hover.is = false;
        }
        if (o->var("ui_object_updated").truthy()) {
            o->set_var("ui_object_updated", Value());
            o->parent = this;
            Value r = cv(config, K("role"));
            RoleType minor = RoleType::Minor;
            RoleArgs ra;
            ra.role_type = &minor;
            ra.major = this;
            o->set_role(ra);
            o->move_with_major(0);
            if (o->var("non_recalc").truthy()) {
                if (UIElement *p = parent_uie()) {
                    p->content_dimensions.w = o->T->w;
                    align(p->T->x - o->T->x, p->T->y - o->T->y);
                    p->set_alignments();
                }
            } else {
                box->recalculate();
            }
        }
    }
}

void UIElement::draw_self() {
    if (!states.visible) {
        if (cb(config, K("force_focus"))) add_to_drawhash(this);
        return;
    }
    if (cb(config, K("force_focus")) || cb(config, K("force_collision")) || cb(config, K("button_UIE")) ||
        cb(config, K("button")) || states.collide.can)
        add_to_drawhash(this);

    bool button_active = true;
    float parallax_dist = 1.5f;
    bool button_being_pressed = false;
    float TS = G.TILESIZE;
    bool shadows = G.shadows_on();

    if (cb(config, K("button")) || cb(config, K("button_UIE"))) {
        UIElement *p = parent_uie();
        Moveable *pm = dynamic_cast<Moveable *>(parent);
        bool has_par = pm && parent != box;
        float sp = cb(config, K("shadow")) ? 0.4f : 0;
        layered_parallax.x = (has_par ? pm->layered_parallax.x : 0) + sp * shadow_parrallax->x / TS;
        layered_parallax.y = (has_par ? pm->layered_parallax.y : 0) + sp * shadow_parrallax->y / TS;
        (void)p;
        if (cb(config, K("button")) &&
            ((has_last_clicked && last_clicked > G.TIMERS.REAL - 0.1) ||
             ((states.hover.is || states.drag.is) && G.CONTROLLER->is_cursor_down))) {
            float bd = cf(config, K("button_dist"), 1);
            layered_parallax.x -= parallax_dist * shadow_parrallax->x / TS * bd;
            layered_parallax.y -= parallax_dist * shadow_parrallax->y / TS * bd;
            parallax_dist = 0;
            button_being_pressed = true;
        }
        if (cb(config, K("button_UIE"))) {
            UIElement *bu = cv(config, K("button_UIE")).as<UIElement>();
            if (bu && !cb(bu->config, K("button"))) button_active = false;
        }
    }
    Value colour = cv(config, K("colour"));
    if (colour[4].num(1) > 0.01) {
        if (UIT == UIT_T && cv(config, K("scale")).is_num()) {
            float sc = cf(config, K("scale"), 1);
            LangDef *lang = cv(config, K("lang")).as<LangDef>();
            FontDef &f = *lang->font;
            float tsx = -shadow_parrallax->x * 0.5f / (sc * f.FONTSCALE);
            float tsy = -shadow_parrallax->y * 0.5f / (sc * f.FONTSCALE);
            bool vert = cb(config, K("vert"));
            Text *td = cv(config, K("text_drawable")).as<Text>();
            if (td && ((cb(config, K("button_UIE")) && button_active) || (!cb(config, K("button_UIE")) && cb(config, K("shadow")) && shadows))) {
                prep_draw(this, 0.97f);
                if (vert) { gfx::translate(0, VT.h); gfx::rotate(-(float)PI / 2); }
                if ((cb(config, K("shadow")) || (cb(config, K("button_UIE")) && button_active)) && shadows) {
                    gfx::set_color({0, 0, 0, 0.3f * (float)colour[4].num(1)});
                    td->draw((f.TEXT_OFFSET.x + (vert ? -tsy : tsx)) * sc * f.FONTSCALE / TS,
                             (f.TEXT_OFFSET.y + (vert ? tsx : tsy)) * sc * f.FONTSCALE / TS, 0,
                             sc * f.squish * f.FONTSCALE / TS, sc * f.FONTSCALE / TS);
                }
                gfx::pop();
            }
            prep_draw(this, 1);
            if (vert) { gfx::translate(0, VT.h); gfx::rotate(-(float)PI / 2); }
            if (!button_active) set_color(gcol(K("UI"), K("TEXT_INACTIVE")));
            else set_color(colour);
            if (td)
                td->draw(f.TEXT_OFFSET.x * sc * f.FONTSCALE / TS, f.TEXT_OFFSET.y * sc * f.FONTSCALE / TS, 0,
                         sc * f.squish * f.FONTSCALE / TS, sc * f.FONTSCALE / TS);
            gfx::pop();
        } else if (UIT == UIT_B || UIT == UIT_C || UIT == UIT_R || UIT == UIT_ROOT) {
            prep_draw(this, 1);
            gfx::scale(1 / TS);
            bool has_r = cv(config, K("r")).truthy();
            if (cb(config, K("shadow")) && shadows) {
                gfx::scale(0.98f);
                if (cb(config, K("shadow_colour"))) set_color(cv(config, K("shadow_colour")));
                else gfx::set_color({0, 0, 0, 0.3f * (float)colour[4].num(1)});
                if (has_r && VT.w > 0.01f) draw_pixellated_rect(RECT_SHADOW, parallax_dist);
                else gfx::rectangle_fill(-shadow_parrallax->x * parallax_dist, -shadow_parrallax->y * parallax_dist, VT.w * TS, VT.h * TS);
                gfx::scale(1 / 0.98f);
            }
            gfx::scale(button_being_pressed ? 0.985f : 1);
            if (cv(config, K("emboss")).is_num()) {
                set_color(Value(darken(colour, states.hover.is ? 0.5f : 0.3f)));
                draw_pixellated_rect(RECT_EMBOSS, parallax_dist, (float)cv(config, K("emboss")).num(), true);
            }
            UIElement *collided = cb(config, K("button_UIE")) ? cv(config, K("button_UIE")).as<UIElement>() : this;
            if (!collided) collided = this;
            Value bc[2];
            bc[0] = cv(config, K("button_delay")).truthy() ? Value(mix_colours(colour, gcol(K("L_BLACK")), 0.5f)) : colour;
            bool hover_col = (cb(collided->config, K("hover")) && collided->states.hover.is &&
                              (!G.CONTROLLER->HID.touch || G.CONTROLLER->is_cursor_down)) ||
                             (collided->has_last_clicked && collided->last_clicked > G.TIMERS.REAL - 0.1);
            int nbc = 1;
            if (hover_col) bc[nbc++] = gcol(K("UI"), K("HOVER"));
            for (int k = 0; k < nbc; k++) {
                set_color(bc[k]);
                if (has_r && VT.w > 0.01f) {
                    if (cv(config, K("button_delay")).truthy()) {
                        set_color(gcol(K("GREY")));
                        draw_pixellated_rect(RECT_FILL, parallax_dist);
                        set_color(bc[k]);
                        draw_pixellated_rect(RECT_FILL, parallax_dist, 0, false, cf(config, K("button_delay_progress"), 0));
                    } else if (Table *pb = cv(config, K("progress_bar")).table()) {
                        set_color(pb->get(K("empty_col")).truthy() ? pb->get(K("empty_col")) : gcol(K("GREY")));
                        draw_pixellated_rect(RECT_FILL, parallax_dist);
                        set_color(pb->get(K("filled_col")).truthy() ? pb->get(K("filled_col")) : gcol(K("BLUE")));
                        double val = pb->get(K("ref_table")).get(pb->get(K("ref_value"))).num();
                        draw_pixellated_rect(RECT_FILL, parallax_dist, 0, false, (float)(val / pb->get(K("max")).num(1)));
                    } else {
                        draw_pixellated_rect(RECT_FILL, parallax_dist);
                    }
                } else {
                    gfx::rectangle_fill(0, 0, VT.w * TS, VT.h * TS);
                }
            }
            gfx::pop();
        } else if (UIT == UIT_O) {
            Moveable *obj = object();
            if (obj) {
                if (cb(config, K("focus_with_object")) && obj->states.focus.is) {
                    if (!has_object_focus_timer) { object_focus_timer = G.TIMERS.REAL; has_object_focus_timer = true; }
                    float lw = (float)(50 * std::pow(std::max(0.0, object_focus_timer - G.TIMERS.REAL + 0.3), 2));
                    prep_draw(this, 1);
                    gfx::scale(1 / TS);
                    gfx::set_line_width(lw + 1.5f);
                    set_color(Value(adjust_alpha(gcol(K("WHITE")), 0.2f * lw)));
                    draw_pixellated_rect(RECT_FILL, parallax_dist);
                    set_color(colour[4].num() > 0 ? Value(mix_colours(gcol(K("WHITE")), colour, 0.8f)) : gcol(K("WHITE")));
                    draw_pixellated_rect(RECT_LINE, parallax_dist);
                    gfx::pop();
                } else {
                    has_object_focus_timer = false;
                }
                obj->draw();
            }
        }
    }

    // outline
    if (cv(config, K("outline")).truthy() && cv(config, K("outline_colour"))[4].num(1) > 0.01) {
        prep_draw(this, 1);
        gfx::scale(1 / TS);
        gfx::set_line_width((float)cv(config, K("outline")).num());
        if (cv(config, K("line_emboss")).is_num()) {
            set_color(Value(darken(cv(config, K("outline_colour")), states.hover.is ? 0.5f : 0.3f)));
            draw_pixellated_rect(RECT_LINE_EMBOSS, parallax_dist, (float)cv(config, K("line_emboss")).num(), true);
        }
        set_color(cv(config, K("outline_colour")));
        if (cv(config, K("r")).truthy() && VT.w > 0.01f) draw_pixellated_rect(RECT_LINE, parallax_dist);
        else gfx::rectangle_line(0, 0, VT.w * TS, VT.h * TS);
        gfx::pop();
    }

    if (cb(config, K("pulse_border"))) {
        if (!has_border_pulse_timer) { border_pulse_timer = G.TIMERS.REAL; has_border_pulse_timer = true; }
        float lw = (float)(2 * std::max(0.0, 0.5 * std::cos(6 * (G.TIMERS.REAL - border_pulse_timer)) + 0.5));
        prep_draw(this, 1);
        gfx::scale(1 / TS);
        gfx::set_line_width(lw + 1);
        set_color(Value(adjust_alpha(gcol(K("BLACK")), 0.2f * lw)));
        draw_pixellated_rect(RECT_FILL, parallax_dist);
        set_color(colour[4].num() > 0 ? Value(mix_colours(gcol(K("WHITE")), colour, 0.8f)) : gcol(K("WHITE")));
        draw_pixellated_rect(RECT_LINE, parallax_dist);
        gfx::pop();
    }

    if (states.focus.is) {
        if (!has_focus_timer) { focus_timer = G.TIMERS.REAL; has_focus_timer = true; }
        float lw = (float)(50 * std::pow(std::max(0.0, focus_timer - G.TIMERS.REAL + 0.3), 2));
        prep_draw(this, 1);
        gfx::scale(1 / TS);
        gfx::set_line_width(lw + 1.5f);
        set_color(Value(adjust_alpha(gcol(K("WHITE")), 0.2f * lw)));
        draw_pixellated_rect(RECT_FILL, parallax_dist);
        set_color(colour[4].num() > 0 ? Value(mix_colours(gcol(K("WHITE")), colour, 0.8f)) : gcol(K("WHITE")));
        draw_pixellated_rect(RECT_LINE, parallax_dist);
        gfx::pop();
    } else {
        has_focus_timer = false;
    }

    Value chosen = cv(config, K("chosen"));
    if (chosen.truthy()) {
        bool vert = chosen.equals("vert");
        auto tri = [&](float x, float y, float w, float h, float out[6]) {
            float s = 2;
            if (vert) {
                x = x + std::min((float)(0.6 * std::sin(G.TIMERS.REAL * 9) * s + 0.2), 0.0f);
                float t[6] = {x - 3.5f * s, y + h / 2 - 1.5f * s, x - 0.5f * s, y + h / 2, x - 3.5f * s, y + h / 2 + 1.5f * s};
                std::copy(t, t + 6, out);
            } else {
                y = y + std::min((float)(0.6 * std::sin(G.TIMERS.REAL * 9) * s + 0.2), 0.0f);
                float t[6] = {x + w / 2 - 1.5f * s, y - 4 * s, x + w / 2, y - 1.1f * s, x + w / 2 + 1.5f * s, y - 4 * s};
                std::copy(t, t + 6, out);
            }
        };
        float tv[6];
        prep_draw(this, 0.98f);
        gfx::scale(1 / TS);
        if (cb(config, K("shadow")) && shadows) {
            gfx::set_color({0, 0, 0, 0.3f * (float)colour[4].num(1)});
            tri(layered_parallax.x - shadow_parrallax->x * parallax_dist * 0.5f,
                layered_parallax.y - shadow_parrallax->y * parallax_dist * 0.5f, VT.w * TS, VT.h * TS, tv);
            gfx::polygon_fill(tv, 3);
        }
        gfx::pop();
        prep_draw(this, 1);
        gfx::scale(1 / TS);
        set_color(gcol(K("RED")));
        tri(layered_parallax.x, layered_parallax.y, VT.w * TS, VT.h * TS, tv);
        gfx::polygon_fill(tv, 3);
        gfx::pop();
    }
    draw_boundingrect();
}

void UIElement::draw_pixellated_rect(int type, float parallax, float emboss, bool has_emboss, float progress) {
    float TS = G.TILESIZE;
    RectCache &pr = pixellated_rect;
    std::vector<float> *sel[5] = {&pr.fill, &pr.shadow, &pr.line, &pr.emboss, &pr.line_emboss};
    if (!pr.valid || sel[type]->empty() || parallax != pr.parallax || pr.w != VT.w || pr.h != VT.h ||
        pr.sw != shadow_parrallax->x || pr.sh != shadow_parrallax->y || pr.progress != progress) {
        pr.valid = true;
        pr.w = VT.w;
        pr.h = VT.h;
        pr.sw = shadow_parrallax->x;
        pr.sh = shadow_parrallax->y;
        pr.progress = progress;
        pr.parallax = parallax;
        for (auto *v : sel) v->clear();
        float ext_up = cv(config, K("ext_up")).is_num() ? (float)cv(config, K("ext_up")).num() * TS : 0;
        float res;
        if (cv(config, K("res")).is_num()) res = (float)cv(config, K("res")).num();
        else {
            float m = std::min(VT.w, VT.h + std::fabs(ext_up) / TS);
            res = m > 3.5f ? 0.8f : m > 0.3f ? 0.6f : 0.15f;
        }
        float totw = VT.w * TS, toth = (VT.h + std::fabs(ext_up) / TS) * TS;
        float subw = totw - 4 * res, subh = toth - 4 * res;
        const float v[] = {
            subw / 2, subh / 2 - ext_up, 0, 4 * res - ext_up, 1 * res, 4 * res - ext_up, 1 * res, 2 * res - ext_up,
            2 * res, 2 * res - ext_up, 2 * res, 1 * res - ext_up, 4 * res, 1 * res - ext_up, 4 * res, 0 * res - ext_up,
            subw, 0 * res - ext_up, subw, 1 * res - ext_up, subw + 2 * res, 1 * res - ext_up, subw + 2 * res, 2 * res - ext_up,
            subw + 3 * res, 2 * res - ext_up, subw + 3 * res, 4 * res - ext_up, totw, 4 * res - ext_up, totw, subh - ext_up,
            subw + 3 * res, subh - ext_up, subw + 3 * res, subh + 2 * res - ext_up, subw + 2 * res, subh + 2 * res - ext_up,
            subw + 2 * res, subh + 3 * res - ext_up, subw, subh + 3 * res - ext_up, subw, toth - ext_up, 4 * res, toth - ext_up,
            4 * res, subh + 3 * res - ext_up, 2 * res, subh + 3 * res - ext_up, 2 * res, subh + 2 * res - ext_up,
            1 * res, subh + 2 * res - ext_up, 1 * res, subh - ext_up, 0, subh - ext_up, 0, 4 * res - ext_up,
        };
        const int n = (int)(sizeof(v) / sizeof(v[0]));
        pr.fill.resize(n);
        pr.shadow.resize(n);
        if (n > 4) pr.line.resize(n - 4);
        if (has_emboss) {
            pr.line_emboss.resize(n - 4);
            pr.emboss.resize(n);
        }
        for (int k = 1; k <= n; k++) {
            float val = v[k - 1];
            if (k % 2 == 1 && val > totw * pr.progress) val = totw * pr.progress;
            pr.fill[k - 1] = val;
            if (k > 4) {
                pr.line[k - 5] = val;
                if (has_emboss)
                    pr.line_emboss[k - 5] = val + (k % 2 == 0 ? -emboss * shadow_parrallax->y : -0.7f * emboss * shadow_parrallax->x);
            }
            if (k % 2 == 0) {
                pr.shadow[k - 1] = val - shadow_parrallax->y * parallax;
                if (has_emboss) pr.emboss[k - 1] = val + emboss * TS;
            } else {
                pr.shadow[k - 1] = val - shadow_parrallax->x * parallax;
                if (has_emboss) pr.emboss[k - 1] = val;
            }
        }
    }
    std::vector<float> &verts = *sel[type];
    if (verts.size() < 6) return;
    if (type == RECT_LINE || type == RECT_LINE_EMBOSS) gfx::polygon_line(verts.data(), (int)verts.size() / 2);
    else gfx::polygon_fill(verts.data(), (int)verts.size() / 2);
}

void UIElement::update(double) {
    Value button = cv(config, K("button"));
    if (G.DISABLED_BUTTONS && button.truthy() && !tutorial_disabled.truthy()) {
        G.DISABLED_BUTTONS->each([&](const Value &, const Value &v) {
            if (cv(config, K("button")) == v) {
                tutorial_disabled = cv(config, K("button"));
                config->set(K("button"), Value());
                disable_button = true;
            }
        });
    } else if (!G.DISABLED_BUTTONS && tutorial_disabled.truthy()) {
        disable_button = false;
        config->set(K("button"), tutorial_disabled);
        tutorial_disabled = Value();
    }
    if (cv(config, K("button_delay")).truthy()) {
        config->set(K("button_temp"), cb(config, K("button")) ? cv(config, K("button")) : cv(config, K("button_temp")));
        config->set(K("button"), Value());
        config->set(K("button_delay_progress"),
                    (G.TIMERS.REAL - cv(config, K("button_delay_start")).num()) / cv(config, K("button_delay")).num());
        if (G.TIMERS.REAL >= cv(config, K("button_delay_end")).num()) config->set(K("button_delay"), Value());
    }
    if (cb(config, K("button_temp")) && !cb(config, K("button_delay"))) config->set(K("button"), cv(config, K("button_temp")));
    if (button_clicked) button_clicked = false;
    if (cb(config, K("func"))) call_func(cv(config, K("func")), this);
    if (UIT == UIT_T) update_text();
    if (UIT == UIT_O) update_object();
}

bool UIElement::collides_with_point(XY p) {
    if (box->states.collide.can) return Node::collides_with_point(p);
    return false;
}

void UIElement::click() {
    Value button = cv(config, K("button"));
    if (button.truthy() && (!has_last_clicked || last_clicked + 0.1 < G.TIMERS.REAL) && states.visible &&
        !under_overlay && !disable_button) {
        if (cb(config, K("one_press"))) disable_button = true;
        last_clicked = G.TIMERS.REAL;
        has_last_clicked = true;
        if (cv(config, K("id")).equals("overlay_menu_back_button")) {
            G.CONTROLLER->mod_cursor_context_layer(-1);
            G.NO_MOD_CURSOR_STACK = true;
        }
        if (G.OVERLAY_TUTORIAL && G.OVERLAY_TUTORIAL->var("button_listen") == button) call_func(Value("tut_next"), this);
        call_func(button, this);
        G.NO_MOD_CURSOR_STACK = false;
        if (cb(config, K("choice"))) {
            std::vector<UIElement *> choices;
            box->get_group(nullptr, cv(config, K("group")), choices);
            for (UIElement *v : choices)
                if (cb(v->config, K("choice"))) v->config->set(K("chosen"), false);
            config->set(K("chosen"), true);
        }
        play_sound("button", 1, 0.3f);
        G.ROOM_jiggle += 0.5;
        button_clicked = true;
    }
    if (UIElement *bu = cv(config, K("button_UIE")).as<UIElement>()) bu->click();
}

XY UIElement::put_focused_cursor() {
    Value fa = cv(config, K("focus_args"));
    if (fa.is_table() && fa["type"].equals("tab")) {
        XY r{};
        bool found = false;
        children.each([&](Str *, Node *c) {
            if (found) return;
            UIElement *v = dynamic_cast<UIElement *>(c);
            if (!v || v->children.count() == 0) return;
            UIElement *f = dynamic_cast<UIElement *>(v->children.at(0));
            if (f && cb(f->config, K("chosen"))) {
                r = f->put_focused_cursor();
                found = true;
            }
        });
        return r;
    }
    return Node::put_focused_cursor();
}

void UIElement::remove() {
    if (Moveable *o = object()) {
        o->remove();
        config->set(K("object"), Value());
    }
    if (G.CONTROLLER && G.CONTROLLER->text_input_hook.get() == this) G.CONTROLLER->text_input_hook = nullptr;
    remove_all(children);
    Moveable::remove();
}

void UIElement::hover() {
    if (cb(config, K("on_demand_tooltip"))) {
        config->set(K("h_popup"), Value(create_popup_UIBox_tooltip(cv(config, K("on_demand_tooltip")))));
        bool top = T->y > G.ROOM->T->h / 2;
        config->set(K("h_popup_config"), Value(vl::T({{"align", top ? "tm" : "bm"},
                                                   {"offset", Value(vl::T({{"x", 0}, {"y", top ? -0.1 : 0.1}}))},
                                                   {"parent", Value(this)}})));
    }
    if (cb(config, K("tooltip"))) {
        config->set(K("h_popup"), Value(create_popup_UIBox_tooltip(cv(config, K("tooltip")))));
        config->set(K("h_popup_config"), Value(vl::T({{"align", "tm"}, {"offset", Value(vl::T({{"x", 0}, {"y", -0.1}}))}, {"parent", Value(this)}})));
    }
    if (cb(config, K("detailed_tooltip")) && G.CONTROLLER->HID.pointer) {
        config->set(K("h_popup"), Value(create_UIBox_detailed_tooltip(cv(config, K("detailed_tooltip")))));
        config->set(K("h_popup_config"), Value(vl::T({{"align", "tm"}, {"offset", Value(vl::T({{"x", 0}, {"y", -0.1}}))}, {"parent", Value(this)}})));
    }
    Node::hover();
}

void UIElement::stop_hover() {
    Node::stop_hover();
    if (cb(config, K("on_demand_tooltip"))) config->set(K("h_popup"), Value());
}

void UIElement::release(Node *other) {
    if (Value rf = cv(config, K("release_func")); rf.truthy()) {
        if (Func *f = rf.as<Func>()) f->call(Value(other));
    }
    if (parent) parent->release(other);
}

// ------------------------------------------------------------------ remove_all
void remove_all(Children &c) {
    for (int i = c.count() - 1; i >= 0; i--) {
        NodeRef v = c.at(i);
        c.remove_index(i);
        if (v) {
            remove_all(v->children);
            v->remove();
        }
    }
    c.each([&](Str *, Node *v) {
        remove_all(v->children);
        v->remove();
    });
}

}  // namespace vl
