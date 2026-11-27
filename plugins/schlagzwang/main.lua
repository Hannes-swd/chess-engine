-- Zugfilter: Gibt es Schlagzüge, bleiben nur diese übrig.
return {
  filter_moves = function(board, moves)
    local captures = {}
    for _, m in ipairs(moves) do
      if m.capture then captures[#captures + 1] = m end
    end
    if #captures > 0 then return captures end
    return moves
  end,
}