// game.lua (Game methods), globals.lua (Game:set_globals) and main.lua (love.resize/update/draw glue)
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <memory>
#include <set>

#include "core/lua_sort.h"
#include "engine/controller.h"
#include "engine/random.h"
#include "engine/sprite.h"
#include "engine/ui.h"
#include "game/button_callbacks.h"
#include "game/card.h"
#include "game/functions.h"
#include "game/g.h"
#include "game/render.h"
#include "game/save_manager.h"
#include "game/sound.h"
#include "game/ui_definitions.h"
#include "gfx/gfx.h"
#include "platform/platform.h"

#include "stb/stb_image.h"

namespace vl {

namespace {
inline Value fld(const Value &t, const char *k) { return t.get(Value(intern(k))); }

}  // namespace

TableRef load_data(const std::string &name) {
    std::string src;
    if (!platform::read_file(("data/" + name + ".lua").c_str(), src)) platform::fatal("missing data/%s.lua", name.c_str());
    std::string err;
    Value v = parse_lua_literal(src, &err);
    if (!v.is_table()) platform::fatal("bad data/%s.lua: %s", name.c_str(), err.c_str());
    return TableRef(v.table());
}

namespace {
// localize(args) calls inside the prototype literals were exported as {__localize = args}
void resolve_localize(Table *t) {
    t->each([&](const Value &k, const Value &v) {
        Table *vt = v.table();
        if (!vt) return;
        Value args = vt->get(K("__localize"));
        if (!args.is_nil()) {
            t->set(k, args.is_table() ? localize(TableRef(args.table())) : localize(args.str()));
            return;
        }
        resolve_localize(vt);
    });
}

TableRef game_object_template;

// os.time(); VL_FIXED_TIME pins it so runs can be compared frame by frame with a reference
double os_time() {
    const char *f = std::getenv("VL_FIXED_TIME");
    return f && *f ? std::atof(f) : (double)std::time(nullptr);
}

gfx::Texture *load_texture(const std::string &path, int scale) {
    std::string data;
    if (!platform::read_file(path.c_str(), data)) platform::fatal("missing %s", path.c_str());
    int w, h, n;
    unsigned char *px = stbi_load_from_memory((const unsigned char *)data.data(), (int)data.size(), &w, &h, &n, 4);
    if (!px) platform::fatal("bad png %s", path.c_str());
    gfx::TextureDesc d{w, h, px};
    // TODO(manual-mips): mipmaps = true once the manual mip chain replaces GXM's NPOT mipmaps
    d.mipmaps = false;
    d.virtual_scale = scale;
    d.dpi_scale = (float)scale;
    d.filter = scale == 1 ? gfx::Filter::Nearest : gfx::Filter::Linear;
    gfx::Texture *t = gfx::create_texture(d);
    stbi_image_free(px);
    return t;
}

// love.resize(w, h)
void resize(float w, float h) {
    if (w / h < 1) h = w;
    if (w / h < G.window_prev.orig_ratio) G.TILESCALE = G.window_prev.orig_scale * w / G.window_prev.w;
    else G.TILESCALE = G.window_prev.orig_scale * h / G.window_prev.h;
    if (G.ROOM) {
        G.ROOM->T->w = G.TILE_W;
        G.ROOM->T->h = G.TILE_H;
        G.ROOM_ATTACH->T->w = G.TILE_W;
        G.ROOM_ATTACH->T->h = G.TILE_H;
        if (w / h < G.window_prev.orig_ratio) {
            G.ROOM->T->x = G.ROOM_PADDING_W;
            G.ROOM->T->y = (h / (G.TILESIZE * G.TILESCALE) - (G.ROOM->T->h + G.ROOM_PADDING_H)) / 2 + G.ROOM_PADDING_H / 2;
        } else {
            G.ROOM->T->y = G.ROOM_PADDING_H;
            G.ROOM->T->x = (w / (G.TILESIZE * G.TILESCALE) - (G.ROOM->T->w + G.ROOM_PADDING_W)) / 2 + G.ROOM_PADDING_W / 2;
        }
        G.ROOM_ORIG = {G.ROOM->T->x, G.ROOM->T->y};
        if (auto *b = dynamic_cast<UIBox *>(G.buttons.get())) b->recalculate();
        if (auto *b = dynamic_cast<UIBox *>(G.HUD.get())) b->recalculate();
    }
    G.WINDOWTRANS = {0, 0, G.TILE_W + 2 * G.ROOM_PADDING_W, G.TILE_H + 3 * G.ROOM_PADDING_H, w, h};
    G.CANV_SCALE = 1;
    if (!G.CANVAS) G.CANVAS = gfx::create_canvas((int)w, (int)h);
}

void call_func(const char *name, UIElement *e = nullptr) {
    auto it = G.FUNCS.find(name);
    if (it != G.FUNCS.end()) it->second(e);
}

void draw_node(Node *v) {
    gfx::push();
    v->translate_container();
    v->draw();
    gfx::pop();
}

// Is v part of the overlay menu (an element of it, or a popup/alert attached to one)?
bool in_overlay(Node *v) {
    Node *o = G.OVERLAY_MENU.get();
    for (int i = 0; v && i < 64; i++) {
        if (v == o) return true;
        Moveable *m = dynamic_cast<Moveable *>(v);
        v = v->parent ? v->parent : m ? m->role.major.get() : nullptr;
    }
    return false;
}

void draw_zoomed(Node *v) {
    const auto &z = G.overlay_zoom;
    float ppu = G.TILESCALE * G.TILESIZE;
    gfx::push();
    gfx::translate(z.cx * ppu, z.cy * ppu);
    gfx::scale(z.s);
    gfx::translate(-z.cx * ppu, -z.cy * ppu);
    G.drawing_zoomed = true;
    draw_node(v);
    G.drawing_zoomed = false;
    gfx::pop();
}

// The overlay's panel is drawn up to 1.6x about its own centre, as large as fits the screen with a
// small margin. Only its size matters, so the slide-in animation keeps one scale throughout (the
// panels rest centred on the screen).
void update_overlay_zoom() {
    auto &z = G.overlay_zoom;
    z.on = false;
    UIBox *ob = dynamic_cast<UIBox *>(G.OVERLAY_MENU.get());
    if (!ob || !G.SETTINGS->get(K("vl_large_menus")).truthy()) return;
    // the visible panel: the root itself is a screen-sized dimmed backdrop (padding = 15)
    Node *panel = ob->UIRoot && ob->UIRoot->children.count() ? ob->UIRoot->children.at(0) : nullptr;
    if (!panel || panel->T->w <= 0 || panel->T->h <= 0) return;
    float ppu = G.TILESCALE * G.TILESIZE, m = 0.15f;
    float W = gfx::screen_width() / ppu, H = gfx::screen_height() / ppu;
    Moveable *pm = dynamic_cast<Moveable *>(panel);
    const Transform &t = pm ? pm->VT : *panel->T;
    z.s = std::min({1.6f, (W - 2 * m) / panel->T->w, (H - 2 * m) / panel->T->h});
    z.cx = t.x + t.w / 2 + G.ROOM->T->x;
    z.cy = t.y + t.h / 2 + G.ROOM->T->y;
    z.on = z.s > 1.02f;
}
}  // namespace

// ------------------------------------------------------------------ Game:init / set_globals
void game_init() {
    G.VERSION = "1.0.1o-FULL [M]";
    G.C = load_data("colours");
    TableRef globals = load_data("globals");
    G.SETTINGS = TableRef(globals->get(K("SETTINGS")).table());
    // the extracted defaults come from a mobile configuration; Vitalatro uses the PC flags
    G.SETTINGS->get(K("GRAPHICS")).table()->set("crt", G.F_MOBILE ? 30 : 70);
    G.SETTINGS->set("rumble", Value());
    G.COLLABS = TableRef(globals->get(K("COLLABS")).table());
    G.PROFILES = A({Value(new_table()), Value(new_table()), Value(new_table())});
    G.METRICS = new_table();
    G.ARGS = new_table();
    G.FILES = new_table();
    G.CHALLENGES = load_data("challenges");
    game_object_template = load_data("game_object");
    G.STAGE = MAIN_MENU;
    G.STATE = SPLASH;
    G.F_QUIT_BUTTON = false;
    G.F_VIDEO_SETTINGS = false;
    G.F_EXTERNAL_LINKS = false;
    G.F_SAVE_TIMER = 5;  // the PC value (Windows/OS X): the Vita app can be closed at any moment
    lua_math.seed(os_time());  // math.randomseed(G.SEED)
    // G:init_window()
    G.ROOM_PADDING_H = 0.4f;
    G.ROOM_PADDING_W = 0.8f;
    G.WINDOWTRANS = {0, 0, G.TILE_W + 2 * G.ROOM_PADDING_W, G.TILE_H + 3 * G.ROOM_PADDING_H, 0, 0};
    G.window_prev.orig_scale = G.TILESCALE;
    G.window_prev.w = G.WINDOWTRANS.w * G.TILESIZE * G.TILESCALE;
    G.window_prev.h = G.WINDOWTRANS.h * G.TILESIZE * G.TILESCALE;
    G.window_prev.orig_ratio = G.window_prev.w / G.window_prev.h;
    G.SETTINGS->set("QUEUED_CHANGE", Value(new_table()));
    register_button_callbacks();
    resize((float)gfx::screen_width(), (float)gfx::screen_height());
}

// ------------------------------------------------------------------ Game:start_up
void Globals::start_up() {
    E_MANAGER = new EventManager();
    SPEEDFACTOR = 1;
    sound_start();  // G.F_SOUND_THREAD: the SOUND_MANAGER thread
    // load_all_files(): every file is local on the Vita (the original fetches profile/meta through
    // the platform's cloud save layer, which also reads them from disk)
    for (const char *f : {"settings.jkr", "1/profile.jkr", "1/save.jkr", "1/meta.jkr", "1/unlock_notify.jkr", "2/profile.jkr",
                          "2/save.jkr", "2/meta.jkr", "2/unlock_notify.jkr", "3/profile.jkr", "3/save.jkr", "3/meta.jkr",
                          "3/unlock_notify.jkr"}) {
        Value v = load_local_file(f);
        if (v.truthy()) FILES->set(f, v);
    }
    if (Table *settings_file = FILES->get("settings.jkr").table())
        settings_file->each([&](const Value &k, const Value &v) { SETTINGS->set(k, v); });
    SETTINGS->set("version", Value(VERSION));
    if (!SETTINGS->get(K("profile")).truthy()) SETTINGS->set("profile", 1);
    if (SETTINGS->get(K("vl_large_menus")).is_nil()) SETTINGS->set("vl_large_menus", true);  // Vitalatro option
    SETTINGS->set("paused", Value());

    Value proto = C->get(SETTINGS->get(K("colourblind_option")).truthy() ? "SO_2" : "SO_1");
    Table *suits = C->get(K("SUITS")).table();
    for (const char *s : {"Hearts", "Diamonds", "Spades", "Clubs"}) suits->set(s, fld(proto, s));

    Table *gr = SETTINGS->get(K("GRAPHICS")).table();
    if (gr->get(K("texture_scaling")).truthy()) gr->set("texture_scaling", gr->get(K("texture_scaling")).num() > 1 ? 2 : 1);
    if (!SETTINGS->get(K("language")).truthy()) SETTINGS->set("language", "en-us");

    CONTROLLER = new Controller();
    SETTINGS->set("QUEUED_CHANGE", Value(new_table()));
    SETTINGS->set("music_control", Value(vl::T({{"desired_track", ""}, {"current_track", ""}, {"lerp", 1}})));

    set_render_settings();
    set_language();
    // every font, like LOVE's G:set_language: decoded on a worker during the splash
    {
        std::vector<std::string> ids;
        for (auto &f : FONTS) ids.push_back(f->id);
        Font::preload_async(ids);
    }
    init_item_prototypes();

    auto atlas = [&](const char *n) { return &ASSET_ATLAS[n]; };
    auto pos = [](float x, float y) {
        SpritePos p;
        p.x = x;
        p.y = y;
        return p;
    };
    auto ppos = [](const Value &p) {
        SpritePos s;
        s.x = (float)fld(p, "x").num();
        s.y = (float)fld(p, "y").num();
        return s;
    };
    shared_debuff = new Sprite(0, 0, CARD_W, CARD_H, atlas("centers"), pos(4, 0));
    shared_soul = new Sprite(0, 0, CARD_W, CARD_H, atlas("centers"), ppos(fld(P_CENTERS->get(K("soul")), "pos")));
    shared_undiscovered_joker = new Sprite(0, 0, CARD_W, CARD_H, atlas("centers"), ppos(fld(P_CENTERS->get(K("undiscovered_joker")), "pos")));
    shared_undiscovered_tarot = new Sprite(0, 0, CARD_W, CARD_H, atlas("centers"), ppos(fld(P_CENTERS->get(K("undiscovered_tarot")), "pos")));
    shared_sticker_eternal = new Sprite(0, 0, CARD_W, CARD_H, atlas("stickers"), pos(0, 0));
    shared_sticker_perishable = new Sprite(0, 0, CARD_W, CARD_H, atlas("stickers"), pos(0, 2));
    shared_sticker_rental = new Sprite(0, 0, CARD_W, CARD_H, atlas("stickers"), pos(1, 2));
    const struct { const char *n; float x, y; } stickers[] = {{"White", 1, 0}, {"Red", 2, 0},    {"Green", 3, 0},  {"Black", 0, 1},
                                                              {"Blue", 4, 0},  {"Purple", 1, 1}, {"Orange", 2, 1}, {"Gold", 3, 1}};
    for (auto &s : stickers) shared_stickers[s.n] = new Sprite(0, 0, CARD_W, CARD_H, atlas("stickers"), pos(s.x, s.y));
    const struct { const char *n; float x, y; } seals[] = {{"Gold", 2, 0}, {"Purple", 4, 4}, {"Red", 5, 4}, {"Blue", 6, 4}};
    for (auto &s : seals) shared_seals[s.n] = new Sprite(0, 0, CARD_W, CARD_H, atlas("centers"), pos(s.x, s.y));
    sticker_map = {"White", "Red", "Green", "Black", "Blue", "Purple", "Orange", "Gold"};

    STAGE_OBJECT_INTERRUPT = true;
    CURSOR = new Sprite(0, 0, 0.3f, 0.3f, atlas("gamepad_ui"), pos(18, 0));
    CURSOR->states.collide.can = false;
    STAGE_OBJECT_INTERRUPT = false;

    fetch_achievements();
    // the load manager thread finishing (profile/meta files) ends in G:refresh()
    refresh();
    splash_screen();
}

// ------------------------------------------------------------------ prototypes
void Globals::init_item_prototypes() {
    if (!P_CENTERS) {
        TableRef p = load_data("prototypes");
        resolve_localize(p.get());
        auto t = [&](const char *k) { return TableRef(p->get(k).table()); };
        P_SEALS = t("P_SEALS");
        P_TAGS = t("P_TAGS");
        P_STAKES = t("P_STAKES");
        P_BLINDS = t("P_BLINDS");
        P_CARDS = t("P_CARDS");
        P_CENTERS = t("P_CENTERS");
        tag_undiscovered = p->get("tag_undiscovered");
        b_undiscovered = p->get("b_undiscovered");
        j_locked = p->get("j_locked");
        v_locked = p->get("v_locked");
        c_locked = p->get("c_locked");
        j_undiscovered = p->get("j_undiscovered");
        t_undiscovered = p->get("t_undiscovered");
        p_undiscovered = p->get("p_undiscovered");
        s_undiscovered = p->get("s_undiscovered");
        v_undiscovered = p->get("v_undiscovered");
        booster_undiscovered = p->get("booster_undiscovered");
    }
    P_CENTER_POOLS = new_table();
    for (const char *k : {"Booster", "Default", "Enhanced", "Edition", "Joker", "Tarot", "Planet", "Tarot_Planet", "Spectral",
                          "Consumeables", "Voucher", "Back", "Tag", "Seal", "Stake", "Demo"})
        P_CENTER_POOLS->set(k, Value(new_table()));
    P_JOKER_RARITY_POOLS = A({Value(new_table()), Value(new_table()), Value(new_table()), Value(new_table())});
    P_LOCKED = new_table();

    std::string prof = SETTINGS->get(K("profile")).to_string();
    Value meta = FILES->get(Value(prof + "/meta.jkr"));
    if (!meta.truthy()) meta = Value(new_table());
    for (const char *k : {"unlocked", "discovered", "alerted"})
        if (!fld(meta, k).truthy()) meta.table()->set(k, Value(new_table()));
    Value m_unl = fld(meta, "unlocked"), m_dis = fld(meta, "discovered"), m_al = fld(meta, "alerted");

    auto starts = [](const Value &k, const char *p) { return k.str().rfind(p, 0) == 0; };
    auto reset_flags = [](Table *v) {
        if (v->get(K("unlocked")).is_bool() && v->get(K("start_locked")).truthy()) v->set("unlocked", false);
        if (v->get(K("discovered")).is_bool() && !v->get(K("start_discovered")).truthy()) v->set("discovered", false);
        if (v->get(K("alerted")).is_bool() && !v->get(K("start_alerted")).truthy()) v->set("alerted", false);
    };
    P_CENTERS->each([&](const Value &k, const Value &vv) {
        Table *v = vv.table();
        reset_flags(v);
        v->set("demo", Value());  // not G.FTP_LOCKED
        if (!v->get(K("wip")).truthy() && !v->get(K("demo")).truthy()) {
            bool jbv = starts(k, "j_") || starts(k, "b_") || starts(k, "v_");
            if (!v->get(K("unlocked")).truthy() && jbv && m_unl.get(k).truthy()) v->set("unlocked", true);
            if (!v->get(K("unlocked")).truthy() && jbv) P_LOCKED->push(vv);
            if (!v->get(K("discovered")).truthy() &&
                (starts(k, "j_") || starts(k, "b_") || starts(k, "e_") || starts(k, "c_") || starts(k, "p_") || starts(k, "v_")) &&
                m_dis.get(k).truthy())
                v->set("discovered", true);
            if ((v->get(K("discovered")).truthy() && m_al.get(k).truthy()) || v->get(K("set")).equals("Back") ||
                v->get(K("start_alerted")).truthy())
                v->set("alerted", true);
            else if (v->get(K("discovered")).truthy())
                v->set("alerted", false);
        }
    });
    {
        std::vector<Value> l(P_LOCKED->array().begin(), P_LOCKED->array().end());
        lua_sort(l, [](const Value &a, const Value &b) {
            return !fld(a, "order").truthy() || !fld(b, "order").truthy() || fld(a, "order").num() < fld(b, "order").num();
        });
        for (size_t i = 0; i < l.size(); i++) P_LOCKED->set((int)i + 1, l[i]);
    }
    auto meta_flags = [&](Table *v, const Value &k) {
        if (!v->get(K("discovered")).truthy() && m_dis.get(k).truthy()) v->set("discovered", true);
        if (v->get(K("discovered")).truthy() && m_al.get(k).truthy()) v->set("alerted", true);
        else if (v->get(K("discovered")).truthy()) v->set("alerted", false);
    };
    P_BLINDS->each([&](const Value &k, const Value &vv) {
        Table *v = vv.table();
        reset_flags(v);
        v->set("demo", Value());
        v->set("key", k);
        if (!v->get(K("wip")).truthy() && !v->get(K("demo")).truthy()) meta_flags(v, k);
    });
    P_TAGS->each([&](const Value &k, const Value &vv) {
        Table *v = vv.table();
        reset_flags(v);
        v->set("key", k);
        if (!v->get(K("wip")).truthy() && !v->get(K("demo")).truthy()) {
            meta_flags(v, k);
            P_CENTER_POOLS->get(K("Tag")).table()->push(vv);
        }
    });
    P_SEALS->each([&](const Value &k, const Value &vv) {
        Table *v = vv.table();
        v->set("key", k);
        if (!v->get(K("wip")).truthy() && !v->get(K("demo")).truthy()) {
            meta_flags(v, k);
            P_CENTER_POOLS->get(K("Seal")).table()->push(vv);
        }
    });
    P_STAKES->each([&](const Value &k, const Value &vv) {
        Table *v = vv.table();
        reset_flags(v);
        v->set("key", k);
        P_CENTER_POOLS->get(K("Stake")).table()->push(vv);
    });
    P_CENTERS->each([&](const Value &k, const Value &vv) {
        Table *v = vv.table();
        v->set("key", k);
        Value set = v->get(K("set"));
        if (set.equals("Joker")) P_CENTER_POOLS->get(K("Joker")).table()->push(vv);
        if (set.truthy() && v->get(K("demo")).truthy() && v->get(K("pos")).truthy()) P_CENTER_POOLS->get(K("Demo")).table()->push(vv);
        if (!v->get(K("wip")).truthy()) {
            if (set.truthy() && !set.equals("Joker") && !v->get(K("skip_pool")).truthy() && !v->get(K("omit")).truthy())
                P_CENTER_POOLS->get(set).table()->push(vv);
            if (set.equals("Tarot") || set.equals("Planet")) P_CENTER_POOLS->get(K("Tarot_Planet")).table()->push(vv);
            if (v->get(K("consumeable")).truthy()) P_CENTER_POOLS->get(K("Consumeables")).table()->push(vv);
            if (v->get(K("rarity")).truthy() && set.equals("Joker") && !v->get(K("demo")).truthy())
                P_JOKER_RARITY_POOLS->get((int)v->get(K("rarity")).num()).table()->push(vv);
        }
    });
    save_progress();

    auto sort_pool = [](Table *t, auto less) {
        std::vector<Value> l(t->array().begin(), t->array().end());
        lua_sort(l, less);
        for (size_t i = 0; i < l.size(); i++) t->set((int)i + 1, l[i]);
    };
    auto by_order = [](const Value &a, const Value &b) { return fld(a, "order").num() < fld(b, "order").num(); };
    for (const char *k : {"Joker", "Tarot", "Planet", "Tarot_Planet", "Spectral", "Voucher", "Booster", "Consumeables"})
        sort_pool(P_CENTER_POOLS->get(k).table(), by_order);
    sort_pool(P_CENTER_POOLS->get(K("Back")).table(), [](const Value &a, const Value &b) {
        return fld(a, "order").num() - (fld(a, "unlocked").truthy() ? 100 : 0) < fld(b, "order").num() - (fld(b, "unlocked").truthy() ? 100 : 0);
    });
    for (const char *k : {"Enhanced", "Stake", "Tag", "Seal"}) sort_pool(P_CENTER_POOLS->get(k).table(), by_order);
    sort_pool(P_CENTER_POOLS->get(K("Demo")).table(), [](const Value &a, const Value &b) {
        return fld(a, "order").num() + (fld(a, "set").equals("Joker") ? 1000 : 0) < fld(b, "order").num() + (fld(b, "set").equals("Joker") ? 1000 : 0);
    });
    for (int i = 1; i <= 4; i++) sort_pool(P_JOKER_RARITY_POOLS->get(i).table(), by_order);
}

void Globals::load_profile(int profile) {
    if (!PROFILES->get(profile).truthy()) profile = 1;
    TableRef p = new_table();
    PROFILES->set(profile, Value(p));
    Value info = FILES->get(Value(std::to_string(profile) + "/profile.jkr"));
    if (Table *it = info.table()) it->each([&](const Value &k, const Value &v) { p->set(k, v); });
    std::function<void(Table *, Table *)> recursive_init = [&](Table *t1, Table *t2) {
        t1->each([&](const Value &k, const Value &v) {
            if (!t2->get(k).truthy()) t2->set(k, v);
            else if (t2->get(k).is_table() && v.is_table()) recursive_init(v.table(), t2->get(k).table());
        });
    };
    recursive_init(profile_init_table().get(), p.get());
    double counter = SETTINGS->get(K("profile_counters"))[profile].num();
    p->set("ver", std::max(p->get(K("ver")).num(), counter));
}

void Globals::empty_profile(int profile) {
    ARGS->set("save_settings", Value(SETTINGS));
    if (!FILE_HANDLER) FILE_HANDLER = new_table();
    FILE_HANDLER->set("empty_profile", profile);
    FILE_HANDLER->set("update_queued", true);
    Value prev = PROFILES->get(profile);
    TableRef p = vl::T({{"deleted", true}, {"ver", SETTINGS->get(K("profile_counters"))[profile].num()}});
    if (prev.truthy()) p->set("flags", fld(prev, "flags"));
    PROFILES->set(profile, Value(p));
    FILES->set(Value(std::to_string(profile) + "/profile.jkr"), Value(p));
}

void Globals::set_language() {
    if (LANGUAGES.empty()) {
        // (only the languages whose localization file exists; the Vita build ships all of them)
        struct L { const char *key, *label; int font; bool beta; };
        const L langs[] = {{"en-us", "English", 1, false},          {"de", "Deutsch", 1, false},
                           {"es_419", "Español (México)", 1, false}, {"es_ES", "Español (España)", 1, false},
                           {"fr", "Français", 1, false},             {"id", "Bahasa Indonesia", 1, true},
                           {"it", "Italiano", 1, false},             {"ja", "日本語", 5, false},
                           {"ko", "한국어", 4, true},                 {"nl", "Nederlands", 1, false},
                           {"pl", "Polski", 1, false},               {"pt_BR", "Português", 1, false},
                           {"ru", "Русский", 6, true},               {"zh_CN", "简体中文", 2, false},
                           {"zh_TW", "繁體中文", 3, false}};
        struct F { const char *id; float render_scale, ths, ox, oy, fontscale, squish, descscale; };
        const F fonts[] = {{"m6x11plus_200", TILESIZE * 10, 0.83f, 10, -20, 0.1f, 1, 1},
                           {"NotoSansSC_47", TILESIZE * 7 / 3, 0.7f, 0, -13, 0.36f, 1, 1.1f},
                           {"NotoSansTC_47", TILESIZE * 7 / 3, 0.7f, 0, -13, 0.36f, 1, 1.1f},
                           {"NotoSansKR_140", TILESIZE * 7, 0.8f, 0, -20, 0.12f, 1, 1},
                           {"NotoSansJP_140", TILESIZE * 7, 0.8f, 0, -20, 0.12f, 1, 1},
                           {"NotoSans_140", TILESIZE * 7, 0.65f, 0, -40, 0.12f, 1, 1},
                           {"m6x11plus_200", TILESIZE * 10, 0.9f, 10, 15, 0.1f, 1, 1},
                           {"GoNotoCurrent_200", TILESIZE * 10, 0.8f, 10, -20, 0.1f, 1, 1},
                           {"GoNotoCJKCore_200", TILESIZE * 10, 0.8f, 10, -20, 0.1f, 1, 1}};
        for (const F &f : fonts) {
            Ref<FontDef> d = new FontDef();
            d->id = f.id;
            d->render_scale = f.render_scale;
            d->TEXT_HEIGHT_SCALE = f.ths;
            d->TEXT_OFFSET = {f.ox, f.oy};
            d->FONTSCALE = f.fontscale;
            d->squish = f.squish;
            d->DESCSCALE = f.descscale;
            FONTS.push_back(d);
        }
        for (const L &l : langs) {
            Ref<LangDef> d = new LangDef();
            d->key = l.key;
            d->label = l.label;
            d->font = FONTS[l.font - 1].get();
            d->beta = l.beta;
            // the beta languages' warning, shown by G.FUNCS.warn_lang
            if (d->key == "id")
                d->warning = {"Bahasa ini masih dalam tahap Beta. Untuk membantu", "kami meningkatkannya, silakan klik tombol umpan balik.",
                              "Klik lagi untuk mengonfirmasi"};
            if (d->key == "ko")
                d->warning = {"이 언어는 아직 베타 단계에 있습니다. ", "번역을 도와주시려면 피드백 버튼을 눌러주세요.", "다시 클릭해서 확인하세요"};
            if (d->key == "ru")
                d->warning = {"Этот язык все еще находится в Бета-версии. Чтобы помочь",
                              "нам его улучшить, пожалуйста, нажмите на кнопку обратной связи.", "Щелкните снова, чтобы подтвердить"};
            LANGUAGES[l.key] = d;
        }
        for (const char *k : {"all1", "all2"}) {
            Ref<LangDef> d = new LangDef();
            d->key = "all";
            d->label = "English";
            d->font = FONTS[std::string(k) == "all1" ? 7 : 8].get();
            d->omit = true;
            LANGUAGES[k] = d;
        }
    }
    auto it = LANGUAGES.find(SETTINGS->get(K("language")).to_string());
    LANG = it != LANGUAGES.end() ? it->second.get() : LANGUAGES["en-us"].get();
    // load the language's font now (LOVE loads every font at startup): loading it on first use
    // was a visible hitch when the first text appears
    LANG->font->font();
    LANGUAGES["en-us"]->font->font();
    localization = load_data("loc_" + LANG->key);
    init_localization();
}

void Globals::set_render_settings() {
    Table *gr = SETTINGS->get(K("GRAPHICS")).table();
    if (!gr->get(K("texture_scaling")).truthy()) gr->set("texture_scaling", 2);
    int ts = (int)gr->get(K("texture_scaling")).num();
    // reloading (pixel smoothing changed): free the previous images once the atlases point elsewhere
    std::set<gfx::Texture *> old_images;
    for (auto &kv : ASSET_ATLAS) old_images.insert(kv.second.image);
    for (auto &kv : ANIMATION_ATLAS) old_images.insert(kv.second.image);
    // 2x textures are exact nearest-neighbour upscales of the 1x ones: load 1x, scale virtually
    auto tex = [&](const char *file) { return load_texture(std::string("textures/1x/") + file, ts); };
    const struct { const char *name, *file; float px, py; int frames; } anim[] = {
        {"blind_chips", "BlindChips.png", 34, 34, 21}, {"shop_sign", "ShopSignAnimation.png", 113, 57, 4}};
    for (auto &a : anim) ANIMATION_ATLAS[a.name] = Atlas{a.name, tex(a.file), a.px, a.py, a.frames};
    const struct { const char *name, *file; float px, py; } atli[] = {
        {"cards_1", "8BitDeck.png", 71, 95},     {"cards_2", "8BitDeck_opt2.png", 71, 95}, {"centers", "Enhancers.png", 71, 95},
        {"Joker", "Jokers.png", 71, 95},         {"Tarot", "Tarots.png", 71, 95},          {"Voucher", "Vouchers.png", 71, 95},
        {"Booster", "boosters.png", 71, 95},     {"ui_1", "ui_assets.png", 18, 18},        {"ui_2", "ui_assets_opt2.png", 18, 18},
        {"balatro", "balatro.png", 333, 216},    {"gamepad_ui", "gamepad_ui.png", 32, 32}, {"icons", "icons.png", 66, 66},
        {"tags", "tags.png", 34, 34},            {"stickers", "stickers.png", 71, 95},     {"chips", "chips.png", 29, 29}};
    for (auto &a : atli) ASSET_ATLAS[a.name] = Atlas{a.name, tex(a.file), a.px, a.py, 0};
    // the collab face cards (71x95 frames, 213x95 per skin at 1x)
    for (const char *c : {"AU", "TW", "VS", "DTD", "CYP", "STS", "TBoI", "SV", "SK", "DS", "CL", "D2", "PC", "WF", "EG", "XR", "CR",
                          "BUG", "FO", "DBD", "C7", "R", "AC", "STP"})
        for (const char *n : {"_1", "_2"}) {
            std::string name = std::string("collab_") + c + n;
            ASSET_ATLAS[name] = Atlas{name, tex(("collabs/" + name + ".png").c_str()), 71, 95, 0};
        }
    G.I.SPRITE.each([](Moveable *m) {
        if (auto *s = dynamic_cast<Sprite *>(m)) s->reset();
    });
    ASSET_ATLAS["Planet"] = ASSET_ATLAS["Tarot"];
    ASSET_ATLAS["Spectral"] = ASSET_ATLAS["Tarot"];
    for (gfx::Texture *t : old_images)
        if (t) gfx::destroy_texture(t);
}

// ------------------------------------------------------------------ saves (progress bookkeeping)
void Globals::save_progress() {
    if (!ARGS->get(K("save_progress")).truthy()) ARGS->set("save_progress", Value(new_table()));
    Table *sp = ARGS->get(K("save_progress")).table();
    sp->set("UDA", Value(new_table()));
    sp->set("SETTINGS", Value(SETTINGS));
    sp->set("PROFILE", PROFILES->get(SETTINGS->get(K("profile"))));
    if (!FILE_HANDLER) FILE_HANDLER = new_table();
    FILE_HANDLER->set("progress", true);
    FILE_HANDLER->set("update_queued", true);
    std::string prof = SETTINGS->get(K("profile")).to_string();
    Value meta = FILES->get(Value(prof + "/meta.jkr"));
    if (!meta.truthy()) meta = Value(new_table());
    FILES->set(Value(prof + "/meta.jkr"), meta);
    for (const char *k : {"unlocked", "discovered", "alerted"})
        if (!fld(meta, k).truthy()) meta.table()->set(k, Value(new_table()));
    auto put = [&](const TableRef &src) {
        src->each([&](const Value &k, const Value &v) {
            fld(meta, "unlocked").table()->set(k, fld(v, "unlocked").truthy() ? fld(v, "unlocked") : Value());
            fld(meta, "discovered").table()->set(k, fld(v, "discovered").truthy() ? fld(v, "discovered") : Value());
            fld(meta, "alerted").table()->set(k, fld(v, "alerted").truthy() ? fld(v, "alerted") : Value());
        });
    };
    put(P_CENTERS);
    put(P_BLINDS);
    put(P_TAGS);
    put(P_SEALS);
    Value ver = fld(PROFILES->get(SETTINGS->get(K("profile"))), "ver");
    meta.table()->set("ver", ver.truthy() ? ver : Value(0));
    sp->set("META", meta);
    FILES->set(Value(prof + "/meta.jkr"), meta);
    FILES->set(Value(prof + "/profile.jkr"), sp->get(K("PROFILE")));
}

void Globals::save_notify(const Value &card) {
    save_manager_request(vl::T({{"type", "save_notify"}, {"save_notify", fld(card, "key")}, {"profile_num", SETTINGS->get(K("profile"))}}));
}

void Globals::save_settings() {
    ARGS->set("save_settings", Value(SETTINGS));
    if (!FILE_HANDLER) FILE_HANDLER = new_table();
    FILE_HANDLER->set("settings", true);
    FILE_HANDLER->set("update_queued", true);
    FILES->set(Value(SETTINGS->get(K("profile")).to_string() + "/profile.jkr"), PROFILES->get(SETTINGS->get(K("profile"))));
}

void Globals::refresh() {
    DISCOVER_TALLIES = nullptr;
    PROGRESS = nullptr;
    int prof = SETTINGS->get(K("profile")).truthy() ? (int)SETTINGS->get(K("profile")).num() : 1;
    load_profile(prof);
    init_item_prototypes();
    set_discover_tallies();
    set_profile_progress();
}

// ------------------------------------------------------------------ stages
TableRef Globals::init_game_object() { return game_object_template->deep_copy(); }

void Globals::prep_stage(int new_stage, int new_state, bool new_game_obj) {
    CONTROLLER->locks.clear();
    if (new_game_obj) GAME = init_game_object();
    STAGE = new_stage;
    STATE = new_state;
    STATE_COMPLETE = false;
    SETTINGS->set("paused", false);

    Node::Args a;
    a.T = {ROOM_PADDING_W, ROOM_PADDING_H, TILE_W, TILE_H, 0, 1};
    ROOM = new Node(a);
    ROOM_jiggle = 0;
    ROOM->states.drag.can = false;
    ROOM->set_container(ROOM.get());

    ROOM_ATTACH = new Moveable(0, 0, TILE_W, TILE_H);
    ROOM_ATTACH->states.drag.can = false;
    ROOM_ATTACH->set_container(ROOM.get());
    resize((float)gfx::screen_width(), (float)gfx::screen_height());
}

void Globals::delete_run() {
    if (ROOM) {
        // remove_all(G.STAGE_OBJECTS[G.STAGE])
        std::vector<Ref<Node>> objs(STAGE_OBJECTS[STAGE].raw().begin(), STAGE_OBJECTS[STAGE].raw().end());
        for (int i = (int)objs.size() - 1; i >= 0; i--) {
            STAGE_OBJECTS[STAGE].remove(objs[i].get());
            remove_all(objs[i]->children);
            objs[i]->remove();
        }
        for (NodeRef *n : {&buttons, &deck_preview, &shop, &blind_select, &booster_pack, &MAIN_MENU_UI, &GAME_OVER_UI,
                           &collection_alert, &HUD, &HUD_blind})
            if (*n) {
                (*n)->remove();
                *n = nullptr;
            }
        for (Ref<Sprite> *s : {&SPLASH_FRONT, &SPLASH_BACK, &SPLASH_LOGO})
            if (*s) {
                (*s)->remove();
                *s = nullptr;
            }
        // TODO(run): HUD_tags
        if (OVERLAY_MENU) {
            OVERLAY_MENU->remove();
            OVERLAY_MENU = nullptr;
        }
        // TODO(tutorial): OVERLAY_TUTORIAL (Jimbo, content)
        for (Ref<CardArea> *a : {&hand, &deck, &jokers, &consumeables, &play, &discard, &shop_jokers, &shop_vouchers, &shop_booster,
                                 &pack_cards, &title_top})
            *a = nullptr;
        I.CARD.clear();
    }
    VIEWING_DECK = false;
    E_MANAGER->clear_queue();
    CONTROLLER->mod_cursor_context_layer(-1000);
    if (GAME) GAME->set("won", false);
    STATE = -1;
}

void Globals::splash_screen() {
    if (SETTINGS->get(K("skip_splash")).equals("Yes")) {
        main_menu();
        return;
    }
    prep_stage(MAIN_MENU, SPLASH, true);
    E_MANAGER->add_event(ev_immediate([] { return true; }));
    E_MANAGER->add_event(ev_immediate([] {
        G.TIMERS.TOTAL = 0;
        G.TIMERS.REAL = 0;
        Atlas *ui1 = &G.ASSET_ATLAS["ui_1"];
        SpritePos p;
        p.x = 2;
        p.y = 0;
        G.SPLASH_BACK = new Sprite(-30, -13, G.ROOM->T->w + 60, G.ROOM->T->h + 22, ui1, p);
        {
            DrawStep st;
            st.shader = gfx::ShaderId::Splash;
            const double vort_offset = std::fmod(2 * 90.15315131 * os_time(), 100000);
            st.send = {{"time", [] { return Value(G.TIMERS.REAL); }},
                       {"vort_speed", [] { return Value(1); }},
                       {"colour_1", [] { return G.C->get(K("BLUE")); }},
                       {"colour_2", [] { return G.C->get(K("WHITE")); }},
                       {"mid_flash", [] { return Value(0); }},
                       {"vort_offset", [vort_offset] { return Value(vort_offset); }}};
            G.SPLASH_BACK->define_draw_steps({st});
        }
        {
            AlignArgs al;
            al.major = G.ROOM_ATTACH.get();
            al.type = "cm";
            XY off{0, 0};
            al.offset = &off;
            G.SPLASH_BACK->set_alignment(al);
        }
        G.SPLASH_FRONT = new Sprite(0, -20, G.ROOM->T->w * 2, G.ROOM->T->h * 4, ui1, p);
        {
            DrawStep st;
            st.shader = gfx::ShaderId::Flash;
            st.send = {{"time", [] { return Value(G.TIMERS.REAL); }}, {"mid_flash", [] { return Value(1); }}};
            G.SPLASH_FRONT->define_draw_steps({st});
            AlignArgs al;
            al.major = G.ROOM_ATTACH.get();
            al.type = "cm";
            XY off{0, 0};
            al.offset = &off;
            G.SPLASH_FRONT->set_alignment(al);
        }

        auto SC = std::make_shared<CardRef>();
        G.E_MANAGER->add_event(ev_after(0.2, [SC] {
            float s = 1.2f;
            Card *c = new Card(G.ROOM->T->w / 2 - s * G.CARD_W / 2, 10.f + G.ROOM->T->h / 2 - s * G.CARD_H / 2, s * G.CARD_W, s * G.CARD_H,
                               G.P_CARDS->get(K("empty")), G.P_CENTERS->get(K("j_joker")));
            *SC = c;
            c->T->y = G.ROOM->T->h / 2 - s * G.CARD_H / 2;
            c->ambient_tilt = 1;
            c->states.drag.can = false;
            c->states.hover.can = false;
            c->no_ui = true;
            G.VIBRATION += 2;
            G.MOBILE_VIBRATION_QUEUE = std::max(G.MOBILE_VIBRATION_QUEUE, 1.0);
            play_sound("whoosh1", 0.7f, 0.2f);
            play_sound("introPad1", 0.704f, 0.6f);
            return true;
        }));
        G.E_MANAGER->add_event(ev_after(1.8, [SC] {
            (*SC)->start_dissolve(A({G.colour("WHITE"), G.colour("WHITE")}), true, 12, true);
            play_sound("magic_crumple", 1, 0.5f);
            play_sound("splash_buildup", 1, 0.7f);
            return true;
        }));

        auto make_splash_card = [](double scale) {
            double angle = lua_math.random() * 2 * 3.14;
            double card_size = scale * (lua_math.random() + 1);
            XY pos{(float)((18 + card_size) * std::sin(angle)), (float)((18 + card_size) * std::cos(angle))};
            Value front = pseudorandom_element(Value(G.P_CARDS));
            Card *card = new Card(pos.x + G.ROOM->T->w / 2 - G.CARD_W * (float)card_size / 2, pos.y + G.ROOM->T->h / 2 - G.CARD_H * (float)card_size / 2,
                                  (float)card_size * G.CARD_W, (float)card_size * G.CARD_H, front, G.P_CENTERS->get(K("c_base")));
            if (lua_math.random() > 0.8) {
                card->sprite_facing = "back";
                card->facing = "back";
            }
            card->no_shadow = true;
            card->states.hover.can = false;
            card->states.drag.can = false;
            card->vortex = true;
            card->T->r = (float)angle;
            return std::make_pair(card, pos);
        };

        G.vortex_time = G.TIMERS.REAL;
        double temp_del = 3;
        for (int i = 1; i <= 200; i++) {
            EventConfig e = ev_after(temp_del, [i, make_splash_card] {
                auto [card, card_pos] = make_splash_card(2 - i / 300.0);
                CardRef cref(card);
                double speed = std::max(2. - i * 0.005, 0.001);
                ease_value(ValueRef::of(&card->T->scale, card), -card->T->scale, false, nullptr, 1. * speed, EaseType::Elastic);
                ease_value(ValueRef::of(&card->T->x, card), -card_pos.x, false, nullptr, 0.9 * speed);
                ease_value(ValueRef::of(&card->T->y, card), -card_pos.y, false, nullptr, 0.9 * speed);
                double temp_pitch = i * 0.007 + 0.6;
                EventConfig w = ev_immediate([cref, i, temp_pitch] {
                    if (cref->T->scale <= 0) {
                        if (i < 30) play_sound("whoosh1", (float)(temp_pitch + lua_math.random() * 0.05), (float)(0.25 * (1 - i / 50.0)));
                        if (i == 15) play_sound("whoosh_long", 0.9f, 0.7f);
                        G.VIBRATION += 0.1;
                        G.MOBILE_VIBRATION_QUEUE = std::max(G.MOBILE_VIBRATION_QUEUE, 1.0);
                        cref->remove();
                        return true;
                    }
                    return false;
                });
                w.blockable = false;
                G.E_MANAGER->add_event(std::move(w));
                return true;
            });
            e.blockable = false;
            G.E_MANAGER->add_event(std::move(e));
            temp_del = temp_del + std::max(1.0 / i, std::max(0.2 * (170 - i) / 500, 0.016));
        }
        G.E_MANAGER->add_event(ev_after(2., [] {
            G.SPLASH_BACK->remove();
            G.SPLASH_BACK = G.SPLASH_FRONT;
            G.SPLASH_FRONT = nullptr;
            G.main_menu("splash");
            return true;
        }));
        return true;
    }));
}

void Globals::main_menu(std::string_view change_context) {
    bool splash = change_context == "splash", game = change_context == "game";
    if (!splash) {
        TIMERS.REAL = 12;
        TIMERS.TOTAL = 12;
    } else {
        RESET_STATES(MENU);
    }
    prep_stage(MAIN_MENU, MENU, true);
    GAME->set("selected_back", Value((Object *)new Back(P_CENTERS->get(K("b_red")))));

    SETTINGS->set("tutorial_complete", get_cloud_flag("tutorial_complete"));
    Value tp = SETTINGS->get(K("tutorial_progress"));
    if (!SETTINGS->get(K("tutorial_complete")).truthy() && fld(fld(tp, "completed_parts"), "big_blind").truthy())
        set_cloud_flag("tutorial_complete");
    if (SETTINGS->get(K("tutorial_complete")).truthy() && tp.truthy()) {
        tp.table()->set("forced_voucher", Value());
        tp.table()->set("forced_tags", Value());
        tp.table()->set("forced_shop", Value());
    }
    // G.FUNCS.change_shadows{to_key = ...}: rewrites the same value and saves the settings
    save_settings();

    ease_background_colour(vl::T({{"new_colour", G.colour("BLACK")}, {"contrast", 1}}));

    if (SPLASH_FRONT) {
        SPLASH_FRONT->remove();
        SPLASH_FRONT = nullptr;
    }
    if (SPLASH_BACK) {
        SPLASH_BACK->remove();
        SPLASH_BACK = nullptr;
    }
    SpritePos p2;
    p2.x = 2;
    SPLASH_BACK = new Sprite(-30, -13, ROOM->T->w + 60, ROOM->T->h + 22, &ASSET_ATLAS["ui_1"], p2);
    {
        AlignArgs al;
        al.major = ROOM_ATTACH.get();
        al.type = "cm";
        XY off{0, 0};
        al.offset = &off;
        SPLASH_BACK->set_alignment(al);
    }
    TableRef splash_args = vl::T({{"mid_flash", splash ? 1.6 : 0.}});
    ease_value(ValueRef::field(splash_args, Value("mid_flash")), -(splash ? 1.6 : 0), false, nullptr, 4);

    int special = fld(ACHIEVEMENTS->get(K("completionist_plus_plus")), "earned").truthy() ? 3
                  : fld(ACHIEVEMENTS->get(K("completionist_plus")), "earned").truthy()  ? 2
                  : fld(ACHIEVEMENTS->get(K("completionist")), "earned").truthy()       ? 1
                                                                                         : 0;
    {
        DrawStep st;
        st.shader = gfx::ShaderId::Splash;
        const char *c1 = special == 3 ? "BLUE" : "RED", *c2 = special == 3 ? "RED" : "BLUE";
        st.send = {{"time", [] { return Value(G.TIMERS.REAL_SHADER); }},
                   {"vort_speed", [] { return Value(0.4); }},
                   {"colour_1", [c1] { return G.C->get(c1); }},
                   {"colour_2", [c2] { return G.C->get(c2); }},
                   {"mid_flash", [splash_args] { return splash_args->get(K("mid_flash")); }},
                   {"vort_offset", [] { return Value(0); }}};
        SPLASH_BACK->define_draw_steps({st});
    }
    // missing-controller overlay (F_CONTROLLER_REQUIRED) does not apply on the Vita
    E_MANAGER->add_event(ev_immediate([] {
        unlock_notify();
        return true;
    }));

    float SC_scale = 1.1f;
    title_top = new CardArea(0, 0, CARD_W, CARD_H, vl::T({{"card_limit", 1}, {"type", "title"}}));
    Atlas *logo = &ASSET_ATLAS["balatro"];
    SPLASH_LOGO = new Sprite(0, 0, 13 * SC_scale, 13 * SC_scale * (logo->py / logo->px), logo, SpritePos{});
    {
        AlignArgs al;
        al.major = title_top.get();
        al.type = "cm";
        Bond strong = Bond::Strong;
        al.bond = &strong;
        XY off{0, 0};
        al.offset = &off;
        SPLASH_LOGO->set_alignment(al);
        DrawStep st;
        st.shader = gfx::ShaderId::Dissolve;
        SPLASH_LOGO->define_draw_steps({st});
    }
    SPLASH_LOGO->dissolve_colours = A({G.colour("WHITE"), G.colour("WHITE")});
    SPLASH_LOGO->dissolve = 1;
    // start baking the logo's dissolve field now (worker thread): it stays invisible for 1.8 s
    if (FAST_SHADERS) SPLASH_LOGO->dissolve_field(SPLASH_LOGO->ID);

    CardRef replace_card = new Card(title_top->T->x, title_top->T->y, 1.2f * CARD_W * SC_scale, 1.2f * CARD_H * SC_scale,
                                    P_CARDS->get(K("S_A")), P_CENTERS->get(K("c_base")));
    title_top->emplace(replace_card.get());
    replace_card->states.visible = false;
    replace_card->no_ui = true;
    replace_card->ambient_tilt = 0.0f;

    {
        EventConfig e = ev_after(game ? 1.5 : 0, [replace_card, splash] {
            replace_card->states.visible = true;
            if (splash) {
                replace_card->start_materialize(A({G.colour("WHITE"), G.colour("WHITE")}), true, 2.5f);
                play_sound("whoosh1", (float)(lua_math.random() * 0.1 + 0.3), 0.3f);
                std::string c = "crumple" + std::to_string((int)lua_math.random(1, 5));
                play_sound(c.c_str(), (float)(lua_math.random() * 0.2 + 0.6), 0.65f);
            } else {
                replace_card->start_materialize(A({G.colour("WHITE"), G.colour("WHITE")}), false, 1.2f);
            }
            G.VIBRATION += 1;
            G.MOBILE_VIBRATION_QUEUE = std::max(G.MOBILE_VIBRATION_QUEUE, 2.0);
            return true;
        });
        e.blockable = false;
        e.blocking = false;
        E_MANAGER->add_event(std::move(e));
    }
    {
        EventConfig e = ev_after(splash ? 1.8 : game ? 2 : 0.5, [splash] {
            play_sound(splash ? "magic_crumple2" : "magic_crumple3", splash ? 1 : 1.3f, 0.9f);
            play_sound("whoosh1", 0.4f, 0.8f);
            ease_value(ValueRef::of(&G.SPLASH_LOGO->dissolve, G.SPLASH_LOGO.get()), -1, false, nullptr, splash ? 2.3 : 0.9);
            G.VIBRATION += 1.5;
            G.MOBILE_VIBRATION_QUEUE = std::max(G.MOBILE_VIBRATION_QUEUE, 2.0);
            return true;
        });
        e.blockable = false;
        e.blocking = false;
        E_MANAGER->add_event(std::move(e));
    }
    delay(0.1 + (splash ? 2 : game ? 1.5 : 0));

    if (fld(P_CENTERS->get(K("j_blueprint")), "unlocked").truthy()) {
        TableRef viable = new_table();
        for (int i = 1; i <= P_LOCKED->len(); i++) {
            Value v = P_LOCKED->get(i);
            if ((fld(v, "set").equals("Voucher") || fld(v, "set").equals("Joker")) && !fld(v, "demo").truthy()) viable->push(v);
        }
        if (viable->len() > 0) {
            auto card = std::make_shared<CardRef>();
            E_MANAGER->add_event(ev_after(4.04, [card, viable, replace_card, SC_scale] {
                Value c = pseudorandom_element(Value(viable));
                if (!c.truthy()) c = G.P_CENTERS->get(K("j_joker"));
                *card = new Card(G.title_top->T->x, G.title_top->T->y, 1.2f * G.CARD_W * SC_scale, 1.2f * G.CARD_H * SC_scale, Value(), c);
                (*card)->no_ui = viable->len() == 0;
                (*card)->states.visible = false;
                replace_card->parent = nullptr;
                replace_card->start_dissolve(A({G.colour("BLACK"), G.colour("ORANGE"), G.colour("RED"), G.colour("GOLD")}));
                return true;
            }));
            E_MANAGER->add_event(ev_after(1.04, [card] {
                (*card)->start_materialize();
                G.title_top->emplace(card->get());
                return true;
            }));
        }
    }

    E_MANAGER->add_event(ev_immediate([] { return true; }));  // G.CONTROLLER.lock_input = false
    set_screen_positions();
    title_top->sort("order");
    title_top->set_ranks();
    title_top->align_cards();
    title_top->hard_set_cards();

    {
        EventConfig e = ev_after(splash ? 4.05 : game ? 3 : 1.5, [] {
            set_main_menu_UI();
            return true;
        });
        e.blockable = false;
        e.blocking = false;
        E_MANAGER->add_event(std::move(e));
    }
    fld(PROFILES->get(SETTINGS->get(K("profile"))), "career_stats").table()->each([](const Value &k, const Value &) {
        check_for_unlock(vl::T({{"type", "career_stat"}, {"statname", k}}));
    });
    check_for_unlock(vl::T({{"type", "blind_discoveries"}}));
    {
        EventConfig e = ev_immediate([] {
            set_discover_tallies();
            set_profile_progress();
            G.REFRESH_ALERTS = true;
            return true;
        });
        e.blockable = false;
        E_MANAGER->add_event(std::move(e));
    }
    new UIBox(vl::T({{"n", (double)UIT_ROOT},
                     {"config", Value(vl::T({{"align", "cm"}, {"colour", fld(G.C->get(K("UI")), "TRANSPARENT_DARK")}}))},
                     {"nodes", Value(A({Value(vl::T({{"n", (double)UIT_T},
                                                     {"config", Value(vl::T({{"text", Value(VERSION)},
                                                                             {"scale", 0.3},
                                                                             {"colour", fld(G.C->get(K("UI")), "TEXT_LIGHT")}}))}}))}))}}),
              vl::T({{"align", "tri"},
                     {"offset", Value(vl::T({{"x", 0}, {"y", 0}}))},
                     {"major", Value((Object *)ROOM_ATTACH.get())},
                     {"bond", "Weak"}}));
}

// ------------------------------------------------------------------ Game:update
void Globals::update(double dt) {
    FRAMES.MOVE++;
    // TODO(tutorial): G.FUNCS.tutorial_controller() while the tutorial is not complete
    modulate_sound(dt);
    update_canvas_juice(dt);
    TIMERS.REAL += dt;
    TIMERS.REAL_SHADER = reduced_motion() ? 300 : TIMERS.REAL;
    TIMERS.UPTIME += dt;
    Value spin = ARGS->get(K("spin"));
    TIMERS.BACKGROUND += dt * (spin.truthy() ? fld(spin, "amount").num() : 0);
    real_dt = dt;

    set_alerts();
    if (paused()) dt = 0;
    if (!paused()) {
        Value name = fld(PROFILES->get(SETTINGS->get(K("profile"))), "name");
        G_TABLE->set("profile_display", name.truthy() ? name : Value("ERROR"));
    }

    static int acc_state = 0;
    static double ACC = 0;
    if (STATE != acc_state) ACC = 0;
    acc_state = STATE;
    if (STATE == HAND_PLAYED || STATE == NEW_ROUND) ACC = std::min(ACC + dt * 0.2 * SETTINGS->get(K("GAMESPEED")).num(), 16.0);
    else ACC = 0;
    SPEEDFACTOR = (STAGE == RUN && !paused() && !screenwipe) ? SETTINGS->get(K("GAMESPEED")).num() : 1;
    SPEEDFACTOR = SPEEDFACTOR + std::max(0.0, std::fabs(ACC) - 2);
    TIMERS.TOTAL += dt * SPEEDFACTOR;

    Table *de = C->get(K("DARK_EDITION")).table();
    de->set(1, 0.6 + 0.2 * std::sin(TIMERS.REAL * 1.3));
    de->set(3, 0.6 + 0.2 * (1 - std::sin(TIMERS.REAL * 1.3)));
    de->set(2, std::min(de->get(3).num(), de->get(1).num()));
    Table *ed = C->get(K("EDITION")).table();
    ed->set(1, 0.7 + 0.2 * (1 + std::sin(TIMERS.REAL * 1.5 + 0)));
    ed->set(3, 0.7 + 0.2 * (1 + std::sin(TIMERS.REAL * 1.5 + 3)));
    ed->set(2, 0.7 + 0.2 * (1 + std::sin(TIMERS.REAL * 1.5 + 6)));

    E_MANAGER->update(real_dt);
    // TODO(run): boss warning text, per-state updates (selecting hand, shop, packs, round eval...)

    ANIMATIONS.each([&](Moveable *v) { v->animate(); });
    exp_times.xy = std::exp(-50 * real_dt);
    exp_times.scale = std::exp(-60 * real_dt);
    exp_times.r = std::exp(-190 * real_dt);
    double move_dt = std::min(1.0 / 20, real_dt);
    exp_times.max_vel = 70 * move_dt;
    MOVEABLES.each([&](Moveable *v) {
        if (v->FRAME.MOVE < FRAMES.MOVE) v->move(move_dt);
    });
    MOVEABLES.each([&](Moveable *v) {
        v->update(dt * SPEEDFACTOR);
        v->states.collide.is = false;
    });
    CONTROLLER->update(real_dt);
    // TODO(run): loc_blind_states refresh

    // profile versions
    Table *counters = SETTINGS->get(K("profile_counters")).table();
    PROFILES->each([&](const Value &k, const Value &v) {
        Table *p = v.table();
        if (!p || !counters) return;
        Value ver = p->get(K("ver"));
        if (ver.truthy() && ver.num() > counters->get(k).num()) counters->set(k, ver);
        if (!ver.truthy() || ver.num() < counters->get(k).num()) p->set("ver", counters->get(k));
    });
    if (Table *cur = PROFILES->get(SETTINGS->get(K("profile"))).table()) cur->set("deleted", Value());

    // Save every F_SAVE_TIMER seconds, unless forced or paused/unpaused
    Table *fh = FILE_HANDLER.get();
    if (fh && fh->get(K("update_queued")).truthy() &&
        (fh->get(K("force")).truthy() || fh->get(K("last_sent_stage")).num() != STAGE ||
         (fh->get(K("last_sent_pause")).truthy() != paused() && fh->get(K("run")).truthy()) || !fh->get(K("last_sent_time")).truthy() ||
         fh->get(K("last_sent_time")).num() < TIMERS.UPTIME - F_SAVE_TIMER)) {
        if (fh->get(K("run")).truthy()) {
            save_manager_request(vl::T({{"type", "save_run"}, {"save_table", ARGS->get(K("save_run"))}, {"profile_num", SETTINGS->get(K("profile"))}}));
            Value sr = ARGS->get(K("save_run"));
            FILES->set(Value(SETTINGS->get(K("profile")).to_string() + "/save.jkr"), sr.is_table() ? Value(sr.table()->deep_copy()) : Value());
        }
        if (fh->get(K("progress")).truthy()) {
            save_manager_request(vl::T({{"type", "save_progress"}, {"save_progress", ARGS->get(K("save_progress"))}}));
        } else if (fh->get(K("empty_profile")).truthy()) {
            save_manager_request(vl::T({{"type", "empty_profile"},
                                        {"save_settings", ARGS->get(K("save_settings"))},
                                        {"profile_num", fh->get(K("empty_profile"))},
                                        {"save_profile", PROFILES->get(fh->get(K("empty_profile")))}}));
        } else if (fh->get(K("settings")).truthy()) {
            save_manager_request(vl::T({{"type", "save_settings"},
                                        {"save_settings", ARGS->get(K("save_settings"))},
                                        {"profile_num", SETTINGS->get(K("profile"))},
                                        {"save_profile", PROFILES->get(SETTINGS->get(K("profile")))}}));
        }
        fh->set("force", false);
        fh->set("last_sent_stage", STAGE);
        fh->set("last_sent_time", TIMERS.UPTIME);
        fh->set("last_sent_pause", paused());
        for (const char *k : {"settings", "empty_profile", "progress", "metrics", "run"}) fh->set(k, Value());
    }
}

// ------------------------------------------------------------------ Game:draw
void Globals::draw() {
    FRAMES.DRAW++;
    reset_drawhash();
    if (OVERLAY_TUTORIAL && !OVERLAY_MENU) under_overlay = true;
    gfx::set_canvas(CANVAS);
    gfx::push();
    gfx::scale(CANV_SCALE);
    gfx::set_shader(gfx::ShaderId::Default);
    gfx::clear({0, 0, 0, 1});

    if (SPLASH_BACK) {
        gfx::push();
        SPLASH_BACK->translate_container();
        if (!render::draw_background_sprite(SPLASH_BACK.get())) SPLASH_BACK->draw();
        gfx::pop();
    }
    STAGE_OBJECTS[STAGE].each([&](Node *v) { v->under_overlay = (bool)OVERLAY_TUTORIAL; });
    I.NODE.each([](Node *v) {
        if (!v->parent) draw_node(v);
    });
    I.MOVEABLE.each([](Moveable *v) {
        if (!v->parent) draw_node(v);
    });
    if (SPLASH_LOGO) draw_node(SPLASH_LOGO.get());

    if (!OVERLAY_MENU || !F_HIDE_BG) {
        I.UIBOX.each([&](Node *v) {
            auto *b = dynamic_cast<UIBox *>(v);
            if (b && !b->attention_text && !b->parent && v != OVERLAY_MENU.get() && v != screenwipe.get() && v != OVERLAY_TUTORIAL.get() &&
                v != debug_tools.get() && v != online_leaderboard.get() && v != achievement_notification.get())
                draw_node(v);
        });
        I.CARDAREA.each([](Moveable *v) {
            if (!v->parent) draw_node(v);
        });
        I.CARD.each([&](Moveable *v) {
            if (!v->parent && v != CONTROLLER->dragging.target.get() && v != CONTROLLER->focused.target.get()) draw_node(v);
        });
        I.UIBOX.each([&](Node *v) {
            auto *b = dynamic_cast<UIBox *>(v);
            if (b && b->attention_text && v != debug_tools.get() && v != online_leaderboard.get() && v != achievement_notification.get())
                draw_node(v);
        });
        if (SPLASH_FRONT) {
            gfx::push();
            SPLASH_FRONT->translate_container();
            if (!render::draw_flash_sprite(SPLASH_FRONT.get())) SPLASH_FRONT->draw();
            gfx::pop();
        }
        under_overlay = false;
        if (OVERLAY_TUTORIAL) {
            draw_node(OVERLAY_TUTORIAL.get());
            // TODO(tutorial): OVERLAY_TUTORIAL.highlights
        } else {
            DISABLED_BUTTONS = nullptr;
        }
    }
    update_overlay_zoom();
    auto draw_maybe_zoomed = [&](Node *v) {
        if (overlay_zoom.on && in_overlay(v)) draw_zoomed(v);
        else draw_node(v);
    };
    if (OVERLAY_MENU && OVERLAY_MENU.get() != CONTROLLER->dragging.target.get()) draw_maybe_zoomed(OVERLAY_MENU.get());
    if (debug_tools && debug_tools.get() != CONTROLLER->dragging.target.get()) draw_node(debug_tools.get());
    I.ALERT.each([&](Node *v) { draw_maybe_zoomed(v); });
    if (CONTROLLER->dragging.target && CONTROLLER->dragging.target != CONTROLLER->focused.target)
        draw_node(CONTROLLER->dragging.target.get());
    if (Card *fc = dynamic_cast<Card *>(CONTROLLER->focused.target.get())) {
        if (!(hand && fc->area == hand.get()) || CONTROLLER->focused.target == CONTROLLER->dragging.target) draw_node(fc);
    }
    I.POPUP.each([&](Node *v) { draw_maybe_zoomed(v); });
    if (achievement_notification) draw_node(achievement_notification.get());
    if (screenwipe) draw_node(screenwipe.get());
    gfx::push();
    CURSOR->translate_container();
    gfx::translate(-CURSOR->T->w * TILESCALE * TILESIZE * 0.5f, -CURSOR->T->h * TILESCALE * TILESIZE * 0.5f);
    CURSOR->draw();
    gfx::pop();
    gfx::pop();

    // CRT pass to the screen (G.AA_CANVAS is nil: love.graphics.setCanvas(nil))
    gfx::set_canvas(nullptr);
    if (!eased_cursor_pos.set)
        eased_cursor_pos = {CURSOR->T->x, CURSOR->T->y, CONTROLLER->cursor_position.x, CONTROLLER->cursor_position.y, true};
    screenwipe_amt = has_screenwipe_amt ? 0.95 * screenwipe_amt + 0.05 * ((screenwipe || screenglitch) ? 0.4 : 0) : 1;
    has_screenwipe_amt = true;
    render::CrtParams crt;
    crt.crt = (float)(SETTINGS->get(K("GRAPHICS")).get(Value("crt")).num() * 0.3);
    crt.time = (float)(400 + TIMERS.REAL);
    crt.mouse_x = (float)eased_cursor_pos.sx;
    crt.mouse_y = (float)eased_cursor_pos.sy;
    crt.screen_scale = TILESCALE * TILESIZE;
    render::crt_pass(CANVAS, crt);
}

}  // namespace vl
