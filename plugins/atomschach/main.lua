-- Nach jedem Schlagzug: Explosion rund um das Zielfeld.
return {
  on_load = function(rules)
    rules.set_name("Atomschach")
  end,

  on_move = function(board, move)
    if not move.capture then return end
    for dx = -1, 1 do
      for dy = -1, 1 do
        local x, y = move.to_x + dx, move.to_y + dy
        if board:inside(x, y) then
          local id = board:get(x, y)
          if id and (id ~= "pawn" or (dx == 0 and dy == 0)) then
            board:clear(x, y)
          end
        end
      end
    end
  end,

  check_end = function(board)
    local white, black = false, false
    for _, p in ipairs(board:pieces()) do
      if p.id == "king" then
        if p.color == "white" then white = true else black = true end
      end
    end
    if not white then return "black", "Weißer König explodiert" end
    if not black then return "white", "Schwarzer König explodiert" end
  end,
}