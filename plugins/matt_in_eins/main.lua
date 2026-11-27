-- Matt in 1: Wer am Zug ist, setzt matt. In jeder Stellung gibt es genau EINEN Zug, der matt setzt –
-- andere Schachgebote gibt es oft auch, aber nur einer beendet die Partie. Du hast einen Versuch.
--
-- Die 237 Stellungen stammen aus echten (schwachen) Engine-Partien und sind gemischt:
-- Dame, Turm, Läufer, Springer, Bauer, Grundreihenmatt, Abzugsschach, Umwandlung, ersticktes Matt.
-- Jede wurde geprüft: genau ein Mattzug, Weiß steht nicht im Schach. Die Hälfte der Aufgaben wird gespiegelt,
-- dann zieht Schwarz. Mit "player_moves_first" in plugin.json spielst du immer die Seite am Zug.

local kinds = lang == "en" and {
  queen = "queen mate", rook = "rook mate", bishop = "bishop mate", knight = "knight mate",
  pawn = "pawn mate", Grundreihe = "back-rank mate", ["Abzug"] = "discovered mate",
  ["Umwandlung"] = "mate with promotion", erstickt = "smothered mate",
} or {
  queen = "Damenmatt", rook = "Turmmatt", bishop = "Läufermatt", knight = "Springermatt",
  pawn = "Bauernmatt", Grundreihe = "Grundreihenmatt", ["Abzug"] = "Matt durch Abzug",
  ["Umwandlung"] = "Matt mit Umwandlung", erstickt = "Ersticktes Matt",
}

