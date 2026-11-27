-- Amazone statt Dame: zieht wie Dame + Springer.
return {
  on_load = function(rules)
    rules.add_piece{ id = "amazone", name = lang == "en" and "Amazon" or "Amazone", symbol = "A", value = 1250,
                     moves_like = { "queen", "knight" }, promotion = true,
                     image_white = "figuren/amazone_weiss.png", image_black = "figuren/amazone_schwarz.png" }
    rules.set_name(lang == "en" and "Amazon Chess" or "Amazonenschach")
    rules.set_start("rnbakbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBAKBNR w KQkq - 0 1")
  end,
}