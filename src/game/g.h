#pragma once
// The global G table of the original game. Engine-level state is typed; game data that the
// original keeps in plain Lua tables (GAME, SETTINGS, PROFILES, C, ...) stays dynamic.
#include <functional>
#include <unordered_map>
#include <vector>

#include "core/object.h"
#include "core/value.h"
#include "engine/font.h"
#include "engine/node.h"

namespace vl {

class Controller;
class EventManager;
class Sprite;
class UIBox;
class Card;
class CardArea;
class Card;
class Sprite;

namespace gfx { struct Texture; }

// Object lists that keep insertion order (Lua arrays + table.remove) with O(1) removal.
template <class T>
class ObjList {
public:
    void add(T *o) { items_.push_back(Ref<T>(o)); }
    void remove(const void *o) {
        for (size_t i = 0; i < items_.size(); i++)
            if ((const void *)items_[i].get() == o) { items_.erase(items_.begin() + i); return; }
    }
    bool contains(T *o) const {
        for (auto &r : items_) if (r.get() == o) return true;
        return false;
    }
    size_t size() const { return items_.size(); }
    T *operator[](size_t i) const { return items_[i].get(); }
    // snapshot iteration: the callback may add/remove objects
    template <class F> void each(F &&f) const {
        std::vector<Ref<T>> snap(items_);
        for (auto &r : snap) f(r.get());
    }
    void clear() { items_.clear(); }
    std::vector<Ref<T>> &raw() { return items_; }

private:
    std::vector<Ref<T>> items_;
};

enum GameState : int {
    SELECTING_HAND = 1, HAND_PLAYED = 2, DRAW_TO_HAND = 3, GAME_OVER = 4, SHOP = 5, PLAY_TAROT = 6,
    BLIND_SELECT = 7, ROUND_EVAL = 8, TAROT_PACK = 9, PLANET_PACK = 10, MENU = 11, TUTORIAL = 12,
    SPLASH = 13, SANDBOX = 14, SPECTRAL_PACK = 15, DEMO_CTA = 16, STANDARD_PACK = 17,
    BUFFOON_PACK = 18, NEW_ROUND = 19, ASYNC_WAIT = 20, ASYNC_OFFLINE = 21,
};
enum GameStage : int { MAIN_MENU = 1, RUN = 2, STAGE_SANDBOX = 3, ASYNC_LIMBO = 4 };

// G.UIT
enum UIT : int { UIT_T = 1, UIT_B = 2, UIT_C = 3, UIT_R = 4, UIT_O = 5, UIT_ROOT = 7, UIT_S = 8, UIT_I = 9 };

struct FontDef : Object {  // G.FONTS entries (referenced from DynaText configs)
    std::string id;  // baked font id; FONT is loaded on first use (CJK pages are large)
    Ref<Font> FONT;
    Font *font() {
        if (!FONT) FONT = Font::load(id);
        return FONT.get();
    }
    float render_scale, TEXT_HEIGHT_SCALE, FONTSCALE, squish, DESCSCALE;
    XY TEXT_OFFSET;
};
struct LangDef : Object {  // G.LANGUAGES entries (referenced from UI configs)
    std::string key, label, button;
    FontDef *font = nullptr;
    bool beta = false, omit = false;
    std::vector<std::string> warning;
};

struct Atlas {  // G.ASSET_ATLAS / G.ANIMATION_ATLAS entries
    std::string name;
    gfx::Texture *image = nullptr;
    float px = 0, py = 0;
    int frames = 0;
};

using UIFunc = std::function<void(UIElement *e)>;
using TableFunc = std::function<void(const TableRef &args)>;  // G.FUNCS called with an args table

struct Globals {
    // ---------------------------------------------------------------- feature flags (globals.lua)
    bool F_MOBILE = false, F_SOUND_THREAD = false, F_HIDE_BG = false, F_VIDEO_SETTINGS = false;
    bool F_QUIT_BUTTON = false, F_SKIP_TUTORIAL = false, F_ENGLISH_ONLY = false, F_GUIDE = false;
    bool F_CRASH_REPORTS = false, F_NO_ACHIEVEMENTS = false, F_EXTERNAL_LINKS = false;
    bool F_PS4_PLAYSTATION_GLYPHS = true;  // Vita face buttons match the PS4 glyph row
    bool F_MUTE = false;
    double PITCH_MOD = 1, SPLASH_VOL = 1;  // G.SPLASH_VOL starts nil: "or 1"
    bool normal_music_speed = false;
    bool F_SWAP_AB_PIPS = false, F_SWAP_AB_BUTTONS = false, F_SWAP_XY_BUTTONS = false;
    double F_RUMBLE = 0;
    int F_SAVE_TIMER = 30;

