-- Eigene Siegbedingung über check_end.
local hill = { { 4, 4 }, { 5, 4 }, { 4, 5 }, { 5, 5 } }

return {
  check_end = function(board, info)
    for _, sq in ipairs(hill) do
      local id, color = board:get(sq[1], sq[2])
      if id == "king" then
        return color, "König auf dem Hügel"
      end
    end
  end,
}