#pragma once
// card.lua, cardarea.lua, back.lua. Card data that game rules read by name (ability, base,
// edition, config.center/card, params) stays in dynamic tables exactly as in the original; the
// engine-facing state (sprites, facing, area, rank...) is typed.
#include <string>
#include <vector>

#include "engine/node.h"
#include "engine/sprite.h"

namespace vl {

class CardArea;

class Card : public Moveable {
public:
    // Card(X, Y, W, H, card, center, params)
    Card(float x, float y, float w, float h, const Value &card, const Value &center, TableRef params = nullptr);

    void update_alert();
    void set_base(const Value &card, bool initial = false);
    void set_sprites(const Value &center, const Value &front);
    void set_ability(const Value &center, bool initial = false, bool delay_sprites = false);
    void set_cost();
    void set_edition(const Value &edition, bool immediate = false, bool silent = false);
    void set_seal(const Value &seal, bool silent = false, bool immediate = false);
    Value get_seal(bool bypass_debuff = false) const;
    void set_debuff(bool should_debuff);
    void change_suit(const std::string &new_suit);
    void set_eternal(bool eternal);
    void set_perishable(bool perishable);
    void set_rental(bool rental);
    void remove_UI();
    void add_to_deck(bool from_debuff = false);
    void remove_from_deck(bool from_debuff = false);
    double get_nominal(const char *mod = nullptr) const;
    int get_id() const;
    bool is_face(bool from_boss = false) const;
    bool is_suit(const Value &suit, bool bypass_debuff = false, bool flush_calc = false) const;
    void start_dissolve(TableRef colours = nullptr, bool silent = false, float time_fac = 1, bool no_juice = false);
    void shatter();
    // scoring (card.lua)
    double get_chip_bonus() const;
    double get_chip_mult();
    double get_chip_x_mult() const;
    double get_chip_h_mult() const;
    double get_chip_h_x_mult() const;
    Value get_edition();
    TableRef get_end_of_round_effect();
    double get_p_dollars();
    Value calculate_dollar_bonus() const;
    Value calculate_seal(const TableRef &context);
    void calculate_rental();
    void calculate_perishable();
    Value calculate_joker(const TableRef &context);
    void start_materialize(TableRef colours = nullptr, bool silent = false, float time_fac = 1);
    void set_card_area(CardArea *a);
    void remove_from_area();
    void align();
    void flip();
    void update(double dt) override;
    void hard_set_T(float x, float y, float w, float h) override;
    void hard_set_T() { hard_set_T(T->x, T->y, T->w, T->h); }
    void move(double dt) override;
    void hover() override;
    void stop_hover() override;
    bool simple_touch() override { return false; }
    void juice_up(float scale, const float *rot_amount) override { juice_up_card(scale, rot_amount, false); }
    void juice_up_card(float scale = -1, const float *rot_amount = nullptr, bool disable_rumble = false);
    void draw() override { draw_layer("both"); }
    void draw_layer(std::string_view layer);
    void release(Node *dragged) override;
    void highlight(bool is_highlighted);
    void sell_card();
    // shop / consumables / boosters
    void redeem();
    bool check_use();
    bool can_use_consumeable(bool any_state = false, bool skip_check = false);
    void use_consumeable(CardArea *area, Card *copier = nullptr);
    void open();
    void explode(TableRef colours = nullptr, double explode_time_fac = 1);
    // the description popup (card_ui.cpp)
    TableRef generate_UIBox_ability_table();
    TableRef align_h_popup();
    TableRef ability_UIBox_table;
    Value get_field(const Value &key) const override;
    bool can_sell_card() const;
    void click() override;
    void single_tap() override;
    bool has_single_tap() const override { return true; }
    bool can_long_press() override;
    bool can_hover_on_drag() override { return false; }
    void remove() override;
    bool is_card() const override { return true; }

    Sprite *sprite(const char *name) const { return dynamic_cast<Sprite *>(children.get(name)); }

    // dynamic data (read by name by game rules and UI)
    TableRef params, ability, base;
    TableRef edition;  // nil when the card has no edition
    Value seal, sticker, sticker_run;
    Value playing_card;  // index into G.playing_cards or nil
    Value label;
    Value sell_cost_label;

    std::string back = "selected_back";
    bool bypass_discovery_center = false, bypass_discovery_ui = false, bypass_lock = false;
    bool no_ui = false, no_shadow = false, greyed = false, vortex = false, pinned = false;
    double base_cost = 0, extra_cost = 0, cost = 0, sell_cost = 0;
    double unique_val = 0;
    int sort_id = 0;
    struct {
        double r = 0, x = 0, y = 0;
    } discard_pos;
    std::string facing = "front", sprite_facing = "front", flipping;
    CardArea *area = nullptr;
    bool highlighted = false;
    bool debuff = false;
    int rank = 0;  // 0 = nil
    bool added_to_deck = false;
    bool has_added_to_deck = false;
    float ambient_tilt = 0.2f;
    bool has_ambient_tilt = true;
    float send_to_shader[2] = {0, 0};
    bool removed = false;
    bool lucky_trigger = false, shattered = false, destroyed = false;
    bool getting_sliced = false, vampired = false;
    bool opening = false, shop_voucher = false, from_tag = false;
    bool temp_edition = false;  // a tag is about to give the shop joker an edition
    Moveable *jimbo = nullptr;   // the Card_Character this card is (hovering it shows no description)
    bool lua_discovered = false;  // card.discovered, set when discover_card() is handed the card itself
    std::vector<Ref<Card>> eligible_strength_jokers, eligible_editionless_jokers;  // (Card:update)
    Value back_overlay;
};
using CardRef = Ref<Card>;

class CardArea : public Moveable {
public:
    CardArea(float x, float y, float w, float h, TableRef config);

    void emplace(Card *card, const char *location = nullptr, bool stay_flipped = false);
    Card *remove_card(Card *card = nullptr, bool discarded_only = false);
    void change_size(int delta);
    bool can_highlight(Card *card) const;
    void add_to_highlighted(Card *card, bool silent = false);
    void parse_highlighted();
    void remove_from_highlighted(Card *card, bool force = false);
    void unhighlight_all();
    void set_ranks();
    void move(double dt) override;
    void update(double dt) override;
    void draw() override;
    void align_cards();
    void hard_set_T(float x, float y, float w, float h) override;
    void hard_set_cards();
    void sort(const char *method = nullptr);
    void shuffle(const std::string &seed = "shuffle");
    bool draw_card_from(CardArea *area, bool stay_flipped = false, bool discarded_only = false);
    void click() override;
    void release(Node *dragged) override {}
    void remove() override;

    std::string type() const;  // config.type

    std::vector<CardRef> cards;
    bool cards_nil = false;  // self.cards = nil after remove()
    std::vector<CardRef> highlighted;
    float card_w = 0;
    double shuffle_amt = 0;
    std::vector<std::string> draw_layers;
};
using CardAreaRef = Ref<CardArea>;

// back.lua
class Back : public Object {
public:
    explicit Back(const Value &selected_back = Value());
    Value get_name() const;
    void change_to(const Value &new_back);
    // Back:generate_UI(other, ui_scale, min_dims): the deck's description (run setup, collection)
    TableRef generate_UI(const Value &other = Value(), double ui_scale = 0.9, double min_dims = 0.7);
    void apply_to_run();
    // trigger_effect{context = 'final_scoring_step', chips, mult} (Plasma Deck)
    void trigger_effect_final_scoring(double &chips, double &mult);

    std::string name;
    TableRef effect;  // {center, text_UI, config}
    Value loc_name;
    TableRef pos;     // {x, y}
};

}  // namespace vl
