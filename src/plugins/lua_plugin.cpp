#include "plugins/lua_plugin.hpp"

#include "core/fen.hpp"
#include "core/notation.hpp"
#include "util/json.hpp"

#include <lua.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>

namespace chess::plugins {

namespace {

const char* const kBoardMeta = "chess.board";

std::string readText(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

const char* colorName(Color c) { return c == Color::White ? "white" : "black"; }

Color checkColor(lua_State* L, int idx) {
    static const char* const names[] = {"white", "black", nullptr};
    return luaL_checkoption(L, idx, nullptr, names) == 0 ? Color::White : Color::Black;
}

// Brett-Objekt fuer Lua. Nur waehrend des jeweiligen Aufrufs gueltig (danach Zeiger = nullptr).
struct BoardRef {
    Position* pos = nullptr;        // nur wenn das Plugin aendern darf
    const Position* cpos = nullptr;  // immer gesetzt, solange gueltig
};

BoardRef* boardArg(lua_State* L) {
    auto* b = static_cast<BoardRef*>(luaL_checkudata(L, 1, kBoardMeta));
    if (!b->cpos) luaL_error(L, "Das Brett ist nur während des Aufrufs gültig");
    return b;
}
const Position& readBoard(lua_State* L) { return *boardArg(L)->cpos; }
Position& writeBoard(lua_State* L) {
    BoardRef* b = boardArg(L);
    if (!b->pos) luaL_error(L, "Das Brett darf hier nur gelesen werden");
    return *b->pos;
}

// Lua zaehlt Felder ab 1: (1,1) = a1
Square checkSquare(lua_State* L, const Position& pos, int xIdx) {
    const int x = static_cast<int>(luaL_checkinteger(L, xIdx)) - 1;
    const int y = static_cast<int>(luaL_checkinteger(L, xIdx + 1)) - 1;
    if (!pos.contains(x, y)) luaL_error(L, "Feld (%d, %d) liegt nicht auf dem Brett", x + 1, y + 1);
    return pos.square(x, y);
}

void pushPiece(lua_State* L, const Position& pos, Square s) {
    const Piece& p = pos.at(s);
    lua_createtable(L, 0, 4);
    lua_pushstring(L, pos.registry().get(p.type).id.c_str());
    lua_setfield(L, -2, "id");
    lua_pushstring(L, colorName(p.color));
    lua_setfield(L, -2, "color");
    lua_pushinteger(L, pos.fileOf(s) + 1);
    lua_setfield(L, -2, "x");
    lua_pushinteger(L, pos.rankOf(s) + 1);
    lua_setfield(L, -2, "y");
}

// --- Brett-Methoden -----------------------------------------------------------

int bWidth(lua_State* L) {
    lua_pushinteger(L, readBoard(L).width());
    return 1;
}
int bHeight(lua_State* L) {
    lua_pushinteger(L, readBoard(L).height());
    return 1;
}
int bInside(lua_State* L) {
    const Position& pos = readBoard(L);
    lua_pushboolean(L, pos.contains(static_cast<int>(luaL_checkinteger(L, 2)) - 1,
                                    static_cast<int>(luaL_checkinteger(L, 3)) - 1));
    return 1;
}
int bGet(lua_State* L) {  // board:get(x, y) -> id, color   (nil, wenn leer)
    const Position& pos = readBoard(L);
    const Square s = checkSquare(L, pos, 2);
    const Piece& p = pos.at(s);
    if (p.empty()) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushstring(L, pos.registry().get(p.type).id.c_str());
    lua_pushstring(L, colorName(p.color));
    return 2;
}
int bSet(lua_State* L) {  // board:set(x, y, id, color)
    Position& pos = writeBoard(L);
    const Square s = checkSquare(L, pos, 2);
    const char* id = luaL_checkstring(L, 4);
    const Color c = checkColor(L, 5);
    const auto type = pos.registry().findById(id);
    if (!type) return luaL_error(L, "Unbekannte Figur '%s'", id);
    pos.place(s, Piece{*type, c}, lua_toboolean(L, 6) != 0);
    return 0;
}
int bClear(lua_State* L) {
    Position& pos = writeBoard(L);
    pos.clear(checkSquare(L, pos, 2));
    return 0;
}
int bResize(lua_State* L) {  // board:resize(w, h) – leert das Brett
    Position& pos = writeBoard(L);
    const int w = static_cast<int>(luaL_checkinteger(L, 2)), h = static_cast<int>(luaL_checkinteger(L, 3));
    if (w < 2 || h < 2 || w > 26 || h > 26) return luaL_error(L, "Brettgröße %dx%d nicht erlaubt (2 bis 26)", w, h);
    pos.resize(w, h);
    return 0;
}
int bSide(lua_State* L) {
    lua_pushstring(L, colorName(readBoard(L).sideToMove()));
    return 1;
}
int bSetSide(lua_State* L) {
    writeBoard(L).setSideToMove(checkColor(L, 2));
    return 0;
}
int bInCheck(lua_State* L) {
    lua_pushboolean(L, readBoard(L).inCheck(checkColor(L, 2)));
    return 1;
}
int bAttacked(lua_State* L) {  // board:attacked(x, y, by)
    const Position& pos = readBoard(L);
    lua_pushboolean(L, pos.isAttacked(checkSquare(L, pos, 2), checkColor(L, 4)));
    return 1;
}
int bPieces(lua_State* L) {  // board:pieces([color]) -> { {id, color, x, y}, ... }
    const Position& pos = readBoard(L);
    const bool filter = !lua_isnoneornil(L, 2);
    const Color c = filter ? checkColor(L, 2) : Color::White;
    lua_newtable(L);
    int n = 0;
    for (Square s = 0; s < pos.squareCount(); ++s) {
        const Piece& p = pos.at(s);
        if (p.empty() || (filter && p.color != c)) continue;
        pushPiece(L, pos, s);
        lua_rawseti(L, -2, ++n);
    }
    return 1;
}
int bFen(lua_State* L) {
    lua_pushstring(L, toFen(readBoard(L)).c_str());
    return 1;
}

void pushMove(lua_State* L, const Position& pos, const Move& m);  // weiter unten

// board:legal_moves() -> alle erlaubten Zuege der Seite am Zug (mit "san", z. B. "Qg7#")
int bLegalMoves(lua_State* L) {
    const Position& pos = readBoard(L);
    const std::vector<Move> moves = pos.legalMoves();
    lua_createtable(L, static_cast<int>(moves.size()), 0);
    Position tmp = pos;
    for (size_t i = 0; i < moves.size(); ++i) {
        pushMove(L, pos, moves[i]);
        lua_pushstring(L, toSan(tmp, moves[i]).c_str());
        lua_setfield(L, -2, "san");
        lua_rawseti(L, -2, static_cast<lua_Integer>(i + 1));
    }
    return 1;
}

// board:checkmate() -> steht die Seite am Zug matt?
int bCheckmate(lua_State* L) {
    const Position& pos = readBoard(L);
    lua_pushboolean(L, pos.inCheck(pos.sideToMove()) && pos.legalMoves().empty());
    return 1;
}

void registerBoardMeta(lua_State* L) {
    static const luaL_Reg methods[] = {
        {"width", bWidth},   {"height", bHeight},   {"inside", bInside},       {"get", bGet},
        {"set", bSet},       {"clear", bClear},     {"resize", bResize},       {"side", bSide},
        {"set_side", bSetSide}, {"in_check", bInCheck}, {"attacked", bAttacked}, {"pieces", bPieces},
        {"fen", bFen},       {"legal_moves", bLegalMoves}, {"checkmate", bCheckmate}, {nullptr, nullptr}};
    luaL_newmetatable(L, kBoardMeta);
    luaL_newlib(L, methods);
    lua_setfield(L, -2, "__index");
    lua_pop(L, 1);
}

BoardRef* pushBoard(lua_State* L, Position* writable, const Position& readable) {
    auto* b = static_cast<BoardRef*>(lua_newuserdatauv(L, sizeof(BoardRef), 0));
    b->pos = writable;
    b->cpos = &readable;
    luaL_setmetatable(L, kBoardMeta);
    return b;
}

void pushMove(lua_State* L, const Position& pos, const Move& m) {
    lua_createtable(L, 0, 6);
    lua_pushinteger(L, pos.fileOf(m.from) + 1);
    lua_setfield(L, -2, "from_x");
    lua_pushinteger(L, pos.rankOf(m.from) + 1);
    lua_setfield(L, -2, "from_y");
    lua_pushinteger(L, pos.fileOf(m.to) + 1);
    lua_setfield(L, -2, "to_x");
    lua_pushinteger(L, pos.rankOf(m.to) + 1);
    lua_setfield(L, -2, "to_y");
    lua_pushboolean(L, (m.flags & FlagCapture) != 0);
    lua_setfield(L, -2, "capture");
    if (m.promotion != NoPieceType) {
        lua_pushstring(L, pos.registry().get(m.promotion).id.c_str());
        lua_setfield(L, -2, "promotion");
    }
}

int intField(lua_State* L, int idx, const char* key, int arrayIndex, int fallback) {
    lua_getfield(L, idx, key);
    if (lua_isnil(L, -1) && arrayIndex > 0) {
        lua_pop(L, 1);
        lua_rawgeti(L, idx, arrayIndex);
    }
    int isNum = 0;
    const lua_Integer v = lua_tointegerx(L, -1, &isNum);
    lua_pop(L, 1);
    return isNum ? static_cast<int>(v) : fallback;
}

// Ein Zug aus Lua: vorhandenen Zug wiederverwenden, sonst neu bauen. nullopt = ungueltig.
std::optional<Move> readMove(lua_State* L, int idx, const Position& pos, const std::vector<Move>& known) {
    if (!lua_istable(L, idx)) return std::nullopt;
    const int fx = intField(L, idx, "from_x", 0, 0) - 1, fy = intField(L, idx, "from_y", 0, 0) - 1;
    const int tx = intField(L, idx, "to_x", 0, 0) - 1, ty = intField(L, idx, "to_y", 0, 0) - 1;
    if (!pos.contains(fx, fy) || !pos.contains(tx, ty)) return std::nullopt;
    PieceTypeId promo = NoPieceType;
    lua_getfield(L, idx, "promotion");
    if (lua_isstring(L, -1))
        if (auto t = pos.registry().findById(lua_tostring(L, -1))) promo = *t;
    lua_pop(L, 1);
    Move m{pos.square(fx, fy), pos.square(tx, ty), promo, FlagNone};
    for (const Move& k : known)
        if (k == m) return k;
    const Piece& mover = pos.at(m.from);
    const Piece& target = pos.at(m.to);
    if (mover.empty() || (!target.empty() && target.color == mover.color)) return std::nullopt;
    if (!target.empty()) m.flags |= FlagCapture;
    if (promo != NoPieceType) m.flags |= FlagPromotion;
    return m;
}

}  // namespace

// Zugriff der C-Funktionen auf private Mitglieder
struct LuaAccess {
    static LuaPlugin* self(lua_State* L) { return *static_cast<LuaPlugin**>(lua_getextraspace(L)); }

