#pragma once
// card_character.lua: Jimbo, the card that talks (game over, win screen, tutorial).
#include "engine/node.h"

namespace vl {

class Card_Character : public Moveable {
public:
    // Card_Character{x, y, w, h, center}: args.center defaults to G.P_CENTERS.j_joker
    explicit Card_Character(const TableRef &args);

    void move(double dt) override;
    void hard_set_VT();
    void align();
    // add_button(button, func, colour, update_func, snap_to, yoff)
    void add_button(const Value &button, const Value &func, const Value &colour = Value(), const Value &update_func = Value(), bool snap_to = false,
                    const Value &yoff = Value());
    // add_speech_bubble(text_key, align, loc_vars): loc_vars.quip picks the quips (else the tutorial)
    void add_speech_bubble(const std::string &text_key, const char *align = nullptr, const TableRef &loc_vars = nullptr);
    void remove_button();
    void remove_speech_bubble();
    void say_stuff(int n, bool not_first = false);
    void draw() override;
    void remove() override;

    TableRef config;
    bool talking = false;
    int last_said = 0;
};

// G.UIDEF.speech_bubble(text_key, loc_vars)
TableRef speech_bubble(const std::string &text_key, const TableRef &loc_vars);
TableRef create_UIBox_character_button(const TableRef &args);

}  // namespace vl