local puzzles = {
  { "r1bk1b1r/1pR1pBpp/1B1p4/p5N1/8/P7/5PPP/6K1 w - - 2 23", "Rxb7#", "Abzug" },
  { "6rk/1p6/p6B/n6Q/P7/2P3Pb/1P2Bn1P/6K1 w - - 0 30", "Bf8#", "Abzug" },
  { "2kr4/2b5/2Q1p3/P2pPp1p/1B1P4/7P/5PP1/2R4K w - - 5 40", "Qxc7#", "Abzug" },
  { "4R3/R4B1k/8/6P1/2K4P/5P2/1P6/8 w - - 1 66", "Bh5#", "Abzug" },
  { "5r1k/r4ppn/n2p4/p3p1PQ/P1B1P3/1p1P4/1PP1K1P1/R6R w - - 0 22", "Qxh7#", "Abzug" },
  { "R7/2RR1pk1/6p1/4P1Pp/1p6/7Q/7P/3B2K1 w - - 1 48", "Rxf7#", "Abzug" },
  { "7k/n4Qp1/8/5p1p/7P/P1B5/5PPK/8 w - - 1 40", "Qxg7#", "Abzug" },
  { "5r1k/r5pn/n2p4/p3ppPQ/P1BPP3/1p6/1PP1K1P1/R6R w - f6 0 23", "Qxh7#", "Abzug" },
  { "k1B3Q1/8/1P2N3/p7/P6K/8/8/8 w - - 1 86", "Ba6#", "Abzug" },
  { "6k1/7p/4Rn1P/p2B4/5r2/6p1/8/6K1 w - - 2 50", "Re8#", "Abzug" },
  { "6k1/2p3p1/1p4Qp/5r2/4B3/1P4RP/1B6/R5K1 w - - 7 34", "Qxg7#", "Abzug" },
  { "7k/3Q2b1/8/5p2/7p/PPB5/4KP2/6R1 w - - 8 56", "Qxg7#", "Abzug" },
  { "r1bk1NQ1/pp1pq3/8/1p3p1B/8/1P6/1PK2PPP/4r3 w - - 1 26", "Ne6#", "Abzug" },
  { "2bn4/1p2N2k/4pb1B/r4p1Q/p1P5/P6P/1p3PP1/1R4K1 w - - 2 31", "Bf8#", "Abzug" },
  { "r3kb1r/Q2q2pp/4p1n1/1BPp4/p2P2b1/1P2BN2/P4PPP/R3K2R w KQk - 4 23", "Qxd7#", "Abzug" },
  { "8/7R/8/3B3N/2P5/1K4pk/8/8 w - - 3 68", "Nf6#", "Abzug" },
  { "r1bbk2r/pp1p1pp1/3B3p/2pn4/2p1N1P1/P1P1Q3/1q2PPBP/2R1K2R w Kkq - 4 20", "Nf6#", "Abzug" },
  { "2Q5/kq1R4/1r4pp/ppB5/5p1P/PP6/2P2P2/1K1R4 w - - 6 38", "Qxb7#", "Abzug" },
  { "8/1R3Qbk/5pp1/p7/2B1P2p/2B5/5PPP/5K2 w - - 0 36", "Qxg7#", "Abzug" },
  { "4BQ2/2R3rk/5p2/p6p/P3N3/1p4PP/1P3P2/4K3 w - - 1 39", "Qxg7#", "Abzug" },
  { "2k4B/7R/3pR3/1p6/3P2B1/2p4K/1P1N4/8 w - - 1 46", "Re8#", "Abzug" },
  { "R2Qb1k1/4B1p1/4p1Pp/8/4p2P/4P3/8/2R1K3 w - - 1 56", "Qxe8#", "Abzug" },
  { "4R1Nk/8/7P/2P3K1/8/8/8/8 w - - 1 87", "Nf6#", "Abzug" },
  { "2qrkb1r/pp3ppp/5p2/8/1nb1B2N/6P1/PPP1QP1P/R1B1K2R w KQk - 8 16", "Bc6#", "Abzug" },
  { "8/kp3R1p/6p1/pQ1P4/2P5/P1p5/1P4PP/R5K1 w - - 2 26", "Qxb7#", "Abzug" },
  { "1r5k/ppp3b1/4bBQ1/4p3/3pP3/P2P3P/2P2PK1/R6R w - - 10 32", "Qxg7#", "Abzug" },
  { "1k6/1p6/pQ4P1/P7/1R6/8/KP2r2P/8 w - - 9 56", "Qxb7#", "Abzug" },
  { "8/8/1Kp4P/p7/8/4R1pk/5Q2/1N6 w - - 0 63", "Qxg3#", "Abzug" },
  { "7k/1p3Qb1/1P6/2p5/p1P5/8/1B6/4K3 w - - 7 53", "Qxg7#", "Abzug" },
  { "2n4k/5Qp1/8/5p1p/7P/2B5/P4PPK/8 w - - 1 39", "Qxg7#", "Abzug" },
  { "6k1/8/6K1/8/6P1/p3R3/P7/8 w - - 15 70", "Re8#", "Grundreihe" },
  { "3k4/5R2/3K4/6N1/7p/5P1p/7P/8 w - - 10 66", "Rf8#", "Grundreihe" },
  { "5k2/8/4R1BP/7P/p7/P1p5/P1P2N2/5K2 w - - 7 63", "Re8#", "Grundreihe" },
  { "1k6/5R2/8/P7/7R/7P/7K/8 w - - 1 87", "Rh8#", "Grundreihe" },
  { "7k/3R4/5p2/pp5p/5B2/6R1/B4K2/8 w - - 1 60", "Rg8#", "Grundreihe" },
  { "r3k3/p1p2p2/1p4p1/2Bp1B2/5P2/1P2P3/P6R/R3K3 w Q - 2 31", "Rh8#", "Grundreihe" },
  { "2B4k/5Q2/2r2R1p/1n6/4p1p1/1p6/7P/3RK3 w - - 0 41", "Rd8#", "Grundreihe" },
  { "1k6/p1p2K2/1p4r1/8/2P3p1/5B2/P2R1P2/8 w - - 0 36", "Rd8#", "Grundreihe" },
  { "1k1bR3/p2R4/1pp5/5pp1/1n6/1N6/1P6/5K2 w - - 10 40", "Rexd8#", "Grundreihe" },
  { "6k1/5pp1/p5r1/7Q/3P1P2/P6p/4R1P1/2B4K w - - 2 44", "Re8#", "Grundreihe" },
  { "2k5/n4R2/1p1p2Q1/pR1P4/P1P3Pp/7P/8/2K5 w - - 17 64", "Qg8#", "Grundreihe" },
  { "5k2/8/1R3K2/4p3/4P1P1/5P2/8/7r w - - 8 72", "Rb8#", "Grundreihe" },
  { "3k4/2N3Q1/1R6/8/6K1/1p6/8/8 w - - 13 88", "Rb8#", "Grundreihe" },
  { "4k3/7Q/1p6/8/P1p5/P1P5/3B4/6RK w - - 7 46", "Rg8#", "Grundreihe" },
  { "6k1/4Q3/1p4pp/1Pp5/2P1P3/p5P1/7P/5RK1 w - - 1 46", "Rf8#", "Grundreihe" },
  { "3k4/1R6/2P3N1/p7/P4B1P/8/2P1K3/8 w - - 1 58", "Rb8#", "Grundreihe" },
  { "7k/3R4/3B4/8/2p2P1P/2K5/1P5P/R7 w - - 3 50", "Ra8#", "Grundreihe" },
  { "rn2r1k1/ppp2p2/6p1/5bP1/3B4/P1PR3R/1PP5/2K2B2 w - - 7 21", "Rh8#", "Grundreihe" },
  { "3k4/4p3/2Q3R1/p3B3/1p6/8/3K4/8 w - - 8 66", "Rg8#", "Grundreihe" },
  { "Q3q2k/6pp/6r1/pB2p3/3pP3/2N4b/PPP3PK/5R2 w - - 8 28", "Qxe8#", "Grundreihe" },
  { "k7/6p1/3N3p/1N5P/3p1pK1/1p3P2/8/2R5 w - - 4 66", "Rc8#", "Grundreihe" },
  { "2k5/5p1R/3K4/p2B2P1/P7/1P6/8/8 w - - 5 77", "Rh8#", "Grundreihe" },
  { "1k6/2p2pR1/Q7/p1P4p/7P/3P4/PP2PP2/R3K3 w Q - 0 30", "Rg8#", "Grundreihe" },
  { "1B3b1k/5Qp1/6QP/p7/P4P2/8/6K1/8 w - - 3 60", "Qxf8#", "Grundreihe" },
  { "6k1/1R6/8/7p/1p3p1B/5P1P/6P1/R5K1 w - - 3 37", "Ra8#", "Grundreihe" },
  { "4k3/8/3Q4/2r3p1/1P6/7P/B4PP1/3R2K1 w - - 1 42", "Qd8#", "Grundreihe" },
  { "1k6/3Q4/p7/1pP5/8/1p5P/PP3P1P/4RRK1 w - - 9 47", "Re8#", "Grundreihe" },
  { "2b2kr1/2p2p2/1pP2p2/1Nb4p/r6P/3Q2R1/1K3PP1/8 w - - 0 36", "Qd8#", "Grundreihe" },
  { "2N4k/4Q3/8/8/5Rp1/6P1/6K1/8 w - - 5 77", "Rf8#", "Grundreihe" },
  { "7k/pp2Q3/5p2/6p1/6BP/K7/5PP1/3R3R w - - 2 43", "Rd8#", "Grundreihe" },
  { "8/4PB2/1p3kP1/7R/Pb2R3/5P2/2K5/q7 w - - 7 59", "e8=N#", "Umwandlung" },
  { "8/1pn3Pk/4P3/6Q1/8/8/K7/8 w - - 0 45", "g8=Q#", "Umwandlung" },
  { "2N1k3/5RP1/8/1P2pp2/4P2P/2P5/3K1P2/8 w - - 1 40", "g8=Q#", "Umwandlung" },
  { "2k5/P1B3R1/5P2/8/1p6/4p2p/7K/8 w - - 3 66", "a8=Q#", "Umwandlung" },
  { "3k3r/1P2ppbp/6p1/3pN2b/2pP4/2P1P2P/1P1N1PP1/2B1K2R w K - 0 21", "b8=Q#", "Umwandlung" },
  { "4rk1r/pb3Pp1/2P5/8/P1p2P1P/b1q4P/2P2n1K/2B1R3 w - - 0 33", "fxe8=Q#", "Umwandlung" },
  { "2k4B/PR6/7p/8/8/5K2/5P2/8 w - - 1 76", "a8=Q#", "Umwandlung" },
  { "4k3/1PP1N3/3B1RP1/1b6/8/3p4/3K4/8 w - - 1 69", "c8=Q#", "Umwandlung" },
  { "8/3R1P1k/6p1/3p1p1p/3B1P1P/2N2NPK/Q7/8 w - - 1 54", "f8=Q#", "Umwandlung" },
  { "7k/1P2KP2/8/4N3/p3B2p/P7/5R2/8 w - - 1 59", "f8=Q#", "Umwandlung" },
  { "8/r4Pk1/4R3/5Qp1/6P1/B7/5P1p/7K w - - 1 58", "f8=Q#", "Umwandlung" },
  { "1k6/2RP4/p7/2N3pP/5p2/N6P/P2KP3/R7 w - - 1 38", "d8=Q#", "Umwandlung" },
  { "6r1/NkP1R2p/3B4/2K3p1/5p2/5P2/8/8 w - - 11 68", "c8=Q#", "Umwandlung" },
  { "8/6Pk/6R1/1P3p2/1BP4p/5P1P/6K1/8 w - - 1 54", "g8=Q#", "Umwandlung" },
  { "8/kP6/8/2N1B3/2K5/5p2/r7/8 w - - 1 72", "b8=Q#", "Umwandlung" },
  { "5Q2/p5Pk/8/2r1p2p/2n2PPP/8/1B5N/7K w - - 3 53", "g8=Q#", "Umwandlung" },
  { "5k2/1N4RP/8/8/8/7K/1p6/1B6 w - - 1 90", "h8=Q#", "Umwandlung" },
  { "2B5/1PB5/k7/7p/2R4P/8/8/4K3 w - - 3 75", "b8=Q#", "Umwandlung" },
  { "5Q2/p1pqr1Pk/1p6/2bP4/2P3n1/2N4P/PPP3P1/R3R2K w - - 3 30", "g8=Q#", "Umwandlung" },
  { "8/4PkN1/5P2/1P3K2/2P3P1/8/7p/7R w - - 1 79", "e8=Q#", "Umwandlung" },
  { "2B5/kPB5/2R5/7p/7P/8/8/4K3 w - - 1 74", "b8=Q#", "Umwandlung" },
  { "8/3R2Pk/p3B2p/Pp6/7P/2bP2n1/2P2BK1/5R2 w - - 1 39", "g8=Q#", "Umwandlung" },
  { "R7/1kP5/8/7p/P2B3P/6N1/1K2P3/8 w - - 0 57", "c8=Q#", "Umwandlung" },
  { "5br1/p1p2p1p/1p4p1/4p3/1Ppk4/2N4P/1P1B1PP1/1R2KB1R w K - 2 21", "Be3#", "bishop" },
  { "8/1R6/8/5R2/k7/p2BK3/2p5/8 w - - 4 66", "Bxc2#", "bishop" },
  { "r1bk4/p1pp2pp/1p4B1/2N5/2P5/P4P1P/1P3P2/R1B2K1R w - - 2 22", "Bg5#", "bishop" },
  { "3R1rk1/6p1/1p3p2/5P2/1P5Q/P7/5PK1/3R1B2 w - - 1 47", "Bc4#", "bishop" },
  { "1r5k/8/p5Qp/6p1/1BP3P1/P4P1P/8/R4K2 w - - 7 47", "Bc3#", "bishop" },
  { "8/8/p1n3Pk/1pB2Q2/8/2P1P3/P1P4K/R5R1 w - - 5 41", "Bf8#", "bishop" },
  { "r5k1/p7/4b2Q/6p1/2B5/PP5P/1P3PP1/RN2R1K1 w - - 15 33", "Bxe6#", "bishop" },
  { "rk5r/pp3pp1/3q1n2/2Q4p/2P2B2/1P3N2/P4PPP/4R1K1 w - - 3 22", "Bxd6#", "bishop" },
  { "8/3Q4/5p1k/2B1pP2/4P1P1/p7/8/6K1 w - - 6 81", "Be3#", "bishop" },
  { "5Q2/8/4p1k1/7p/1P4pP/2p3P1/3p2B1/5NK1 w - - 4 50", "Be4#", "bishop" },
  { "2Q5/8/1k6/p3B3/P1P4P/1P1p4/5PK1/3R4 w - - 13 76", "Bd4#", "bishop" },
  { "4Q3/1p6/3Q4/k3Pp2/3p4/7p/7K/2B5 w - - 0 47", "Bd2#", "bishop" },
  { "8/5R2/P6k/3b1p1B/1B3N1P/3p3P/P4K1N/8 w - - 1 46", "Bf8#", "bishop" },
  { "4kb2/7R/p4B2/3B4/6P1/1P1K2P1/8/8 w - - 4 45", "Bc6#", "bishop" },
  { "r1k5/ppB5/6p1/4K2p/8/6P1/1r5P/2R2B1R w - - 1 36", "Bh3#", "bishop" },
  { "2k5/8/3Q2p1/pB5p/7P/p1P3PK/P4P2/q7 w - - 7 56", "Ba6#", "bishop" },
  { "k1r3nr/p1P5/2qbp1p1/Pp3p2/3P4/4P1P1/1P1Q2B1/R1R3K1 w - - 1 28", "Bxc6#", "bishop" },
  { "3k4/8/4Q1p1/1p4Np/5P2/1PB3P1/7K/8 w - - 2 44", "Ba5#", "bishop" },
  { "8/5ppp/1pR5/pN1p2k1/3N2bR/1PB3K1/8/8 w - - 1 49", "Bd2#", "bishop" },
  { "7k/1R6/4B1p1/p4p1p/4P3/3K4/P2BN1PP/7R w - - 1 32", "Bc3#", "bishop" },
  { "5Q2/7k/8/6p1/6B1/6P1/3p4/6K1 w - - 2 60", "Bf5#", "bishop" },
  { "1k6/8/Q7/p1PP4/p7/2B4p/3K4/8 w - - 6 67", "Be5#", "bishop" },
  { "4k3/8/5Q2/7p/3P1P1P/3BPR2/8/1K4N1 w - - 3 53", "Bb5#", "bishop" },
  { "6R1/8/2Q2N2/k7/8/pp6/1B6/4K3 w - - 2 67", "Bc3#", "bishop" },
  { "1Q6/4k3/8/4NP2/P1PP4/8/4K3/R1B5 w - - 5 57", "Bg5#", "bishop" },
  { "4k3/8/3QP3/1p5P/8/8/4B3/6K1 w - - 0 66", "Bxb5#", "bishop" },
  { "k2B4/3R1p2/8/1pp1Q3/6P1/2p4n/1qB4p/7K w - - 7 56", "Be4#", "bishop" },
  { "7k/5Q2/1N3pp1/p1PN2P1/7p/2B4P/1P3P2/4K2R w K - 0 40", "Bxf6#", "bishop" },
  { "5k1r/2N1p2p/p4pp1/1p3B2/8/1Q3N2/PPnBKPPP/3R3R w - - 0 23", "Bh6#", "bishop" },
  { "2r5/3b1p2/1p1p3Q/pB2pk2/8/2b2P2/2P3KP/3R1R2 w - - 1 31", "Bxd7#", "bishop" },
  { "2rqkb1r/pp1nnp1b/8/1P1p3p/P1Np2p1/1Q1BP3/5PPP/RNB1K2R w KQk - 0 14", "Nd6#", "erstickt" },
  { "r5rk/ppp1N1pp/3p2q1/4pp2/3nP3/8/PPPP2PR/R1B3K1 w - - 0 18", "Nxg6#", "erstickt" },
  { "r2qkb1r/1b1pnppp/2p1p3/4P3/1p2N3/3Q1N2/P1P2PPP/R1B2RK1 w kq - 0 13", "Nd6#", "erstickt" },
  { "r2qkb1r/pp1bnpp1/2n1p2p/1N1pP3/B2N3P/8/PPPB1PP1/R2QK2R w KQkq - 6 13", "Nd6#", "erstickt" },
  { "r5rk/1pp3pp/7N/p7/P2p1n2/3P4/1PPK1PPP/R4R2 w - - 0 21", "Nf7#", "erstickt" },
  { "8/8/P2PNK1p/7k/2R5/1p6/1B3P2/8 w - - 4 61", "Ng7#", "knight" },
  { "8/pb6/6p1/3pp1Bk/1P4NP/5P2/p6K/4R3 w - - 0 47", "Nf6#", "knight" },
  { "8/R4p2/8/1p4Pk/1P2NP1p/2P5/2B2K2/6R1 w - - 1 50", "Nf6#", "knight" },
  { "8/2Q2P2/k3N3/8/1K6/8/5n2/8 w - - 5 85", "Nc5#", "knight" },
  { "3k4/4R3/8/5B2/1P1N1K2/P1p5/6p1/4b1R1 w - - 2 55", "Nc6#", "knight" },
  { "N1b1k2r/pp4Qp/6n1/2pp1pB1/4P3/6PB/PPP2P1P/R3K2R w KQ - 0 18", "Nc7#", "knight" },
  { "8/7p/p7/k3p3/1R2P1pP/6P1/N7/N3K3 w - - 4 45", "Nb3#", "knight" },
  { "5Q2/7k/4P1p1/p2N4/8/5p1P/7K/8 w - - 6 65", "Nf6#", "knight" },
  { "1b6/k5p1/2Q4p/2p1pp2/2p4P/P1N1PP2/1P6/3K3R w - - 2 33", "Nb5#", "knight" },
  { "2Q5/k3N3/p7/P3R1P1/8/6P1/6K1/8 w - - 1 66", "Nc6#", "knight" },
  { "8/8/4N3/8/R7/1k5K/7P/2Q5 w - - 17 74", "Nc5#", "knight" },
  { "r1bq1b1Q/ppp1nk1p/5Np1/8/1np5/5N2/PPPP1PPP/R1B1K2R w KQ - 4 10", "Ng5#", "knight" },
  { "r2r4/2p3pp/4pp1k/p4qNP/3PQ1P1/bpPRP3/2P1K3/7R w - - 1 28", "Nf7#", "knight" },
  { "r2qkr2/4bp1p/ppb5/4Q3/P2nN3/3n3P/1PPP1PP1/R1BK2NR w q - 0 16", "Nf6#", "knight" },
  { "r1b2r1k/p1ppR3/1p1b3p/5pp1/3P3N/PBB3P1/1P3P1P/3R2K1 w - g6 0 21", "Ng6#", "knight" },
  { "3k2rr/2p4p/p1ppQ3/P5N1/5b2/5P1R/5P1P/7K w - - 2 30", "Nf7#", "knight" },
  { "1R6/8/4Q3/N7/3k4/8/2K5/8 w - - 5 84", "Nb3#", "knight" },
  { "1brqr3/1p4p1/4pnkp/3b1pN1/Qp3PP1/4BN1P/PP6/3R1RK1 w - - 0 25", "Nh4#", "knight" },
  { "1n6/1p6/kP1R4/6p1/p3N3/2Q3p1/8/1R4K1 w - - 8 60", "Nc5#", "knight" },
  { "r1bk1bnr/1ppp2pp/p7/1q2N3/4R3/8/PPPPQPPP/RNB3K1 w - - 1 10", "Nf7#", "knight" },
  { "r6Q/2pbkp2/1p1pq3/p4r2/6np/2P1N3/PP4PP/3RR1K1 w - - 2 27", "Nxf5#", "knight" },
  { "8/1p1P2p1/7k/6Np/1P6/5PpP/R1Q5/7K w - - 4 45", "Nf7#", "knight" },
  { "7k/5Q2/p7/P1P1N3/4P3/8/5K1p/8 w - - 1 58", "Ng6#", "knight" },
  { "r5R1/pppb1p2/3pk3/2bNn3/4P1P1/P7/1PP1NPP1/R3KB2 w Q - 3 18", "Nef4#", "knight" },
  { "8/P2Q4/1P3pkp/P7/5PP1/8/4K1N1/8 w - - 3 47", "Nh4#", "knight" },
  { "r1b2b1r/1p2pp1p/1n2kn2/pNp1B1p1/P1p3P1/5N2/1P3P1P/3RKB1R w K - 2 15", "Nc7#", "knight" },
  { "1rb1k2r/pp3ppp/3Qpn2/1N6/8/n1N5/5PPP/5RK1 w k - 0 19", "Nc7#", "knight" },
  { "2kr3r/1R3p2/B7/P4N2/6P1/B1P1p2p/3P3P/5K2 w - - 2 35", "Ne7#", "knight" },
  { "1rn3r1/1p2bp1p/1Pk1bp2/4p3/p1N1P3/P3B3/2P2PPP/2KR1B1R w - - 1 23", "Na5#", "knight" },
  { "Nnb1kr2/p2p3p/1pp5/2q3B1/4P3/3R1QP1/PbP1B2P/5K1R w - - 1 21", "Nc7#", "knight" },
  { "1r5Q/pbq1kp2/2p1prB1/4P3/8/7P/PPP3P1/R1BR3K w - - 5 34", "exf6#", "pawn" },
  { "3rr3/1p2np1p/pnq1b1pk/2P5/4N1PN/P1Q1P2P/4BP2/R3K2R w KQ - 2 22", "g5#", "pawn" },
  { "8/2R3Q1/p5p1/3Np1kp/1P2P3/5PP1/7P/1K6 w - - 1 57", "h4#", "pawn" },
  { "2N4r/2Q2Bpk/pp5p/2p3P1/8/8/nPPK3P/5R1R w - - 1 28", "g6#", "pawn" },
  { "rn1q3r/p3b1p1/3p1nkp/1p2pbN1/2p1P3/2NP1Q1P/PPP3P1/R1B1R1K1 w - - 0 15", "exf5#", "pawn" },
  { "7R/P7/5Pp1/P5k1/4P1P1/3p3P/8/3B1RK1 w - - 3 50", "h4#", "pawn" },
  { "5Nk1/7R/1p3P1B/6p1/7p/n6P/2pN4/2K2R2 w - - 2 54", "f7#", "pawn" },
  { "8/8/3R4/1pk1Pppp/2p4P/3PP3/1P1B1PP1/6K1 w - g6 0 30", "d4#", "pawn" },
  { "8/2Q5/p5p1/1k6/6P1/P3P2R/1PBB2K1/8 w - - 5 50", "a4#", "pawn" },
  { "4rk2/p3p2R/1pP1BpP1/8/2Pp4/Q2P4/P2N1K2/8 w - - 3 34", "g7#", "pawn" },
  { "7r/6p1/8/5Q1p/2p1B2k/3P4/1PP2PPP/RNb3K1 w - - 1 31", "g3#", "pawn" },
  { "1N6/8/1kB2p2/1N6/1PP2P2/8/p5P1/2K5 w - - 0 61", "c5#", "pawn" },
  { "5k2/8/6P1/1P5p/2BBR2P/8/1K6/5n2 w - - 0 66", "g7#", "pawn" },
  { "8/p4R1p/3k4/PpR5/1P2PKb1/2n5/B1P4P/8 w - - 0 38", "e5#", "pawn" },
  { "8/6Q1/3p4/2pP3k/2P1p1pp/PPB1P2P/8/K2R4 w - - 0 48", "hxg4#", "pawn" },
  { "7Q/BR6/6k1/5p2/5P1P/8/1K4p1/8 w - - 4 81", "h5#", "pawn" },
  { "8/8/2p2Q2/1P5k/8/2B3PP/2P2b1K/8 w - - 3 60", "g4#", "pawn" },
  { "6R1/4B3/4R2p/1P2P2k/8/2N5/6Pp/7K w - - 7 49", "g4#", "pawn" },
  { "4q1Q1/Q7/2pk3p/4p3/1PP1P3/3Kn2P/7P/R6R w - - 1 35", "c5#", "pawn" },
  { "5K1k/6np/4pP2/8/8/8/2B5/8 w - - 0 65", "fxg7#", "pawn" },
  { "8/6R1/7k/6np/P1Q4P/2B5/8/2K5 w - - 1 51", "hxg5#", "pawn" },
  { "8/3R4/3p1kp1/2pPbp1p/1pP3P1/1P3P1K/3Q3P/7r w - - 3 39", "g5#", "pawn" },
  { "8/5Q2/6pk/7p/PP4PP/8/4KP2/8 w - - 5 72", "g5#", "pawn" },
  { "8/8/8/2p1KBk1/2P5/5P2/7R/8 w - - 12 76", "f4#", "pawn" },
  { "R6R/8/5Pp1/P5k1/4P1P1/7P/3p4/3B1RK1 w - - 0 51", "h4#", "pawn" },
  { "2Q5/k2K4/8/PP5p/2B3pP/2N3P1/8/8 w - - 9 76", "b6#", "pawn" },
  { "5R1N/1kp5/2b5/P6p/3BP3/6P1/1P4BP/R5K1 w - - 3 37", "a6#", "pawn" },
  { "6R1/p4p2/1n2pN1k/2K1P1pp/7P/P2B2P1/4b2r/8 w - - 5 54", "hxg5#", "pawn" },
  { "8/1Q3p2/R7/p1q1pk2/P7/3pP2P/3B1P2/K7 w - - 14 53", "e4#", "pawn" },
  { "r3B2Q/8/1p6/2kbR1K1/p1p2B2/PpP5/1P6/8 w - - 1 77", "Qf8#", "queen" },
  { "1k1r3r/2R3pp/p7/2Qq1p2/P7/4P1P1/b4P1P/4R1K1 w - - 3 30", "Qa7#", "queen" },
  { "4Q3/7R/3p2p1/p4p2/3k4/3P2P1/PP3PB1/5RK1 w - - 1 32", "Qe3#", "queen" },
  { "8/7k/2p2Q2/7p/3B2b1/1PR5/7P/4K3 w - - 11 59", "Qg7#", "queen" },
  { "6Q1/2k5/4Q3/8/1P6/1K6/8/8 w - - 3 76", "Qgc8#", "queen" },
  { "8/Q6p/p3R3/N5rk/1p5P/PPP2P2/5K2/8 w - - 5 64", "Qxh7#", "queen" },
  { "4k3/1pR5/p3pR2/4Bq2/PP6/4P2P/5PP1/3Q2K1 w - - 5 34", "Qd7#", "queen" },
  { "3k4/1Q6/p1P5/5p2/r7/8/1P6/1K6 w - - 1 69", "Qd7#", "queen" },
  { "2R5/8/k7/1N6/P2Q4/8/6K1/8 w - - 5 59", "Qa7#", "queen" },
  { "3N2kr/8/2n5/1p3Qp1/p2pP3/P2P2BP/1P1b2P1/2R4K w - - 3 43", "Qf7#", "queen" },
  { "r2q1r2/ppp1k1p1/2np4/2bnp1Np/2B1P1Q1/8/PPPP1PPP/R1B2RK1 w - h6 0 11", "Qe6#", "queen" },
  { "2Q5/7k/5ppp/8/P1B1P3/6K1/8/6R1 w - - 2 42", "Qg8#", "queen" },
  { "7k/4R3/8/pp4Q1/1B1P2P1/PP1P1P2/7P/6K1 w - a6 0 45", "Qg7#", "queen" },
  { "4R3/7k/7P/P1p3Q1/N1P2p2/P4P2/3P1P2/6K1 w - - 0 40", "Qg7#", "queen" },
  { "1R6/8/8/8/3Qp3/r7/k3KP2/7q w - - 35 73", "Qb2#", "queen" },
  { "1Q4Q1/8/2p2k2/2P5/8/6N1/8/2K5 w - - 3 57", "Qd6#", "queen" },
  { "5Q2/1R6/8/3P3P/8/7K/k7/8 w - - 1 73", "Qa8#", "queen" },
  { "r3r1B1/p2PP1B1/1p4k1/1Pp4b/P4p2/4P3/7R/Q3K3 w - - 6 37", "Qf6#", "queen" },
  { "1B2k3/6Q1/4r3/1Pp2pp1/2Q4p/7P/1P1R4/1K6 w - - 6 52", "Qxe6#", "queen" },
  { "8/1R6/8/8/p4K2/P4P2/3Q4/k6r w - - 7 86", "Qb2#", "queen" },
  { "R1QR4/8/8/1p6/pP5k/P4K2/8/8 w - - 1 73", "Qg4#", "queen" },
  { "k7/5Q2/1Rn5/3p4/2B5/1P4P1/N4P1P/4K2R w K - 4 33", "Qb7#", "queen" },
  { "6k1/1R6/7Q/3K1p1p/7P/8/5r2/8 w - - 10 67", "Qg7#", "queen" },
  { "r7/1p1N4/4p3/pP5Q/1k5P/3Q4/2p1KP2/3R4 w - - 2 40", "Qg4#", "queen" },
  { "3QN3/5pp1/1p5p/p1k5/P6P/1Pn1pP2/7K/3R4 w - - 1 40", "Qd6#", "queen" },
  { "8/8/2p3P1/2P1k2p/2bp3P/7Q/1B3R2/6K1 w - - 2 89", "Qf5#", "queen" },
  { "4Q3/8/3Q4/kp2Pp2/3p1B2/7p/7K/8 w - b6 0 48", "Qa8#", "queen" },
  { "N4Q2/5R2/6k1/4p2p/1P6/PNp4P/2P3P1/3K4 w - - 3 37", "Qg7#", "queen" },
  { "7Q/Q7/4p1k1/3ppp2/8/5P2/4N3/3K4 w - - 2 52", "Qag7#", "queen" },
  { "3Q4/1k6/8/P3p3/1p2P3/7K/3B1Q2/R3N3 w - - 1 58", "Qfb6#", "queen" },
  { "8/1p1R3p/p1n2b1k/5B2/PB3K2/1pP1r1P1/7r/7R w - - 17 36", "Rxh7#", "rook" },
  { "2K5/1Q6/4R3/8/k7/8/8/8 w - - 17 84", "Ra6#", "rook" },
  { "4R3/4PQ2/p5pk/P2p4/3P2nP/2B5/6B1/6K1 w - - 3 50", "Rh8#", "rook" },
  { "5rk1/2R4R/6pp/2p5/2P2pPK/8/P7/5r2 w - - 4 46", "Rcg7#", "rook" },
  { "8/1b2R3/4P1pk/R2P2Np/pP3K1P/6P1/8/8 w - - 1 47", "Rh7#", "rook" },
  { "7k/8/4p3/P7/4P3/5R2/3BN2P/4K1R1 w - - 3 35", "Rh3#", "rook" },
  { "Q7/5k2/2p3p1/1P1pK1P1/6r1/5P2/3Q1P1r/R7 w - - 9 42", "Ra7#", "rook" },
  { "4Q3/2p3k1/2P5/5pP1/p7/P3RN2/6KP/R7 w - - 1 42", "Re7#", "rook" },
  { "4R3/p2R4/5kp1/3p4/5pBP/1P2P3/5PK1/5R2 w - - 7 40", "Re6#", "rook" },
  { "R7/2R5/1p6/8/1k6/8/R5K1/8 w - - 3 66", "Rb2#", "rook" },
  { "1R4Q1/8/P6k/6pp/3p4/5P2/3N4/6K1 w - - 0 51", "Rb6#", "rook" },
  { "R7/4p1kp/1r2n1pN/4K1P1/4P3/8/4P3/8 w - - 7 56", "Rg8#", "rook" },
  { "8/7B/R7/4k3/6Rp/1n2N3/5KP1/8 w - - 5 44", "Re4#", "rook" },
  { "4r3/3p3R/1p2kp2/8/1B2PPPp/pP2K3/P1r2n2/3R4 w - - 1 44", "Rd6#", "rook" },
  { "6Q1/8/7k/1P6/2N3pb/2P5/2K2p2/7R w - - 6 61", "Rxh4#", "rook" },
  { "7k/5K2/R7/7p/7P/6P1/8/8 w - - 3 78", "Rh6#", "rook" },
  { "8/7R/6k1/3K1p2/5P2/7R/8/8 w - - 5 71", "R3h6#", "rook" },
  { "8/8/R7/2R3p1/1P2k2p/4B2P/3K2P1/8 w - - 5 60", "Re6#", "rook" },
  { "8/8/2R5/3p2p1/k2N2P1/P7/2K3P1/8 w - - 4 48", "Ra6#", "rook" },
  { "R3R3/8/8/3k1p2/5P2/2Q3P1/1B6/6K1 w - - 3 72", "Rad8#", "rook" },
  { "4R3/6k1/1p2Q3/6pp/1P6/5P2/4pK1P/R2b4 w - - 5 48", "Ra7#", "rook" },
  { "8/R7/6k1/7p/4RP1B/5PN1/7K/8 w - - 0 76", "Re6#", "rook" },
  { "7Q/8/8/4N1k1/7p/1p3R2/3K4/4R3 w - - 2 73", "Rg1#", "rook" },
  { "1k4r1/R4R2/1pp5/p3p3/P1P1P3/3Pb2B/1P2N3/1K6 w - - 4 46", "Rfb7#", "rook" },
  { "7R/3R2r1/3p2k1/2p1p1Nr/1p2Pp1P/1P1P4/2P1K3/8 w - - 4 56", "Rxd6#", "rook" },
  { "5R2/5Pk1/3p3p/2pP1Q2/P2r4/2B2P2/1p6/R5K1 w - - 1 55", "Rg8#", "rook" },
  { "r1k1r3/pp5p/3QP2q/8/8/4P1N1/PP4PP/5RK1 w - - 3 26", "Rc1#", "rook" },
  { "2R5/8/1p5P/5p2/p5p1/P5K1/7R/6k1 w - - 2 61", "Rc1#", "rook" },
  { "3r4/6R1/1p3k2/p3NP2/P4K2/7p/8/8 w - - 0 52", "Rf7#", "rook" },
  { "8/2R5/1R2p2k/4P3/5pKP/5Pp1/6P1/8 w - - 1 48", "Rxe6#", "rook" },
}

