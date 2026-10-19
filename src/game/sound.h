#pragma once
// functions/misc_functions.lua play_sound / modulate_sound and engine/sound_manager.lua. As in the
// original (G.F_SOUND_THREAD), requests go to a sound thread that owns the sources; the mixing
// itself happens in audio/mixer on the platform's audio thread.
namespace vl {

void sound_start();  // starts the sound thread and the audio output (G.SOUND_MANAGER)
void play_sound(const char *sound, float per = 1, float vol = 1);
void modulate_sound(double dt);
void sound_reset_states(int state);  // {type = 'reset_states'}
void sound_stop();                   // {type = 'stop'}

}  // namespace vl
