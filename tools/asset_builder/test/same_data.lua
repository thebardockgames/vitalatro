-- luajit same_data.lua <dir_a> <dir_b> <name>...: compares data/*.lua files as Lua values
-- (key order ignored). Prints the first difference per file.
local a_dir, b_dir = arg[1], arg[2]
local function load(p) return assert(loadfile(p))() end
local function diff(x, y, path)
  if type(x) ~= type(y) then return path .. ': ' .. type(x) .. ' vs ' .. type(y) end
  if type(x) ~= 'table' then
    if x ~= y and not (x ~= x and y ~= y) then return path .. ': ' .. tostring(x) .. ' vs ' .. tostring(y) end
    return nil
  end
  for k, v in pairs(x) do
    local d = diff(v, y[k], path .. '.' .. tostring(k)); if d then return d end
  end
  for k in pairs(y) do if x[k] == nil then return path .. '.' .. tostring(k) .. ': missing in a' end end
end
local bad = 0
for i = 3, #arg do
  local d = diff(load(a_dir .. '/' .. arg[i]), load(b_dir .. '/' .. arg[i]), arg[i])
  if d then bad = bad + 1; print('DIFF ' .. d) else print('same ' .. arg[i]) end
end
os.exit(bad == 0 and 0 or 1)