    static void* alloc(void* ud, void* ptr, size_t osize, size_t nsize) {
        auto* p = static_cast<LuaPlugin*>(ud);
        const size_t old = ptr ? osize : 0;
        if (nsize == 0) {
            std::free(ptr);
            p->memory_ -= old;
            return nullptr;
        }
        if (p->memory_ - old + nsize > LuaPlugin::kMemoryLimit) return nullptr;  // Lua meldet "not enough memory"
        void* n = std::realloc(ptr, nsize);
        if (n) p->memory_ = p->memory_ - old + nsize;
        return n;
    }

    static void countHook(lua_State* L, lua_Debug*) {
        LuaPlugin* p = self(L);
        p->instructions_ += 1000;
        if (p->instructions_ > LuaPlugin::kInstructionLimit)
            luaL_error(L, "Rechenzeit-Grenze erreicht (Endlosschleife?)");
    }

    static int print(lua_State* L) {
        std::string line;
        const int n = lua_gettop(L);
        for (int i = 1; i <= n; ++i) {
            if (i > 1) line += "  ";
            line += luaL_tolstring(L, i, nullptr);
            lua_pop(L, 1);
        }
        LuaPlugin* p = self(L);
        if (p->log_.size() < 500) p->log_.push_back(std::move(line));
        return 0;
    }

