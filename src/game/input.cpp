// main.lua: love.gamepadpressed/released, love.joystickaxis, love.mousepressed/released/moved
// (touch). The Vita pad is reported as a PlayStation gamepad with LOVE's button names.
#include "game/input.h"

#include <cmath>

#include "core/time.h"
#include "engine/controller.h"
#include "game/g.h"

namespace vl {

namespace {
platform::Input prev{};
bool was_touching = false;

struct Map {
    uint32_t bit;
    const char *button;
};
// Cross confirms (LOVE 'a'), Circle goes back ('b'). The Vita has no triggers: L/R are shoulders.
const Map kButtons[] = {{platform::BTN_UP, "dpup"},      {platform::BTN_DOWN, "dpdown"},      {platform::BTN_LEFT, "dpleft"},
                        {platform::BTN_RIGHT, "dpright"}, {platform::BTN_CROSS, "a"},          {platform::BTN_CIRCLE, "b"},
                        {platform::BTN_SQUARE, "x"},      {platform::BTN_TRIANGLE, "y"},       {platform::BTN_L, "leftshoulder"},
                        {platform::BTN_R, "rightshoulder"}, {platform::BTN_START, "start"},     {platform::BTN_SELECT, "back"}};

// LOVE reports stick axes after the controller mapping's deadzone; values are -1..1
float axis(float v) { return std::fabs(v) < 0.05f ? 0.f : v; }
}  // namespace

void input_frame(const platform::Input &in) {
    Controller &C = *G.CONTROLLER;
    // love.gamepadpressed / love.gamepadreleased
    for (const Map &m : kButtons) {
        bool now = in.buttons & m.bit, before = prev.buttons & m.bit;
        if (now && !before) {
            C.set_gamepad();
            C.set_HID_flags("button", m.button);
            C.button_press(m.button);
        } else if (!now && before) {
            C.set_gamepad();
            C.set_HID_flags("button", m.button);
            C.button_release(m.button);
        }
    }
    // love.joystickaxis: any axis beyond 0.2 switches to axis input
    float ax[4] = {axis(in.lx), axis(in.ly), axis(in.rx), axis(in.ry)};
    float pv[4] = {axis(prev.lx), axis(prev.ly), axis(prev.rx), axis(prev.ry)};
    for (int i = 0; i < 4; i++)
        if (ax[i] != pv[i] && std::fabs(ax[i]) > 0.2f) {
            C.set_gamepad();
            C.set_HID_flags("axis");
        }
    C.axes.leftx = ax[0];
    C.axes.lefty = ax[1];
    C.axes.rightx = ax[2];
    C.axes.righty = ax[3];

    // touch arrives as mouse events with the touch flag (love.event: touchpressed + mousepressed)
    if (in.touching) {
        C.mouse_position = {in.touch_x, in.touch_y};
        C.last_touch_time = G.TIMERS.UPTIME;
        if (!was_touching) {
            C.set_HID_flags("touch");
            C.queue_L_cursor_press(in.touch_x, in.touch_y);
        } else if (in.touch_x != prev.touch_x || in.touch_y != prev.touch_y) {
            C.set_HID_flags(C.last_touch_time > G.TIMERS.UPTIME - 0.2 ? "touch" : "mouse");
        }
    } else if (was_touching) {
        C.L_cursor_release(&prev.touch_x, &prev.touch_y);
    }
    was_touching = in.touching;
    prev = in;
}

}  // namespace vl