    // ---------------------------------------------------------------- timing
    struct {
        double TOTAL = 0, REAL = 0, REAL_SHADER = 0, UPTIME = 0, BACKGROUND = 0;
    } TIMERS;
    struct {
        int DRAW = 0, MOVE = 0;
    } FRAMES;
    struct {
        double xy = 0, scale = 0, r = 0, max_vel = 0;
    } exp_times;
    double real_dt = 0, SPEEDFACTOR = 1;
    int ID = 1;

    // ---------------------------------------------------------------- render scale
    float TILESIZE = 20, TILESCALE = 3.65f, TILE_W = 21, TILE_H = 11.2f;
    float DRAW_HASH_BUFF = 2;
    float CARD_W = 2.4f * 35 / 41, CARD_H = 2.4f * 47 / 41;
    float HIGHLIGHT_H = 0.2f * (2.4f * 47 / 41);
    float COLLISION_BUFFER = 0.05f;
    float CANV_SCALE = 1;
    float ROOM_PADDING_H = 0.4f, ROOM_PADDING_W = 0.8f;
    float ANIMATION_FPS = 10;
    float MIN_CLICK_DIST = 0.9f, MIN_HOVER_TIME = 0.1f;

    // ---------------------------------------------------------------- state
    int STATE = SPLASH, STAGE = MAIN_MENU;
    bool STATE_COMPLETE = false;
    bool STAGE_OBJECT_INTERRUPT = false;
    bool DEBUG = false;
    bool under_overlay = false;
    bool FAST_SHADERS = false;  // Vitalatro's GPU-friendly shader variants (render::init)
    int REFRESH_FRAME_MAJOR_CACHE = 0;
    bool REFRESH_ALERTS = false;
    bool NO_MOD_CURSOR_STACK = false;
    TableRef DISABLED_BUTTONS;  // tutorial: list of button names
    double ROOM_jiggle = 0;     // G.ROOM.jiggle
    double VIBRATION = 0, MOBILE_VIBRATION_QUEUE = 0;

    // ---------------------------------------------------------------- dynamic data
    TableRef G_TABLE = new_table();  // ref_table for UI fields read from G itself (G.profile_display)
    TableRef SETTINGS, GAME, PROFILES, C, ARGS, P_CENTERS, P_CARDS, P_BLINDS, P_TAGS, P_SEALS, P_STAKES;
    TableRef localization;

    // ---------------------------------------------------------------- instances
    struct {
        ObjList<Node> NODE;
        ObjList<Moveable> MOVEABLE;
        ObjList<Moveable> SPRITE;
        ObjList<Node> UIBOX;
        ObjList<Node> POPUP;
        ObjList<Moveable> CARD;
        ObjList<Moveable> CARDAREA;
        ObjList<Node> ALERT;
    } I;
    ObjList<Moveable> MOVEABLES;
    ObjList<Moveable> ANIMATIONS;
    std::vector<NodeRef> DRAW_HASH;  // keeps drawn nodes alive until the next draw, like the Lua table
    ObjList<Node> STAGE_OBJECTS[5];

    NodeRef ROOM;
    Ref<Moveable> ROOM_ATTACH, CURSOR;
    NodeRef screenwipe;
    Ref<Moveable> screenwipecard;
    Controller *CONTROLLER = nullptr;
    EventManager *E_MANAGER = nullptr;
    Ref<Moveable> OVERLAY_MENU, OVERLAY_TUTORIAL;

    std::unordered_map<std::string, Atlas> ASSET_ATLAS, ANIMATION_ATLAS;
    std::vector<Ref<FontDef>> FONTS;
    std::unordered_map<std::string, Ref<LangDef>> LANGUAGES;
    LangDef *LANG = nullptr;
    std::unordered_map<std::string, UIFunc> FUNCS;
    std::unordered_map<std::string, TableFunc> FUNCS_T;

    const float *BRUTE_OVERLAY = nullptr;
    double vortex_time = 0;

