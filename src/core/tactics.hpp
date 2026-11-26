#pragma once

#include "core/position.hpp"

#include <string>

namespace chess {

// Summe der Materialwerte einer Farbe (ohne Koenige), aus der PieceRegistry.
int materialOf(const Position& pos, Color c);

// Materialbilanz aus Sicht von c (eigenes minus gegnerisches Material).
int materialBalance(const Position& pos, Color c);

// Verliert die Figur auf `sq` Material? Ja, wenn der Gegner sie schlagen kann und sie entweder
// ungedeckt ist oder der billigste Angreifer weniger wert ist als sie. Ein Koenig zaehlt als
// Angreifer nur gegen ungedeckte Figuren (er darf nicht ins Schach schlagen).
bool isEnPrise(const Position& pos, Square sq);

// "einen Springer", "die Dame", "einen Bauern" – fuer Saetze wie "gewinnt einen Springer".
std::string pieceAccusative(const PieceType& t);
// "Der Springer", "Die Dame" – Satzanfang.
std::string pieceNominative(const PieceType& t);

}  // namespace chess
