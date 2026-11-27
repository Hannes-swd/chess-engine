-- Jede neue Partie bekommt eine andere Grundreihe.
local function backRank()
  local r = {}
  r[({ 1, 3, 5, 7 })[math.random(4)]] = "b"   -- Läufer auf dunklem Feld
  r[({ 2, 4, 6, 8 })[math.random(4)]] = "b"   -- Läufer auf hellem Feld
  local function free()
    local t = {}
    for i = 1, 8 do if not r[i] then t[#t + 1] = i end end
    return t
  end
  local f = free(); r[f[math.random(#f)]] = "q"
  f = free(); r[f[math.random(#f)]] = "n"
  f = free(); r[f[math.random(#f)]] = "n"
  f = free(); r[f[1]] = "r"; r[f[2]] = "k"; r[f[3]] = "r"   -- König zwischen den Türmen
  return table.concat(r)
end

return {
  on_load = function(rules)
    local black = backRank()
    rules.set_name(lang == "en" and "Random Setup" or "Zufallsaufstellung")
    rules.set_start(black .. "/pppppppp/8/8/8/8/PPPPPPPP/" .. black:upper() .. " w - - 0 1")
  end,
}