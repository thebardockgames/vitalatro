// tools/vita_pips.py in C++: the button pips as a Vita shows them. Balatro's PlayStation rows
// (1: PS3, 3: PS4) draw L1/L2 rounded on the right and R1/R2 on the left; the shoulder
// silhouettes are mirrored (their text is not) and row 3's OPTIONS/SHARE labels become START
// and SELECT in the atlas's own 3x6 pixel font.
#include "pips.h"

#include <cstring>
#include <map>
#include <vector>

namespace vlab {

namespace {

const int CELL = 32;
const uint8_t WHITE[4] = {255, 255, 255, 255};
const uint8_t FILL[4] = {79, 99, 103, 255};    // the shoulder buttons' face
const uint8_t BANNER[4] = {57, 71, 74, 255};   // the label banners

const std::map<char, std::vector<const char *>> GLYPHS = {
    {'S', {".##", "#..", ".#.", "..#", "..#", "##."}},
    {'T', {"###", ".#.", ".#.", ".#.", ".#.", ".#."}},
    {'A', {".#.", "#.#", "#.#", "###", "#.#", "#.#"}},
    {'R', {"##.", "#.#", "#.#", "##.", "#.#", "#.#"}},
    {'E', {"###", "#..", "##.", "#..", "#..", "###"}},
    {'L', {"#..", "#..", "#..", "#..", "#..", "###"}},
    {'C', {".##", "#..", "#..", "#..", "#..", ".##"}},
};

void put(Image &im, int x, int y, const uint8_t *c) { std::memcpy(im.at(x, y), c, 4); }

void mirror_shoulder(Image &im, int cx, int cy) {
    int x0 = cx * CELL, y0 = cy * CELL;
    uint8_t cell[CELL][CELL][4];
    std::vector<std::pair<int, int>> text;
    for (int y = 0; y < CELL; y++)
        for (int x = 0; x < CELL; x++) {
            std::memcpy(cell[y][x], im.at(x0 + x, y0 + y), 4);
            if (std::memcmp(cell[y][x], WHITE, 4) == 0) text.push_back({x, y});
        }
    for (auto &p : text) std::memcpy(cell[p.second][p.first], FILL, 4);
    for (int y = 0; y < CELL; y++)
        for (int x = 0; x < CELL; x++) put(im, x0 + x, y0 + y, cell[y][CELL - 1 - x]);  // FLIP_LEFT_RIGHT
    for (auto &p : text) put(im, x0 + p.first, y0 + p.second, WHITE);
}

void relabel(Image &im, int cx, int cy, const std::string &word) {
    int x0 = cx * CELL, y0 = cy * CELL;
    for (int y = 2; y < 8; y++)
        for (int x = 3; x < 29; x++)
            if (im.at(x0 + x, y0 + y)[3] == 255) put(im, x0 + x, y0 + y, BANNER);
    int w = 4 * int(word.size()) - 1;
    int left = (CELL - w) / 2;
    for (size_t i = 0; i < word.size(); i++) {
        const auto &g = GLYPHS.at(word[i]);
        for (int row = 0; row < 6; row++)
            for (int col = 0; col < 3; col++)
                if (g[row][col] == '#') put(im, x0 + left + 4 * int(i) + col, y0 + 2 + row, WHITE);
    }
}

}  // namespace

void vita_pips(Image &im) {
    for (int row : {1, 3})
        for (int col : {4, 5, 6, 7})  // L1, R1, L2, R2
            mirror_shoulder(im, col, row);
    relabel(im, 8, 3, "START");   // 'start' (OPTIONS on a PS4)
    relabel(im, 9, 3, "SELECT");  // 'back' (SHARE on a PS4)
}

}  // namespace vlab
