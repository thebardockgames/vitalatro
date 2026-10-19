#pragma once
// engine/node.lua + engine/moveable.lua
#include <functional>
#include <string_view>
#include <vector>

#include "core/object.h"
#include "core/value.h"

namespace vl {

class Node;
class Moveable;
class UIBox;
class UIElement;
using NodeRef = Ref<Node>;

struct Transform {
    float x = 0, y = 0, w = 1, h = 1, r = 0, scale = 1;
};

struct State {
    bool can = false, is = false;
};
struct States {
    bool visible = true;
    State collide{false, false};
    State focus{false, false};
    State hover{true, false};
    State click{true, false};
    State drag{true, false};
    State release_on{true, false};
};

struct XY {
    float x = 0, y = 0;
};

// self.children in Lua is a table holding both an array part (UI elements, particles) and named
// children ('front', 'h_popup', ...). pairs() visits the array part first.
class Children {
public:
    Node *get(Str *name) const;
    Node *get(std::string_view name) const { return get(intern(name)); }
    void set(Str *name, Node *n);  // nullptr removes
    void set(std::string_view name, Node *n) { set(intern(name), n); }
    void push(Node *n);
    void remove_index(int i);      // table.remove(children, i) on the array part
    void swap(int i, int j) { std::swap(array_[i], array_[j]); }  // SWAP(children, i, j), 0-based
    void remove(Node *n);          // remove wherever it is (array or named)
    int count() const { return (int)array_.size(); }
    Node *at(int i) const { return array_[i].get(); }  // array part, 0-based
    bool empty() const { return array_.empty() && named_.empty(); }
    void clear() { array_.clear(); named_.clear(); }
    // pairs() order: array part, then named children in insertion order
    template <class F> void each(F &&f) const {
        // copies so callbacks may modify the container
        std::vector<NodeRef> a(array_.begin(), array_.end());
        for (auto &n : a) f((Str *)nullptr, n.get());
        std::vector<std::pair<Str *, NodeRef>> b(named_.begin(), named_.end());
        for (auto &p : b) f(p.first, p.second.get());
    }
    template <class F> void each_array(F &&f) const {
        std::vector<NodeRef> a(array_.begin(), array_.end());
        for (auto &n : a) f(n.get());
    }

private:
    std::vector<NodeRef> array_;
    std::vector<std::pair<Str *, NodeRef>> named_;
};

// Node: anything with a transform in the game (engine/node.lua)
class Node : public Object {
public:
    struct Args {
        Transform T;
        Node *container = nullptr;
    };
    Node() = default;
    explicit Node(const Args &args) { init(args); }
    ~Node() override;

    void init(const Args &args);

    // virtual interface (Lua methods that subclasses override)
    virtual void draw();
    virtual bool collides_with_point(XY point);
    virtual void set_offset(XY point, std::string_view type);
    virtual void drag(const XY *offset = nullptr);
    virtual Node *can_drag();
    virtual void stop_drag();
    virtual void hover();
    virtual void stop_hover();
    virtual bool simple_touch() { return true; }
    virtual bool can_long_press() { return false; }
    virtual bool can_hover_on_drag() { return true; }
    virtual XY put_focused_cursor();
    virtual void set_container(Node *container);
    virtual void translate_container();
    virtual void remove();
    virtual void release(Node *dragged) {}
    virtual void click() {}
    virtual void animate() {}
    virtual void update(double dt) {}
    virtual void move(double dt) {}
    virtual bool is_moveable() const { return false; }
    // Card hooks used by the engine (controller): overridden by Card
    virtual bool is_card() const { return false; }
    virtual bool has_single_tap() const { return false; }
    virtual void single_tap() {}

    void draw_boundingrect();
    float fast_mid_dist(Node *other);

    // Fields set ad hoc by game code (self.foo = ...) that have no typed member.
    Value var(std::string_view name) const { return vars_ ? vars_->get(Value(intern(name))) : Value(); }
    void set_var(std::string_view name, Value v);