    // --- rules (nur in on_load) -------------------------------------------------
    static LuaPlugin* loading(lua_State* L) {
        LuaPlugin* p = self(L);
        if (!p->loadVariant_) luaL_error(L, "rules.* geht nur in on_load");
        return p;
    }
    static int setName(lua_State* L) {
        loading(L)->loadVariant_->name = luaL_checkstring(L, 1);
        return 0;
    }
    static int setStart(lua_State* L) {
        loading(L)->loadVariant_->startFen = luaL_checkstring(L, 1);
        return 0;
    }
    static int standardEndRules(lua_State* L) {
        loading(L)->loadVariant_->standardEndRules = lua_toboolean(L, 1) != 0;
        return 0;
    }
    static int pieceIds(lua_State* L) {
        LuaPlugin* p = loading(L);
        lua_newtable(L);
        int n = 0;
        for (const PieceType& t : p->loadRegistry_->all()) {
            lua_pushstring(L, t.id.c_str());
            lua_rawseti(L, -2, ++n);
        }
        return 1;
    }

    // Figur aus einer Lua-Tabelle lesen (fuer add_piece und edit_piece)
    static void readPiece(lua_State* L, int idx, PieceType& t, bool requireAll) {
        LuaPlugin* p = loading(L);
        luaL_checktype(L, idx, LUA_TTABLE);
        auto str = [&](const char* key, std::string& out) {
            lua_getfield(L, idx, key);
            if (lua_isstring(L, -1)) out = lua_tostring(L, -1);
            lua_pop(L, 1);
        };
        auto flag = [&](const char* key, bool& out) {
            lua_getfield(L, idx, key);
            if (!lua_isnil(L, -1)) out = lua_toboolean(L, -1) != 0;
            lua_pop(L, 1);
        };
        str("id", t.id);
        str("name", t.name);
        std::string sym;
        str("symbol", sym);
        if (!sym.empty()) t.symbol = static_cast<char>(std::toupper(static_cast<unsigned char>(sym[0])));
        if (requireAll && (t.id.empty() || sym.empty())) luaL_error(L, "Eine Figur braucht id und symbol");
        if (t.name.empty()) t.name = t.id;
        if (t.symbol < 'A' || t.symbol > 'Z') luaL_error(L, "symbol muss ein Buchstabe sein");
        if (auto other = p->loadRegistry_->findBySymbol(t.symbol); other && p->loadRegistry_->get(*other).id != t.id)
            luaL_error(L, "Symbol '%c' gehört schon zu '%s'", t.symbol, p->loadRegistry_->get(*other).id.c_str());
        lua_getfield(L, idx, "value");
        if (lua_isnumber(L, -1)) t.value = static_cast<int>(lua_tointeger(L, -1));
        lua_pop(L, 1);
        flag("royal", t.royal);
        flag("pawn_like", t.pawnLike);
        flag("promotion", t.promotionTarget);
        flag("castles", t.castles);

        // moves_like = "rook" oder {"rook", "knight"}: Zugmuster vorhandener Figuren uebernehmen
        lua_getfield(L, idx, "moves_like");
        if (!lua_isnil(L, -1)) {
            std::vector<std::string> like;
            if (lua_isstring(L, -1)) {
                like.emplace_back(lua_tostring(L, -1));
            } else if (lua_istable(L, -1)) {
                const lua_Integer n = luaL_len(L, -1);
                for (lua_Integer i = 1; i <= n; ++i) {
                    lua_rawgeti(L, -1, i);
                    if (lua_isstring(L, -1)) like.emplace_back(lua_tostring(L, -1));
                    lua_pop(L, 1);
                }
            }
            t.steps.clear();
            for (const std::string& id : like) {
                auto other = p->loadRegistry_->findById(id);
                if (!other) luaL_error(L, "moves_like: unbekannte Figur '%s'", id.c_str());
                t.steps = steps::combine(t.steps, p->loadRegistry_->get(*other).steps);
            }
        }
        lua_pop(L, 1);

        // steps = { {dx, dy, slide = true, only = "move"|"capture"}, ... }
        lua_getfield(L, idx, "steps");
        if (lua_istable(L, -1)) {
            const int sIdx = lua_gettop(L);
            const lua_Integer n = luaL_len(L, sIdx);
            for (lua_Integer i = 1; i <= n; ++i) {
                lua_rawgeti(L, sIdx, i);
                const int e = lua_gettop(L);
                if (lua_istable(L, e)) {
                    Step st;
                    st.dx = intField(L, e, "dx", 1, 0);
                    st.dy = intField(L, e, "dy", 2, 0);
                    lua_getfield(L, e, "slide");
                    st.slide = lua_toboolean(L, -1) != 0;
                    lua_pop(L, 1);
                    lua_getfield(L, e, "only");
                    if (lua_isstring(L, -1)) {
                        const std::string only = lua_tostring(L, -1);
                        st.mode = only == "move" ? StepMode::MoveOnly
                                  : only == "capture" ? StepMode::CaptureOnly
                                                      : StepMode::MoveOrCapture;
                    }
                    lua_pop(L, 1);
                    if (st.dx != 0 || st.dy != 0) t.steps.push_back(st);
                }
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);

        // moves = function(board, x, y) return { {x, y}, ... } end
        lua_getfield(L, idx, "moves");
        if (lua_isfunction(L, -1)) {
            const int ref = luaL_ref(L, LUA_REGISTRYINDEX);
            // Die Figur haelt ihr Plugin am Leben (auch in Stellungs-Kopien der Engine)
            std::shared_ptr<LuaPlugin> strong = p->shared_from_this();
            t.customMoves = [strong, ref](const Position& pos, Square from, std::vector<Move>& out) {
                strong->customMoves(ref, pos, from, out);
            };
        } else {
            lua_pop(L, 1);
        }
    }
    static int addPiece(lua_State* L) {
        LuaPlugin* p = loading(L);
        PieceType t;
        readPiece(L, 1, t, true);
        p->loadRegistry_->add(std::move(t));
        return 0;
    }
    static int editPiece(lua_State* L) {  // rules.edit_piece("knight", { value = 350, ... })
        LuaPlugin* p = loading(L);
        const char* id = luaL_checkstring(L, 1);
        auto type = p->loadRegistry_->findById(id);
        if (!type) return luaL_error(L, "Unbekannte Figur '%s'", id);
        PieceType t = p->loadRegistry_->get(*type);
        lua_getfield(L, 2, "steps");
        lua_getfield(L, 2, "moves_like");
        if (!lua_isnil(L, -1) || !lua_isnil(L, -2)) t.steps.clear();  // neues Zugmuster ersetzt das alte
        lua_pop(L, 2);
        readPiece(L, 2, t, false);
        t.id = id;
        p->loadRegistry_->add(std::move(t));
        return 0;
    }

    static void pushRules(lua_State* L) {
        static const luaL_Reg fns[] = {{"set_name", setName},
                                       {"set_start", setStart},
                                       {"standard_end_rules", standardEndRules},
                                       {"add_piece", addPiece},
                                       {"edit_piece", editPiece},
                                       {"piece_ids", pieceIds},
                                       {nullptr, nullptr}};
        luaL_newlib(L, fns);
    }
};

// ===========================================================================
// LuaPlugin
// ===========================================================================

LuaPlugin::LuaPlugin(std::string name) : name_(std::move(name)) {}

LuaPlugin::~LuaPlugin() {
    if (L_) lua_close(L_);
}

std::shared_ptr<LuaPlugin> LuaPlugin::fromSource(const std::string& source, const std::string& name,
                                                 std::string* error) {
    std::shared_ptr<LuaPlugin> p(new LuaPlugin(name));
    p->L_ = lua_newstate(&LuaAccess::alloc, p.get());
    if (!p->L_) {
        if (error) *error = "Lua konnte nicht gestartet werden";
        return nullptr;
    }
    lua_State* L = p->L_;
    *static_cast<LuaPlugin**>(lua_getextraspace(L)) = p.get();

    // Nur ungefaehrliche Bibliotheken: kein io, os, package, debug
    static const luaL_Reg libs[] = {{LUA_GNAME, luaopen_base},       {LUA_TABLIBNAME, luaopen_table},
                                    {LUA_STRLIBNAME, luaopen_string}, {LUA_MATHLIBNAME, luaopen_math},
                                    {LUA_UTF8LIBNAME, luaopen_utf8},  {nullptr, nullptr}};
    for (const luaL_Reg* lib = libs; lib->func; ++lib) {
        luaL_requiref(L, lib->name, lib->func, 1);
        lua_pop(L, 1);
    }
    for (const char* g : {"dofile", "loadfile", "load", "require", "collectgarbage"}) {
        lua_pushnil(L);
        lua_setglobal(L, g);
    }
    lua_getglobal(L, "string");
    lua_pushnil(L);
    lua_setfield(L, -2, "dump");
    lua_pop(L, 1);
    lua_pushcfunction(L, &LuaAccess::print);
    lua_setglobal(L, "print");
    registerBoardMeta(L);
    lua_sethook(L, &LuaAccess::countHook, LUA_MASKCOUNT, 1000);

    // Nur Text laden, kein vorkompilierter Bytecode
    if (luaL_loadbufferx(L, source.data(), source.size(), ("=" + name).c_str(), "t") != LUA_OK) {
        if (error) *error = lua_tostring(L, -1);
        return nullptr;
    }
    if (!p->pcall(0, 1, "main.lua")) {
        if (error) *error = p->log_.empty() ? "Fehler beim Ausführen" : p->log_.back();
        return nullptr;
    }
    if (!lua_istable(L, -1)) {
        if (error) *error = "main.lua muss eine Tabelle zurückgeben (return { ... })";
        return nullptr;
    }
    p->table_ = luaL_ref(L, LUA_REGISTRYINDEX);
    return p;
}

std::vector<std::string> LuaPlugin::log() const {
    std::lock_guard lock(mutex_);
    return log_;
}

bool LuaPlugin::pushFunction(const char* field) {
    lua_rawgeti(L_, LUA_REGISTRYINDEX, table_);
    lua_getfield(L_, -1, field);
    lua_remove(L_, -2);
    if (lua_isfunction(L_, -1)) return true;
    lua_pop(L_, 1);
    return false;
}

bool LuaPlugin::hasFunction(const char* field) {
    if (!pushFunction(field)) return false;
    lua_pop(L_, 1);
    return true;
}

bool LuaPlugin::pcall(int nargs, int nresults, const char* what) {
    instructions_ = 0;
    if (lua_pcall(L_, nargs, nresults, 0) == LUA_OK) return true;
    std::string msg = std::string(what) + ": " + (lua_isstring(L_, -1) ? lua_tostring(L_, -1) : "Fehler");
    lua_pop(L_, 1);
    if (log_.size() < 500) log_.push_back(std::move(msg));
    ++errors_;
    return false;
}

bool LuaPlugin::install(Variant& variant, PieceRegistry& registry, int priority, std::string* error) {
    std::lock_guard lock(mutex_);
    if (pushFunction("on_load")) {
        loadVariant_ = &variant;
        loadRegistry_ = &registry;
        LuaAccess::pushRules(L_);
        const bool ok = pcall(1, 0, "on_load");
        loadVariant_ = nullptr;
        loadRegistry_ = nullptr;
        if (!ok) {
            if (error) *error = log_.back();
            return false;
        }
    }

    auto strong = shared_from_this();
    // Hooks halten das Plugin am Leben, solange die Variante existiert
    if (hasFunction("on_setup"))
        variant.hooks.onSetup.add(name_, priority, [strong](Position& pos) {
            std::lock_guard l(strong->mutex_);
            if (!strong->pushFunction("on_setup")) return;
            BoardRef* b = pushBoard(strong->L_, &pos, pos);
            strong->pcall(1, 0, "on_setup");
            b->pos = nullptr, b->cpos = nullptr;
        });
    if (hasFunction("filter_moves"))
        variant.hooks.filterMoves.add(name_, priority, [strong](const Position& pos, std::vector<Move>& moves) {
            strong->filterMoves(pos, moves);
        });
    if (hasFunction("on_move"))
        variant.hooks.onMovePlayed.add(name_, priority, [strong](Position& pos, const Move& m) {
            std::lock_guard l(strong->mutex_);
            if (!strong->pushFunction("on_move")) return;
            BoardRef* b = pushBoard(strong->L_, &pos, pos);
            pushMove(strong->L_, pos, m);
            strong->pcall(2, 0, "on_move");
            b->pos = nullptr, b->cpos = nullptr;
        });
    if (hasFunction("check_end"))
        variant.hooks.checkGameEnd.add(name_, priority, [strong](const Game& game) -> std::optional<GameOutcome> {
            return strong->checkEnd(game);
        });
    return true;
}

void LuaPlugin::customMoves(int ref, const Position& pos, Square from, std::vector<Move>& out) {
    std::lock_guard lock(mutex_);
    lua_rawgeti(L_, LUA_REGISTRYINDEX, ref);
    BoardRef* b = pushBoard(L_, nullptr, pos);
    lua_pushinteger(L_, pos.fileOf(from) + 1);
    lua_pushinteger(L_, pos.rankOf(from) + 1);
    const bool ok = pcall(3, 1, "moves");
    b->cpos = nullptr;
    if (!ok) return;
    if (lua_istable(L_, -1)) {
        const Piece& mover = pos.at(from);
        const lua_Integer n = luaL_len(L_, -1);
        for (lua_Integer i = 1; i <= n; ++i) {
            lua_rawgeti(L_, -1, i);
            const int e = lua_gettop(L_);
            if (lua_istable(L_, e)) {
                const int x = intField(L_, e, "x", 1, 0) - 1, y = intField(L_, e, "y", 2, 0) - 1;
                if (pos.contains(x, y)) {
                    const Square to = pos.square(x, y);
                    const Piece& target = pos.at(to);
                    if (to != from && (target.empty() || target.color != mover.color))
                        out.push_back(Move{from, to, NoPieceType,
                                           static_cast<uint8_t>(target.empty() ? FlagNone : FlagCapture)});
                }
            }
            lua_pop(L_, 1);
        }
    }
    lua_pop(L_, 1);
}

void LuaPlugin::filterMoves(const Position& pos, std::vector<Move>& moves) {
    std::lock_guard lock(mutex_);
    if (!pushFunction("filter_moves")) return;
    BoardRef* b = pushBoard(L_, nullptr, pos);
    lua_createtable(L_, static_cast<int>(moves.size()), 0);
    for (size_t i = 0; i < moves.size(); ++i) {
        pushMove(L_, pos, moves[i]);
        lua_rawseti(L_, -2, static_cast<lua_Integer>(i + 1));
    }
    const bool ok = pcall(2, 1, "filter_moves");
    b->cpos = nullptr;
    if (!ok) return;
    if (lua_istable(L_, -1)) {
        std::vector<Move> result;
        const lua_Integer n = luaL_len(L_, -1);
        for (lua_Integer i = 1; i <= n; ++i) {
            lua_rawgeti(L_, -1, i);
            if (auto m = readMove(L_, lua_gettop(L_), pos, moves))
                if (std::find(result.begin(), result.end(), *m) == result.end()) result.push_back(*m);
            lua_pop(L_, 1);
        }
        moves = std::move(result);
    }
    lua_pop(L_, 1);
}

std::optional<GameOutcome> LuaPlugin::checkEnd(const Game& game) {
    std::lock_guard lock(mutex_);
    if (!pushFunction("check_end")) return std::nullopt;
    const Position& pos = game.position();
    BoardRef* b = pushBoard(L_, nullptr, pos);
    lua_createtable(L_, 0, 3);
    lua_pushinteger(L_, game.ply());
    lua_setfield(L_, -2, "ply");
    if (game.ply() > 0) {
        lua_pushstring(L_, game.sanMoves().back().c_str());
        lua_setfield(L_, -2, "last_san");
        pushMove(L_, pos, game.moves().back());
        lua_setfield(L_, -2, "last_move");
    }
    const bool ok = pcall(2, 2, "check_end");
    b->cpos = nullptr;
    if (!ok) return std::nullopt;
    std::optional<GameOutcome> out;
    if (lua_isstring(L_, -2)) {
        const std::string r = lua_tostring(L_, -2);
        const std::string reason = lua_isstring(L_, -1) ? lua_tostring(L_, -1) : name_;
        if (r == "white") out = GameOutcome{GameResult::WhiteWins, reason};
        if (r == "black") out = GameOutcome{GameResult::BlackWins, reason};
        if (r == "draw") out = GameOutcome{GameResult::Draw, reason};
    }
    lua_pop(L_, 2);
    return out;
}

// ===========================================================================
// Plugins finden und zusammenbauen
// ===========================================================================

std::vector<PluginInfo> discover(const std::vector<std::filesystem::path>& roots) {
    std::map<std::string, PluginInfo> found;
    for (const auto& root : roots) {
        std::error_code ec;
        for (const auto& e : std::filesystem::directory_iterator(root, ec)) {
            if (!e.is_directory() || !std::filesystem::exists(e.path() / "main.lua")) continue;
            PluginInfo info;
            info.id = e.path().filename().string();
            info.name = info.id;
            info.dir = e.path();
            const auto manifest = e.path() / "plugin.json";
            if (std::filesystem::exists(manifest)) {
                std::string err;
                if (auto v = json::parse(readText(manifest), &err); v && v->isObject()) {
                    info.name = v->getString("name", info.id);
                    info.description = v->getString("description", "");
                    info.author = v->getString("author", "");
                    info.version = v->getString("version", "");
                    info.priority = static_cast<int>(v->getNumber("priority", 0));
                } else {
                    info.error = "plugin.json: " + err;
                }
            }
            found[info.id] = std::move(info);
        }
    }
    std::vector<PluginInfo> out;
    for (auto& [id, info] : found) out.push_back(std::move(info));
    std::sort(out.begin(), out.end(), [](const PluginInfo& a, const PluginInfo& b) { return a.name < b.name; });
    return out;
}

VariantBuild buildVariant(const std::vector<PluginInfo>& active) {
    VariantBuild b;
    b.variant = standardVariant();
    if (active.empty()) return b;

    auto registry = std::make_shared<PieceRegistry>(*PieceRegistry::standard());
    // Niedrigste Prioritaet zuerst laden: Was spaeter laedt, gewinnt bei Startstellung, Name und Figuren.
    // So setzt sich das Plugin mit der hoechsten Prioritaet durch. Die Regel-Hooks laufen davon
    // unabhaengig nach Prioritaet (hoechste zuerst; bei check_end entscheidet das erste Ergebnis).
    std::vector<PluginInfo> order = active;
    std::stable_sort(order.begin(), order.end(),
                     [](const PluginInfo& x, const PluginInfo& y) { return x.priority < y.priority; });
    std::vector<std::string> names;
    for (const PluginInfo& info : order) {
        std::string err;
        auto plugin = LuaPlugin::fromSource(readText(info.dir / "main.lua"), info.name, &err);
        if (!plugin || !plugin->install(b.variant, *registry, info.priority, &err)) {
            b.errors.push_back(info.name + ": " + err);
            continue;
        }
        names.push_back(info.name);
        b.loaded.push_back(std::move(plugin));
    }
    b.variant.pieces = registry;
    if (b.variant.name == "Standard" && !names.empty()) {
        b.variant.name.clear();
        for (auto it = names.rbegin(); it != names.rend(); ++it)  // wichtigstes zuerst
            b.variant.name += (b.variant.name.empty() ? "" : " + ") + *it;
    }
    // Startstellung pruefen: lieber Standardschach als ein kaputtes Brett
    try {
        Game test(b.variant);
        (void)test.legalMoves();
    } catch (const std::exception& e) {
        b.errors.push_back(std::string("Startstellung ungültig: ") + e.what());
        b.variant = standardVariant();
        b.loaded.clear();
    }
    return b;
}

}  // namespace chess::plugins
