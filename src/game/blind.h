#pragma once
// blind.lua. What the HUD and the blind choice UIs read by name (loc_name, chip_text,
// loc_debuff_lines, the blind prototype in config.blind) stays in the dynamic table `vars`, which
// is what they get as ref_table; the rest is typed.
#include <string>

#include "engine/node.h"
#include "engine/sprite.h"

namespace vl {

class Card;

class Blind : public Moveable {
public:
    Blind(float x, float y, float w, float h);

    void change_colour(const Value &blind_col = Value());
    void set_text();
    void set_blind(const Value &blind, bool reset = false, bool silent = false);
    void alert_debuff(bool first);
    std::string get_loc_debuff_text() const;
    const char *get_type() const;  // "Small", "Big", "Boss" or nullptr
    void wiggle();
    void juice_up(float a, const float *b) override;
    void juice_up_blind(float a = 0.2f, float b = 0.2f);
    void hover() override;
    void stop_hover() override;
    void draw() override;
    void move(double dt) override;
    void change_dim(float w, float h);
    void align();
    void drawn_to_hand();
    void debuff_card(Card *card, bool from_blind = false);
    void defeat(bool silent = false);
    void disable();
    // true when the blind did something on play (the caller juices the HUD)
    bool press_play();
    // modify_hand(cards, poker_hands, text, mult, hand_chips) -> mult, hand_chips, modded
    void modify_hand(const std::vector<Card *> &cards, const TableRef &poker_hands, const std::string &text, double &mult, double &hand_chips,
                     bool &modded);
    bool debuff_hand(const std::vector<Card *> &cards, const TableRef &hand, const std::string &handname, bool check);
    void remove() override;

    AnimatedSprite *sprite() const { return dynamic_cast<AnimatedSprite *>(children.get("animatedSprite")); }

    // {loc_name, chip_text, loc_debuff_text, loc_debuff_lines = {'', ''}} for the UI; config = {blind = prototype}
    TableRef vars, config;
    std::string name;
    bool has_name = false;  // self.name ~= nil ('' is a name too)
    double dollars = 0, mult = 0, chips = 0;
    int sound_pings = 0;
    TableRef debuff;
    bool disabled = false, boss = false, blind_set = false, prepped = false, hovering = false, block_play = false;
    Value triggered;
    Value discards_sub, hands_sub;
    TableRef hands;     // The Eye: hands played this round
    Value only_hand;    // The Mouth: false, or the one hand allowed
    TableRef colour, dark_colour;
    float ambient_tilt = 0.3f;
    float hover_tilt = 0;
    float shadow_height = 0;
    struct {
        float mx = 0, my = 0, amt = 0;
    } tilt_var;
};
using BlindRef = Ref<Blind>;

}  // namespace vl