    Transform Tstore;
    Transform *T = &Tstore;   // may alias another node's transform (glue_to_major)
    Transform *CT = &Tstore;  // transform used for collision
    XY click_offset, hover_offset;
    bool created_on_pause = false;
    int ID = 0;
    struct {
        int DRAW = -1, MOVE = -1;
    } FRAME;
    States states;
    NodeRef container;
    Children children;
    TableRef config = new_table();
    bool REMOVED = false;
    bool under_overlay = false;
    bool zoomed = false;  // drawn inside the enlarged overlay menu last frame (G.overlay_zoom)
    Node *parent = nullptr;  // non-owning (UI parent / attach parent)
    Value DEBUG_VALUE;
    bool CALCING = false;

protected:
    TableRef vars_;
};

// Moveable: transform T eases into a visible transform VT (engine/moveable.lua)
enum class RoleType : uint8_t { Major, Minor, Glued };
enum class Bond : uint8_t { Strong, Weak };

struct Role {
    RoleType role_type = RoleType::Major;
    XY offset;
    Ref<Moveable> major;
    Moveable *draw_major = nullptr;
    Bond xy_bond = Bond::Strong, wh_bond = Bond::Strong, r_bond = Bond::Strong, scale_bond = Bond::Strong;
};

// arguments of Moveable:set_role (nil fields keep the current value)
struct RoleArgs {
    const RoleType *role_type = nullptr;
    const XY *offset = nullptr;
    Moveable *major = nullptr;
    const Bond *xy_bond = nullptr, *wh_bond = nullptr, *r_bond = nullptr, *scale_bond = nullptr;
    Moveable *draw_major = nullptr;
};

struct Alignment {
    std::string type = "a";
    XY offset;
    std::string prev_type = "";
    XY prev_offset;
    bool lr_clamp = false;
    struct {
        bool a, m, c, b, t, l, r, i;
    } list{};
};

struct AlignArgs {
    Moveable *major = nullptr;
    const Bond *bond = nullptr;  // xy bond, default Weak when major is given
    const Bond *wh_bond = nullptr, *r_bond = nullptr, *scale_bond = nullptr;
    const char *type = nullptr;
    const XY *offset = nullptr;
    bool lr_clamp = false;
};

struct Juice {
    float scale = 0, scale_amt = 0, r = 0, r_amt = 0;
    double start_time = 0, end_time = 0;
    bool handled_elsewhere = false;
};

struct MajorInfo {  // get_major() result
    Moveable *major = nullptr;
    XY offset;
};

class Moveable : public Node {
public:
    Moveable() = default;
    Moveable(float x, float y, float w, float h) { init(x, y, w, h); }
    explicit Moveable(const Args &args) { init(args); }

    void init(float x, float y, float w, float h);
    void init(const Args &args);
    bool is_moveable() const override { return true; }

    void draw() override;
    void drag(const XY *offset = nullptr) override;
    void move(double dt) override;
    void remove() override;

    virtual void juice_up(float amount = 0.4f, const float *rot_amt = nullptr);
    virtual void hard_set_T(float x, float y, float w, float h);
    virtual void set_role(const RoleArgs &args);
    virtual void move_with_major(double dt);
    virtual void align_to_major();
    virtual void calculate_parrallax();

    void set_alignment(const AlignArgs &args);
    void hard_set_VT();
    void move_juice(double dt);
    void lr_clamp();
    void glue_to_major(Moveable *major_tab);
    void move_xy(double dt);
    void move_scale(double dt);
    void move_wh(double dt);
    void move_r(double dt, const XY &vel_xy);
    MajorInfo get_major();

    Transform VT;
    struct {
        float x = 0, y = 0, r = 0, scale = 0, mag = 0;
    } velocity;
    Role role;
    Alignment alignment;
    struct Pinch {
        bool x = false, y = false;
    } pinch_store;
    Pinch *pinch = &pinch_store;  // aliased by glue_to_major
    double last_moved = -1, last_aligned = -1;
    bool static_rotation = false;
    XY offset;
    Moveable *Mid = nullptr;  // self or a UI element used for alignment
    XY shadow_parrallax_store{0, -1.5f};
    XY *shadow_parrallax = &shadow_parrallax_store;
    XY layered_parallax;
    // self.layered_parallax may alias another object's table (Card:set_card_area shares the
    // area's) or be nil (card sprites), in which case prep_draw falls back to the parent's.
    Moveable *layered_parallax_of = nullptr;
    bool layered_parallax_nil = false;
    const XY *lp() const {
        if (layered_parallax_nil) return nullptr;
        return layered_parallax_of ? layered_parallax_of->lp() : &layered_parallax;
    }
    float shadow_height = 0.2f;
    bool zoom = false;
    bool STATIONARY = false;
    bool NEW_ALIGNMENT = false;
    bool has_juice = false;
    Juice juice;
    // FRAME.MAJOR caching from get_major()
    bool frame_major_valid = false;
    MajorInfo frame_major;
    bool refresh_movement = false;  // config.refresh_movement

    // Read by Sprite:draw_shader from the sprite's draw_major (usually a Card)
    float dissolve = 0;
    TableRef dissolve_colours;  // {colour, colour} or nil
    bool touch_collide_tilt = false;
    float hover_tilt = 0;
    bool has_tilt_var = false;
    struct { float mx = 0, my = 0, dx = 0, dy = 0, amt = 0; } tilt_var;
    float mouse_damping = 1;
    bool has_mouse_damping = false;
};

// ------------------------------------------------------------------ helpers (misc_functions.lua)
void point_translate(XY &p, const XY &delta);
void point_rotate(XY &p, float angle);
void prep_draw(Moveable *m, float scale, float rotate = 0, const XY *offset = nullptr, bool force_rotate = false);
void add_to_drawhash(Node *n);

}  // namespace vl
