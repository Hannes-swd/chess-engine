-- Grashüpfer: zieht entlang der Damenlinien bis zur ersten Figur und springt genau ein Feld dahinter.
-- Die Zugregel ist eine Lua-Funktion – so lassen sich beliebige Figuren bauen.
local dirs = { {1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1} }

local function hopper(board, x, y)
  local out = {}
  for _, d in ipairs(dirs) do
    local cx, cy = x + d[1], y + d[2]
    while board:inside(cx, cy) and not board:get(cx, cy) do
      cx, cy = cx + d[1], cy + d[2]
    end
    local lx, ly = cx + d[1], cy + d[2]
    if board:inside(cx, cy) and board:inside(lx, ly) then
      out[#out + 1] = { lx, ly }
    end
  end
  return out
end

return {
  on_load = function(rules)
    rules.add_piece{ id = "grashuepfer", name = "Grashüpfer", symbol = "G", value = 250, moves = hopper }
    rules.set_name("Grashüpferschach")
    rules.set_start("rnbqkbnr/pppppppp/2g2g2/8/8/2G2G2/PPPPPPPP/RNBQKBNR w KQkq - 0 1")
  end,
}