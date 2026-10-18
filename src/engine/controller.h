#pragma once
// engine/controller.lua: all input handling (pointer/touch/gamepad), focus navigation, hover,
// drag and the button registry.
#include <map>
#include <string>
#include <vector>

#include "engine/node.h"

namespace vl {

struct TargetState {
    NodeRef target, prev_target;
    bool handled = true;
    double time = 0;
};

struct CursorState {
    XY T;
    NodeRef target;
    double time = 0;
    bool handled = true;
    bool has_distance = false;
    float distance = 0;
    double duration = 0;
    bool has_duration = false;
};

class Controller {
public:
    Controller();

    void set_gamepad();  // the Vita pad is always a Playstation gamepad

    void set_HID_flags(const std::string &type, const std::string &button = "");
    void set_cursor_position();
    void update(double dt);
    void cull_registry();
    void add_to_registry(Node *node, const std::string &registry);
    void process_registry();
    void mod_cursor_context_layer(int delta);
    void snap_to(Node *node, const XY *T = nullptr);
    bool save_cardarea_focus(const std::string &cardarea);
    void recall_cardarea_focus(const std::string &cardarea);
    void update_cursor(const XY *hard_set_T = nullptr);
    void handle_axis_buttons();
    std::string update_axis(double dt);
    void button_press_update(const std::string &button, double dt);
    void button_hold_update(const std::string &button, double dt);
    void button_release_update(const std::string &button, double dt);
    void key_press_update(const std::string &key, double dt);
    void key_hold_update(const std::string &key, double dt);
    void key_release_update(const std::string &key, double dt);
    void key_press(const std::string &key);
    void key_release(const std::string &key);
    void button_press(const std::string &button);
    void button_release(const std::string &button);
    void get_cursor_collision(const XY &cursor);
    void set_cursor_hover();
    void queue_L_cursor_press(float x, float y);
    void queue_R_cursor_press();
    void L_cursor_press(const float *x = nullptr, const float *y = nullptr);
    void L_cursor_release(const float *x = nullptr, const float *y = nullptr);
    bool is_node_focusable(Node *node);
    void update_focus(char dir = 0);
    bool capture_focused_input(const std::string &button, const std::string &input_type, double dt);
    void navigate_focus(char dir = 0);

    // Node:remove clears every reference the controller holds to the node.
    void forget(Node *n);

    TargetState clicked, focused, dragging, hovering, released_on;
    struct {
        TargetState s_tap, l_press;
        bool clear_touch = false;
        NodeRef l_pressed;
    } touch_control;
    std::vector<NodeRef> collision_list, nodes_at_cursor;
    CursorState cursor_down, cursor_up, cursor_hover;
    XY cursor_position;  // screen pixels
    XY mouse_position;   // love.mouse.getPosition(), fed by the platform layer

    std::map<std::string, bool> pressed_keys, held_keys, released_keys;
    std::map<std::string, double> held_key_times;
    std::map<std::string, bool> pressed_buttons, held_buttons, released_buttons;
    std::map<std::string, double> held_button_times;

    struct {
        bool focus = false;
        bool stack = false;
    } interrupt;
    std::map<std::string, bool> locks;
    bool locked = false;

    struct AxisButton {
        std::string current, previous;
    };
    AxisButton l_stick, r_stick, l_trig, r_trig;
    float axis_cursor_speed = 20;

    struct RegistryEntry {
        NodeRef node;
        bool menu = false;
        bool click = false;
    };
    std::map<std::string, std::vector<RegistryEntry>> button_registry;

    struct Snap {
        bool active = false;
        NodeRef node;
        XY T;
        bool is_node = false;
    } snap_cursor_to;

    struct ContextEntry {
        NodeRef node;
        XY cursor_pos;
        bool interrupt = false;
        bool valid = false;
    };
    struct {
        int layer = 1;
        std::map<int, ContextEntry> stack;
    } cursor_context;
    std::map<std::string, int> cardarea_context;

    struct {
        std::string last_type;
        bool dpad = false, pointer = true, touch = false, controller = false, mouse = true, axis_cursor = false;
    } HID;
    std::string GAMEPAD_CONSOLE;
    bool has_gamepad = false;

    bool is_cursor_down = false;
    bool frame_buttonpress = false;
    bool COYOTE_FOCUS = false;
    bool no_holdcap = false;
    double repress_timer = 0.3;
    bool has_repress_timer = false;
    double overlay_timer = 0;
    NodeRef finger_focus, finger_cursor;
    NodeRef text_input_hook;
    bool capslock = false;
    NodeRef screen_keyboard;
    bool has_L_cursor_queue = false;
    XY L_cursor_queue;
    double last_touch_time = -1;

    // gamepad state fed by the platform layer (love.joystick)
    struct {
        float leftx = 0, lefty = 0, rightx = 0, righty = 0, triggerleft = 0, triggerright = 0;
    } axes;
};

}  // namespace vl
