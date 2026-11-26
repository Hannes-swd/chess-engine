#include "core/piece_type.hpp"

#include <utility>

namespace chess {

namespace {

std::vector<Step> rays(const std::vector<std::pair<int, int>>& dirs, bool slide) {
    std::vector<Step> out;
    out.reserve(dirs.size());
    for (const auto& [dx, dy] : dirs) out.push_back(Step{dx, dy, slide, StepMode::MoveOrCapture});
    return out;
}

}  // namespace

namespace steps {

std::vector<Step> orthogonal(bool slide) { return rays({{1, 0}, {-1, 0}, {0, 1}, {0, -1}}, slide); }

std::vector<Step> diagonal(bool slide) { return rays({{1, 1}, {1, -1}, {-1, 1}, {-1, -1}}, slide); }

std::vector<Step> knight() {
    return rays({{1, 2}, {2, 1}, {-1, 2}, {-2, 1}, {1, -2}, {2, -1}, {-1, -2}, {-2, -1}}, false);
}

std::vector<Step> combine(std::vector<Step> a, const std::vector<Step>& b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

}  // namespace steps

PieceTypeId PieceRegistry::add(PieceType type) {
    if (auto existing = findById(type.id)) {
        types_[static_cast<size_t>(*existing)] = std::move(type);
        return *existing;
    }
    types_.push_back(std::move(type));
    return static_cast<PieceTypeId>(types_.size() - 1);
}

std::optional<PieceTypeId> PieceRegistry::findById(const std::string& id) const {
    for (size_t i = 0; i < types_.size(); ++i)
        if (types_[i].id == id) return static_cast<PieceTypeId>(i);
    return std::nullopt;
}

std::optional<PieceTypeId> PieceRegistry::findBySymbol(char upperSymbol) const {
    for (size_t i = 0; i < types_.size(); ++i)
        if (types_[i].symbol == upperSymbol) return static_cast<PieceTypeId>(i);
    return std::nullopt;
}

std::shared_ptr<PieceRegistry> PieceRegistry::standard() {
    auto reg = std::make_shared<PieceRegistry>();

    PieceType pawn;
    pawn.id = "pawn";
    pawn.name = "Bauer";
    pawn.symbol = 'P';
    pawn.value = 100;
    pawn.steps = {
        Step{0, 1, false, StepMode::MoveOnly},
        Step{1, 1, false, StepMode::CaptureOnly},
        Step{-1, 1, false, StepMode::CaptureOnly},
    };
    pawn.pawnLike = true;
    reg->add(pawn);

    PieceType knight;
    knight.id = "knight";
    knight.name = "Springer";
    knight.symbol = 'N';
    knight.value = 320;
    knight.steps = steps::knight();
    knight.promotionTarget = true;
    reg->add(knight);

    PieceType bishop;
    bishop.id = "bishop";
    bishop.name = "Läufer";
    bishop.symbol = 'B';
    bishop.value = 330;
    bishop.steps = steps::diagonal(true);
    bishop.promotionTarget = true;
    reg->add(bishop);

    PieceType rook;
    rook.id = "rook";
    rook.name = "Turm";
    rook.symbol = 'R';
    rook.value = 500;
    rook.steps = steps::orthogonal(true);
    rook.castlePartner = true;
    rook.promotionTarget = true;
    reg->add(rook);

    PieceType queen;
    queen.id = "queen";
    queen.name = "Dame";
    queen.symbol = 'Q';
    queen.value = 900;
    queen.steps = steps::combine(steps::orthogonal(true), steps::diagonal(true));
    queen.promotionTarget = true;
    reg->add(queen);

    PieceType king;
    king.id = "king";
    king.name = "König";
    king.symbol = 'K';
    king.value = 0;
    king.steps = steps::combine(steps::orthogonal(false), steps::diagonal(false));
    king.royal = true;
    king.castles = true;
    reg->add(king);

    return reg;
}

}  // namespace chess
