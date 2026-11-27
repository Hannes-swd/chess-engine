-- Riesenbrett: 10 x 10 Felder, zwei neue Figuren, keine Rochade.
return {
  on_load = function(rules)
    rules.add_piece{ id = "drache", name = "Drache", symbol = "D", value = 850,
                     moves_like = { "rook", "knight" }, promotion = true }
    rules.add_piece{ id = "erzbischof", name = "Erzbischof", symbol = "E", value = 700,
                     moves_like = { "bishop", "knight" }, promotion = true }
    rules.set_name("Riesenbrett 10×10")
    rules.set_start("rnbdqkebnr/pppppppppp/10/10/10/10/10/10/PPPPPPPPPP/RNBDQKEBNR w - - 0 1")
  end,
}