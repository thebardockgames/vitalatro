#pragma once
// The tutorial: state_events.lua G.FUNCS.tutorial_controller / tutorial_part, common_events.lua
// tutorial_info, button_callbacks.lua tut_next / skip_tutorial_section / start_tutorial, and the
// G.OVERLAY_TUTORIAL parts of game.lua (its removal in delete_run, its highlights in Game:draw).
#include <vector>

#include "engine/ui.h"

namespace vl {

class Card_Character;

// G.OVERLAY_TUTORIAL: the dimming UIBox with the Skip button, plus the fields the original sets on it
// (button_listen stays an ad hoc var: UIElement:click reads it)
class TutorialOverlay : public UIBox {
public:
    using UIBox::UIBox;
    int step = 1;
    bool step_complete = false, skip_steps = false;
    Ref<Card_Character> Jimbo;
    std::vector<NodeRef> highlights;  // G.OVERLAY_TUTORIAL.highlights
    bool has_highlights = false;      // (highlights ~= nil)
};

TutorialOverlay *tutorial_overlay();  // G.OVERLAY_TUTORIAL
void tutorial_controller();           // G.FUNCS.tutorial_controller
void tut_next();                      // G.FUNCS.tut_next
// G.OVERLAY_TUTORIAL.button_listen == button
bool tutorial_listening(const char *button);
// Game:delete_run: Jimbo, content and the overlay are removed
void remove_tutorial_overlay();
// Game:draw: the overlay and its highlights, drawn over the dimmed screen
void draw_tutorial_overlay();
// drag_target's text boxes join the highlights while they exist
void tutorial_highlight_add(Node *n);
void tutorial_highlight_remove(Node *n);
void register_tutorial_callbacks();

}  // namespace vl
