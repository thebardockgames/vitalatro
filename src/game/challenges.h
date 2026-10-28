#pragma once
// Challenges (challenges.cpp): the run setup's Challenges tab and challenge list, the challenge rules
// applied by start_run, and the blind popup shared with the blind lists.
#include <string>
#include <vector>

#include "core/value.h"
#include "engine/sprite.h"

namespace vl {

// get_challenge_rule(_challenge, _type, _id): the rule's value, or nil
Value get_challenge_rule(const Value &challenge, const char *type, const char *id);
int get_challenge_int_from_id(const Value &id);
// the starting deck's prototypes {s, r, e, d, g}; for_run: erratic/no_faces apply, seals from deck.gold_seal
std::vector<TableRef> challenge_card_protos(const Value &challenge, bool for_run);
// create_UIBox_blind_popup(blind, discovered, vars)
TableRef create_UIBox_blind_popup(const Value &blind, bool discovered, const Value &vars = Value());
TableRef challenge_description(const Value &id, bool is_row);
void register_challenge_callbacks();

// an AnimatedSprite blind chip that shows its popup while hovered
class BlindChipSprite : public AnimatedSprite {
public:
    BlindChipSprite(float x, float y, float w, float h, Atlas *a, SpritePos p, const Value &blind);
    void hover() override;
    void stop_hover() override;
    Value blind;
    bool hovering = false;
    bool discovered = true;  // the collection shows undiscovered blinds with a hidden popup
};

}  // namespace vl
