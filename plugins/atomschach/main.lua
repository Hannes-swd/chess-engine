-- Nach jedem Schlagzug: Explosion rund um das Zielfeld.
return {
  on_load = function(rules)
    rules.set_name(lang == "en" and "Atomic Chess" or "Atomschach")
  end,

  on_move = function(board, move)
    if not move.capture then return end
    local gone = 0
    for dx = -1, 1 do
      for dy = -1, 1 do
        local x, y = move.to_x + dx, move.to_y + dy
        if board:inside(x, y) then
          local id = board:get(x, y)
          if id and (id ~= "pawn" or (dx == 0 and dy == 0)) then
            board:clear(x, y)
            ui.mark(x, y, "red")   -- zeigt auf dem Brett, was explodiert ist
            gone = gone + 1
          end
        end
      end
    end
    if lang == "en" then
      ui.message("Explosion: " .. gone .. (gone == 1 and " piece" or " pieces") .. " removed")
    else
      ui.message("Explosion: " .. gone .. (gone == 1 and " Figur" or " Figuren") .. " vom Brett")
    end
  end,

  check_end = function(board)
    local white, black = false, false
    for _, p in ipairs(board:pieces()) do
      if p.id == "king" then
        if p.color == "white" then white = true else black = true end
      end
    end
    if not white then return "black", lang == "en" and "White's king exploded" or "Weißer König explodiert" end
    if not black then return "white", lang == "en" and "Black's king exploded" or "Schwarzer König explodiert" end
  end,
}