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
local raw = {}
local store = {}
local proxy = setmetatable({}, {
    __index = function(_, k) return store[k] end,
    __newindex = function(_, k, v)
        if k == 'P_CENTER_POOLS' and not raw.P_CENTERS then
            for name, val in pairs(store) do raw[name] = deep_copy(val) end
        end
        store[k] = v
    end,
})
store.save_progress = function() end
G = {SETTINGS = {profile = 1}, FILES = {}}
Game.init_item_prototypes(proxy)

write('prototypes', raw)
-- pool order as built by the game for an empty profile (keys only)
local pools = {}
for name, list in pairs(store.P_CENTER_POOLS) do
    pools[name] = {}
    for i, v in ipairs(list) do pools[name][i] = v.key end
end
write('pools_default', pools)

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
