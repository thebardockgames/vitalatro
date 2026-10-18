#pragma once
// The global G table of the original game. Engine-level state is typed; game data that the
// original keeps in plain Lua tables (GAME, SETTINGS, PROFILES, C, ...) stays dynamic.
#include <functional>
#include <unordered_map>
#include <vector>

#include "core/value.h"
#include "engine/node.h"

namespace vl {

class Controller;
class EventManager;
class Sprite;
class UIBox;
class Card;
class CardArea;
class Font;
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

struct FontDef {  // G.FONTS entries
    Ref<Font> FONT;
    float render_scale, TEXT_HEIGHT_SCALE, FONTSCALE, squish, DESCSCALE;
    XY TEXT_OFFSET;
};
struct LangDef {  // G.LANGUAGES entries
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

struct Globals {
    // ---------------------------------------------------------------- feature flags (globals.lua)
    bool F_MOBILE = false, F_SOUND_THREAD = false, F_HIDE_BG = false, F_VIDEO_SETTINGS = false;
    bool F_QUIT_BUTTON = false, F_SKIP_TUTORIAL = false, F_ENGLISH_ONLY = false, F_GUIDE = false;
    bool F_CRASH_REPORTS = false, F_NO_ACHIEVEMENTS = false, F_EXTERNAL_LINKS = false;
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
    int REFRESH_FRAME_MAJOR_CACHE = 0;

    // ---------------------------------------------------------------- dynamic data
    TableRef SETTINGS, GAME, PROFILES, C, ARGS, P_CENTERS, P_CARDS, P_BLINDS, P_TAGS, P_SEALS, P_STAKES;
    TableRef localization;

    // ---------------------------------------------------------------- instances
    struct {
        ObjList<Node> NODE;
        ObjList<Moveable> MOVEABLE;
        ObjList<Moveable> SPRITE;
        ObjList<UIBox> UIBOX;
        ObjList<UIBox> POPUP;
        ObjList<Moveable> CARD;
        ObjList<Moveable> CARDAREA;
        ObjList<UIBox> ALERT;
    } I;
    ObjList<Moveable> MOVEABLES;
    ObjList<Moveable> ANIMATIONS;
    std::vector<Node *> DRAW_HASH;
    ObjList<Node> STAGE_OBJECTS[5];

    Ref<Moveable> ROOM, ROOM_ATTACH;
    Controller *CONTROLLER = nullptr;
    EventManager *E_MANAGER = nullptr;
    Ref<Moveable> OVERLAY_MENU, OVERLAY_TUTORIAL;

    std::unordered_map<std::string, Atlas> ASSET_ATLAS, ANIMATION_ATLAS;
    std::vector<FontDef> FONTS;
    std::unordered_map<std::string, LangDef> LANGUAGES;
    LangDef *LANG = nullptr;
    std::unordered_map<std::string, UIFunc> FUNCS;

    const float *BRUTE_OVERLAY = nullptr;

    // ---------------------------------------------------------------- helpers
    bool paused() const;
    bool reduced_motion() const;
    bool shadows_on() const;
    Value colour(std::string_view name) const;  // G.C.NAME
};

extern Globals G;

}  // namespace vl
