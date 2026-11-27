-- Amazone statt Dame: zieht wie Dame + Springer.
return {
  on_load = function(rules)
    rules.add_piece{ id = "amazone", name = "Amazone", symbol = "A", value = 1250,
                     moves_like = { "queen", "knight" }, promotion = true }
    rules.set_name("Amazonenschach")
    rules.set_start("rnbakbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBAKBNR w KQkq - 0 1")
  end,
}