local current, solution, mover

-- Brett spiegeln und Farben tauschen: aus "Weiß setzt matt" wird "Schwarz setzt matt"
local function swapCase(s)
  return (s:gsub("%a", function(c)
    if c == c:upper() then return c:lower() else return c:upper() end
  end))
end

local function mirrorFen(fen)
  local boardPart, side, castle, ep, half, full = fen:match("^(%S+) (%S+) (%S+) (%S+) (%S+) (%S+)$")
  local rows = {}
  for row in boardPart:gmatch("[^/]+") do table.insert(rows, 1, swapCase(row)) end
  local c = "-"
  if castle ~= "-" then
    c = ""
    local swapped = swapCase(castle)
    for _, k in ipairs({ "K", "Q", "k", "q" }) do
      if swapped:find(k, 1, true) then c = c .. k end
    end
  end
  if ep ~= "-" then ep = ep:sub(1, 1) .. tostring(9 - tonumber(ep:sub(2, 2))) end
  return table.concat(rows, "/") .. " " .. (side == "w" and "b" or "w") .. " " .. c .. " " .. ep .. " " .. half .. " " .. full
end

-- Zug in Notation spiegeln: nur die Reihen ändern sich (Dg7# -> Dg2#)
local function mirrorSan(san)
  return (san:gsub("%d", function(d) return tostring(9 - tonumber(d)) end))
end

return {
  on_load = function(rules)
    current = puzzles[math.random(#puzzles)]
    if math.random(2) == 1 then
      rules.set_start(current[1])
      solution, mover = current[2], "white"
      rules.set_name(lang == "en" and "Mate in 1: White to move and mate" or "Matt in 1: Weiß zieht und setzt matt")
    else
      rules.set_start(mirrorFen(current[1]))
      solution, mover = mirrorSan(current[2]), "black"
      rules.set_name(lang == "en" and "Mate in 1: Black to move and mate" or "Matt in 1: Schwarz zieht und setzt matt")
    end
  end,

  -- Ein Versuch: Matt gefunden -> gewonnen, sonst ist die Aufgabe vorbei und die Lösung wird gezeigt
  check_end = function(board, info)
    if info.ply == 0 then return end
    local kind = kinds[current[3]] or (lang == "en" and "mate" or "Matt")
    if board:checkmate() then
      return mover, (lang == "en" and "Mate found: " or "Matt gefunden: ") .. solution .. " (" .. kind .. ")"
    end
    return (mover == "white") and "black" or "white", (lang == "en" and "No mate – the move was " or "Kein Matt – gesucht war ") .. solution .. " (" .. kind .. ")"
  end,
}