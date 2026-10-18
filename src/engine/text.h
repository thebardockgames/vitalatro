#pragma once
// engine/text.lua: DynaText, per-letter animated text (pop in/out, float, bump, pulse, quiver).
// The config is the dynamic table passed by the original UI code; it is read live every frame
// because game code changes fields such as pop_in, pulse or quiver after creation.
#include <string>
#include <vector>

#include "engine/font.h"
#include "engine/node.h"

namespace vl {

struct FontDef;

class DynaText : public Moveable {
public:
    explicit DynaText(TableRef config);

    void update(double dt) override;
    void update_text(bool first_pass = false);
    void pop_out(double pop_out_timer = 1);
    void pop_in(double pop_in_timer = 0);
    void align_letters();
    void set_quiver(double amt = 0.7);
    void pulse(double amt = 0.2);
    void draw() override;

    struct Letter {
        Ref<Text> letter;
        std::string ch;
        float scale = 1, r = 0;
        XY offset;
        XY dims;
        float pop_in = 1;
        Value prefix, suffix, colour;
    };
    struct String {
        std::string string;
        bool has_string = false;
        std::vector<Letter> letters;
        float W = 0, H = 0, W_offset = 0, H_offset = 0;
    };

    bool shadow = false;
    float scale = 1;
    float pop_in_rate = 3, bump_rate = 2.666f, bump_amount = 1;
    FontDef *font = nullptr;
    std::string string;  // self.string: the focused string's text
    XY text_offset;
    Value colours;
    double created_time = 0;
    bool silent = false;
    bool start_pop_in = false, reset_pop_in = false;
    std::vector<String> strings;
    int focused_string = 1;
    bool pop_cycle = false;
    double pop_delay = 0, pop_out_time = 0;
    bool has_pop_delay = false;

private:
    bool moveable_ready_ = false;  // `if self.T` before Moveable.init has run
};

}  // namespace vl
