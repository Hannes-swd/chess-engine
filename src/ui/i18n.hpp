#pragma once

// Sprache der Oberflaeche. Im Code stehen die deutschen Texte; tr() liefert bei Englisch die Uebersetzung.
// Unbekannte Texte (z. B. Namen, Coach-Erklaerungen, Eroeffnungsnamen) bleiben unveraendert.

#include <string>

namespace chess::ui {

enum class Language { German = 0, English = 1 };

void setLanguage(Language lang);
Language language();
inline bool english() { return language() == Language::English; }

// Fuer Literale: der Zeiger bleibt gueltig (zeigt auf das Literal oder die feste Uebersetzung).
const char* tr(const char* german);
// Fuer zusammengesetzte oder temporaere Texte.
std::string tr(const std::string& german);

}  // namespace chess::ui
