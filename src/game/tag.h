#pragma once
// tag.lua. config/ability stay dynamic tables as in the original (game rules read them by name).
#include <string>

#include "core/object.h"
#include "engine/sprite.h"

namespace vl {

class Tag;

// The tag's sprite in a UI (Tag:generate_UI overrides hover/stop_hover on the instance).
class TagSprite : public Sprite {
public:
    TagSprite(float x, float y, float w, float h, Atlas *a, SpritePos p, Tag *tag) : Sprite(x, y, w, h, a, p), tag(tag) {}
    void hover() override;
    void stop_hover() override;
    Ref<Tag> tag;
    bool hovering = false;
    TableRef ability_UIBox_table;  // Tag:get_uibox_table()
};

class Tag : public Object {
public:
    // Tag(_tag, for_collection, _blind_type)
    Tag(const Value &key, bool for_collection = false, const Value &blind_type = Value());

    void nope();
    void yep(const Value &message, const Value &colour, std::function<bool()> func);
    void set_ability();
    // Tag:apply_to_run(_context): the result table, or nil
    Value apply_to_run(const TableRef &context);
    TableRef save() const;
    void load(const TableRef &t);
    void juice_up(float scale = 0.4f, const float *rot = nullptr);
    // returns {n = C, ...} and the sprite
    TableRef generate_UI(float size = 0.8f, TagSprite **sprite_out = nullptr);
    // Tag:get_uibox_table(tag_sprite): the description into the sprite's ability_UIBox_table
    void get_uibox_table(TagSprite *sprite = nullptr);
    void remove_from_game();
    void remove();

    std::string key, name;
    TableRef config, ability;
    Value pos;
    double tally = 0;
    bool triggered = false, hide_ability = false;
    int ID = 0;
    Ref<TagSprite> tag_sprite;
    Ref<Node> HUD_tag;
};
using TagRef = Ref<Tag>;

}  // namespace vl
