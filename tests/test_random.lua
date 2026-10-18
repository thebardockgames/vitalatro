local seeds = {0, 1, 0.5, 123.456, 0.123456789, 1e-9, 98765.4321}
for _, s in ipairs(seeds) do
    math.randomseed(s)
    for i = 1, 4 do print(string.format('%.17g', math.random())) end
    print(string.format('%.17g %.17g', math.random(52), math.random(3, 9)))
end
local function pseudohash(str)
    local num = 1
    for i=#str, 1, -1 do num = ((1.1239285023/num)*string.byte(str, i)*math.pi + math.pi*i)%1 end
    return num
end
for _, s in ipairs({'Joker1', 'shop_pack', 'VITABENCH', 'cashout1', 'tarot2rerollsAAAAAAA'}) do
    local h = pseudohash(s)
    print(string.format('%.17g %.17g', h, tonumber(string.format('%.13f', h*1.72431234 + 2.134453429141))))
end
