-- Extracts Balatro's data tables from the user's copy of the game, at build time, with LuaJIT:
--   luajit extract_data.lua <balatro_dir> <out_dir>
-- Writes STR_PACK-format files ("return {...}") that the engine parses with parse_lua_literal.
-- Keys are written in LuaJIT's pairs() order so iteration order is preserved where it matters.
-- Nothing here runs on the Vita; the game logic itself is reimplemented in C++.
local game_dir, out_dir = arg[1], arg[2]
package.path = game_dir .. '/?.lua;' .. package.path

-- ------------------------------------------------------------------ serializer
local function q(s) return string.format('%q', s) end
local function key_str(k)
    if type(k) == 'string' then return '[' .. q(k) .. ']' end
    return '[' .. tostring(k) .. ']'
end
local function num_str(n)
    if n ~= n then return '0/0' end
    if n == math.huge then return 'math.huge' end
    if n == -math.huge then return '-math.huge' end
    return string.format('%.17g', n)
end

local seen
local function pack(v, path)
    local t = type(v)
    if t == 'number' then return num_str(v) end
    if t == 'string' then return q(v) end
    if t == 'boolean' then return v and 'true' or 'false' end
    if t == 'table' then
        if seen[v] then error('shared/cyclic table at ' .. path .. ' (also at ' .. seen[v] .. ')') end
        seen[v] = path
        local parts = {}
        for k, val in pairs(v) do
            parts[#parts + 1] = key_str(k) .. '=' .. pack(val, path .. '.' .. tostring(k))
        end
        seen[v] = nil
        return '{' .. table.concat(parts, ',') .. '}'
    end
    if t == 'function' then return q('<function>') end
    error('cannot pack ' .. t .. ' at ' .. path)
end

local function write(name, value)
    seen = {}
    local f = assert(io.open(out_dir .. '/' .. name .. '.lua', 'w'))
    f:write('return ', pack(value, name), '\n')
    f:close()
    print('wrote ' .. name)
end

local function deep_copy(o, memo)
    if type(o) ~= 'table' then return o end
    memo = memo or {}
    if memo[o] then return memo[o] end
    local c = {}
    memo[o] = c
    for k, v in pairs(o) do c[deep_copy(k, memo)] = deep_copy(v, memo) end
    return c
end

-- ------------------------------------------------------------------ stubs
function HEX(hex)
    if #hex <= 6 then hex = hex .. 'FF' end
    local _, _, r, g, b, a = hex:find('(%x%x)(%x%x)(%x%x)(%x%x)')
    return {tonumber(r, 16) / 255, tonumber(g, 16) / 255, tonumber(b, 16) / 255, tonumber(a, 16) / 255}
end
-- localize() calls inside prototypes are resolved by the engine at runtime
function localize(args) return {__localize = args} end
function copy_table(t) return deep_copy(t) end

love = {
    system = {getOS = function() return 'Windows' end},
    mod_filesystem = {getInfo = function() return nil end, createDirectory = function() end},
    filesystem = {getInfo = function() return nil end},
}
dofile(game_dir .. '/engine/object.lua')

-- ------------------------------------------------------------------ prototypes
Game = Object:extend()
dofile(game_dir .. '/game.lua')

-- Run init_item_prototypes on a proxy; snapshot the raw literals at the moment the function
-- starts building P_CENTER_POOLS (right after the literal definitions, before profile data).
-- The literals are serialized right there (not copied first): a copy would be a different hash
-- layout, and pairs() order must stay the one the game's own tables have.
local raw_parts
local store = {}
local proxy = setmetatable({}, {
    __index = function(_, k) return store[k] end,
    __newindex = function(_, k, v)
        if k == 'P_CENTER_POOLS' and not raw_parts then
            raw_parts = {}
            for name, val in pairs(store) do
                seen = {}
                raw_parts[#raw_parts + 1] = key_str(name) .. '=' .. pack(val, name)
            end
        end
        store[k] = v
    end,
})
store.save_progress = function() end
G = {SETTINGS = {profile = 1}, FILES = {}}
Game.init_item_prototypes(proxy)

do
    local f = assert(io.open(out_dir .. '/prototypes.lua', 'w'))
    f:write('return {', table.concat(raw_parts, ','), '}\n')
    f:close()
    print('wrote prototypes')
end
-- pool order as built by the game for an empty profile (keys only)
local pools = {}
for name, list in pairs(store.P_CENTER_POOLS) do
    pools[name] = {}
    for i, v in ipairs(list) do pools[name][i] = v.key end
end
write('pools_default', pools)

-- ------------------------------------------------------------------ the fresh G.GAME table
-- Game:init_game_object() only reads P_BLINDS and get_starting_params(), so its result is data.
dofile(game_dir .. '/functions/misc_functions.lua')
G.P_BLINDS = store.P_BLINDS
write('game_object', Game.init_game_object(G))

-- ------------------------------------------------------------------ globals (G.C etc.)
dofile(game_dir .. '/globals.lua')  -- defines Game:set_globals and runs G = Game()
write('colours', G.C)
write('globals', {
    STATES = G.STATES, STAGES = G.STAGES, UIT = G.UIT, handlist = G.handlist,
    COLLABS = G.COLLABS, SETTINGS = G.SETTINGS,
})

-- ------------------------------------------------------------------ challenges
G.CHALLENGES = nil
dofile(game_dir .. '/challenges.lua')
write('challenges', G.CHALLENGES)

-- ------------------------------------------------------------------ localization
for _, lang in ipairs({'en-us', 'de', 'es_419', 'es_ES', 'fr', 'id', 'it', 'ja', 'ko', 'nl', 'pl', 'pt_BR', 'ru',
                       'zh_CN', 'zh_TW'}) do
    local t = dofile(game_dir .. '/localization/' .. lang .. '.lua')
    write('loc_' .. lang, t)
end

-- ------------------------------------------------------------------ static UI trees
-- Screens whose definitions are fixed literals (the credits) are built here with the original
-- G.UIDEF functions and stored as data. Colours stay references into G.C ({__C = "UI.TEXT_LIGHT"})
-- so the engine resolves them to its own (possibly animated) colour tables.
local colour_path = {}
local function index_colours(t, prefix)
    for k, v in pairs(t) do
        if type(v) == 'table' then
            if type(v[1]) == 'number' and #v == 4 then
                colour_path[v] = colour_path[v] or (prefix .. k)
            else
                index_colours(v, prefix .. k .. '.')
            end
        end
    end
end
index_colours(G.C, '')

local function pack_ui(v, path)
    local t = type(v)
    if t == 'table' then
        if colour_path[v] then return '{["__C"]=' .. q(colour_path[v]) .. '}' end
        local parts = {}
        for k, val in pairs(v) do
            parts[#parts + 1] = key_str(k) .. '=' .. pack_ui(val, path .. '.' .. tostring(k))
        end
        return '{' .. table.concat(parts, ',') .. '}'
    end
    return pack(v, path)
end
local function write_ui(name, value)
    seen = {}
    local f = assert(io.open(out_dir .. '/' .. name .. '.lua', 'w'))
    f:write('return ', pack_ui(value, name), '\n')
    f:close()
    print('wrote ' .. name)
end

dofile(game_dir .. '/functions/UI_definitions.lua')
-- builders the engine provides itself: kept as markers
function create_UIBox_generic_options(args) return args end
function create_tabs(args) return args end
function UIBox_button(args) return {__ui = 'UIBox_button', args = args} end
function create_option_cycle(args) return {__ui = 'create_option_cycle', args = args} end
function Moveable() return {__object = 'Moveable'} end
G.F_EXTERNAL_LINKS = false  -- the Vita has no browser
G.F_BASIC_CREDITS = nil
G.F_BABYNAMES = nil

local credits = G.UIDEF.credits()
local tabs = {}
for i, tab in ipairs(credits.contents[1].nodes[1].tabs) do
    local def = tab.tab_definition_function()
    -- the Collabs tab is live (an option cycle swapping a UIBox): only its data is kept
    tabs[i] = {label = tab.label, chosen = tab.chosen, definition = tab.label ~= 'Collabs' and def or nil}
end
write_ui('ui_credits', {tabs = tabs, collab_credits = G.collab_credits})
