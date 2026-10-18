// Pass 3: expands the per-block colours to screen pixels with the original block grid.
extern number pixel_size;
extern vec2 blocks;

vec4 effect(vec4 colour, Image texture, vec2 texture_coords, vec2 screen_coords)
{
    vec2 b = floor(screen_coords * (1. / pixel_size));
    return Texel(texture, (b + 0.5) / blocks);
}
