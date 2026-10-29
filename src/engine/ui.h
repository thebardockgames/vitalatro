#pragma once
// engine/ui.lua: UIBox (root of a UI graph) and UIElement (its nodes). Definitions and configs
// are dynamic tables exactly as in the original: {n = G.UIT.X, config = {...}, nodes = {...}}.
#include <map>

#include "core/math.h"
#include "engine/node.h"

namespace vl {

class UIElement;

class UIBox : public Moveable {
public:
    // UIBox{T = ..., definition = ..., config = ...}
    UIBox(TableRef definition, TableRef config, const Transform *T = nullptr);

    UIElement *get_UIE_by_ID(std::string_view id, UIElement *node = nullptr);
    void calculate_xywh(UIElement *node, Transform &T, bool recalculate = false, float scale = 1, bool has_scale = false);
    bool remove_group(UIElement *node, const Value &group);
    void get_group(UIElement *node, const Value &group, std::vector<UIElement *> &out);
    void set_parent_child(const TableRef &node, UIElement *parent);
    void remove() override;
    void draw() override;
    void recalculate();
    void move(double dt) override;
    void drag(const XY *offset = nullptr) override;
    void add_child(const TableRef &node, UIElement *parent);
    void set_container(Node *container) override;

    TableRef definition;
    Ref<UIElement> UIRoot;
    std::map<int, UIElement *> draw_layers;
    bool attention_text = false;
    bool has_config = false;
};
using UIBoxRef = Ref<UIBox>;

class UIElement : public Moveable {
public:
    UIElement(UIElement *parent, UIBox *box, int uit, TableRef config);

    void set_values(const Transform &T, bool recalculate);
    void initialize_VT();
    void juice_up(float amount = 0.4f, const float *rot_amt = nullptr) override;
    Node *can_drag() override;
    void draw() override {}
    void draw_children();
    XY set_wh();
    void align(float x, float y);
    void set_alignments();
    void update_text();
    void update_object();
    void draw_self();
    void draw_pixellated_rect(int type, float parallax, float emboss = 0, bool has_emboss = false, float progress = 1);
    void update(double dt) override;
    bool collides_with_point(XY p) override;
    void click() override;
    XY put_focused_cursor() override;
    void remove() override;
    void hover() override;
    void stop_hover() override;
    void release(Node *other) override;

    Moveable *object() const;  // config.object
    UIElement *parent_uie() const { return dynamic_cast<UIElement *>(parent); }

    UIBox *box = nullptr;  // self.UIBox
    int UIT = 0;
    struct {
        float w = 0, h = 0;
    } content_dimensions;
    // per-element state used by draw/click
    bool button_clicked = false;
    double last_clicked = -1e9;
    bool has_last_clicked = false;
    double focus_timer = 0, object_focus_timer = 0, border_pulse_timer = 0;
    bool has_focus_timer = false, has_object_focus_timer = false, has_border_pulse_timer = false;
    Value tutorial_disabled;
    bool disable_button = false;
    bool initialized = false;  // Moveable.init has run (set_values)

    struct RectCache {
        bool valid = false;
        float w, h, sw, sh, progress, parallax;
        std::vector<float> fill, shadow, line, emboss, line_emboss;
    } pixellated_rect;
};

enum RectType { RECT_FILL, RECT_SHADOW, RECT_LINE, RECT_EMBOSS, RECT_LINE_EMBOSS };

// Vitalatro: while set, UI elements it reports as hidden (behind an enlarged overlay panel) are not
// drawn, nor their children
extern bool (*g_ui_hidden)(Node *);

// functions/misc_functions.lua remove_all(t)
void remove_all(Children &c);

// helpers for colour tables {r, g, b, a}
Color colour_of(const Value &c);
TableRef mix_colours(const Value &c1, const Value &c2, float p);
TableRef darken(const Value &c, float p);
TableRef lighten(const Value &c, float p);
TableRef adjust_alpha(const Value &c, float a);
TableRef HEX(std::string_view hex);
void set_color(const Value &c);  // love.graphics.setColor(table)

}  // namespace vl
