-- Riesenbrett: 10 x 10 Felder, zwei neue Figuren, keine Rochade.
return {
  on_load = function(rules)
    rules.add_piece{ id = "drache", name = lang == "en" and "Dragon" or "Drache", symbol = "D", value = 850,
                     moves_like = { "rook", "knight" }, promotion = true,
                     image_white = "figuren/drache_weiss.png", image_black = "figuren/drache_schwarz.png" }
    rules.add_piece{ id = "erzbischof", name = lang == "en" and "Archbishop" or "Erzbischof", symbol = "E", value = 700,
                     moves_like = { "bishop", "knight" }, promotion = true,
                     image_white = "figuren/erzbischof_weiss.png", image_black = "figuren/erzbischof_schwarz.png" }
    rules.set_name(lang == "en" and "Giant Board 10×10" or "Riesenbrett 10×10")
    rules.set_start("rnbdqkebnr/pppppppppp/10/10/10/10/10/10/PPPPPPPPPP/RNBDQKEBNR w - - 0 1")
  end,
}