    // ---------------------------------------------------------------- game objects (game.lua)
    TableRef P_CENTER_POOLS, P_JOKER_RARITY_POOLS, P_LOCKED, CHALLENGES, DISCOVER_TALLIES, PROGRESS, FILES, METRICS;
    TableRef ACHIEVEMENTS, COLLABS;
    Value j_locked, v_locked, c_locked, j_undiscovered, t_undiscovered, p_undiscovered, s_undiscovered, v_undiscovered,
        booster_undiscovered, tag_undiscovered, b_undiscovered;
    Ref<CardArea> hand, deck, jokers, consumeables, play, discard, shop_jokers, shop_vouchers, shop_booster, pack_cards,
        title_top;
    std::vector<Ref<Card>> playing_cards;
    bool has_playing_cards = false;  // G.playing_cards ~= nil
    Ref<Sprite> shared_debuff, shared_soul, shared_undiscovered_joker, shared_undiscovered_tarot, shared_sticker_eternal,
        shared_sticker_perishable, shared_sticker_rental;
    std::unordered_map<std::string, Ref<Sprite>> shared_stickers, shared_seals;
    std::vector<std::string> sticker_map;
    Ref<Sprite> SPLASH_BACK, SPLASH_FRONT, SPLASH_LOGO;
    NodeRef MAIN_MENU_UI, buttons, deck_preview, shop, blind_select, booster_pack, GAME_OVER_UI, collection_alert, HUD,
        HUD_blind, debug_tools, online_leaderboard, achievement_notification, boss_warning_text;
    int sort_id = 0;
    double last_materialized = 0;
    bool has_last_materialized = false;
    bool VIEWING_DECK = false;
    std::string viewed_collab;  // G.viewed_collab
    // Vitalatro: overlay menus drawn enlarged about the screen centre to fill the small screen
    // (SETTINGS.vl_large_menus). Positions are in screen units (tiles).
    struct OverlayZoom {
        bool on = false;
        float s = 1, cx = 0, cy = 0;
        XY zoom(XY p) const { return on ? XY{cx + (p.x - cx) * s, cy + (p.y - cy) * s} : p; }
        XY unzoom(XY p) const { return on ? XY{cx + (p.x - cx) / s, cy + (p.y - cy) / s} : p; }
    } overlay_zoom;
    bool drawing_zoomed = false;
    // GPU bench (hold L+R): 0 normal, 1 no CRT pass, 2 no background, 3 no overlay menu, 4 no cards,
    // 5 overlay unzoomed, 6 no occlusion pre-pass, 7 card effect shaders
    // replaced by the plain card shader, 8-11 one edition shader plain, 12 no background field pass
    int vl_bench = 0;
    int focused_profile = 1;    // G.focused_profile
    std::vector<Ref<CardArea>> your_collection;  // G.your_collection (one area for the seals page)
    int CHECK_PROFILE_DATA = -1;  // G.CHECK_PROFILE_DATA: -1 nil, 0 false, 1 true
    TableRef collab_credits;    // G.collab_credits (built by the credits' Collabs tab)
    int TAROT_INTERRUPT = 0;
    bool screenglitch = false;
    double screenwipe_amt = 1;
    bool has_screenwipe_amt = false;
    std::string VERSION;
    gfx::Canvas *CANVAS = nullptr;
    struct {
        float orig_scale = 0, w = 0, h = 0, orig_ratio = 0;
    } window_prev;
    XY ROOM_ORIG;
    struct {
        float x = 0, y = 0, w = 0, h = 0, real_window_w = 0, real_window_h = 0;
    } WINDOWTRANS;
    struct {
        double x = 0, y = 0, sx = 0, sy = 0;
        bool set = false;
    } eased_cursor_pos;  // G.ARGS.eased_cursor_pos
    double JIGGLE_VIBRATION = 0, CURR_VIBRATION = 0;
    NodeRef PROFILE_BUTTON;
    std::unordered_map<std::string, NodeRef> alert_uiboxes;  // G[v.alert_uibox_name]
    TableRef FILE_HANDLER, SAVE_HANDLER;

    // ---------------------------------------------------------------- helpers
    // game.lua (Game methods)
    void start_up();
    void delete_run();
    void main_menu(std::string_view change_context = {});
    void splash_screen();
    void debug_table();
    bool vl_debug_table = false;  // boot into debug_table() (hold R at boot; VL_DEBUG_TABLE=1 on PC)
    void prep_stage(int new_stage, int new_state, bool new_game_obj);
    TableRef init_game_object();
    void init_item_prototypes();
    void load_profile(int profile);
    void empty_profile(int profile);
    void set_language();
    void set_render_settings();
    void save_progress();
    void save_notify(const Value &card);
    void save_settings();
    void refresh();
    void update(double dt);
    void draw();

    bool paused() const;
    bool reduced_motion() const;
    bool shadows_on() const;
    Value colour(std::string_view name) const;  // G.C.NAME
};

extern Globals G;

}  // namespace vl
