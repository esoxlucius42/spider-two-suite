#pragma once

#include "spider/game_state.hpp"

#include <cstdint>
#include <vector>

namespace spider {

class GameSession {
public:
    [[nodiscard]] static auto create_new_game(std::uint64_t seed) -> GameSession;
    [[nodiscard]] static auto create_restored_session(
        GameState state,
        std::vector<GameState> undo_history,
        std::vector<GameState> redo_history) -> GameSession;

    [[nodiscard]] auto state() const -> const GameState&;
    [[nodiscard]] auto undo_history() const -> const std::vector<GameState>&;
    [[nodiscard]] auto redo_history() const -> const std::vector<GameState>&;

    [[nodiscard]] auto can_move_sequence(const Move& move) const -> bool;
    [[nodiscard]] auto move_sequence(const Move& move) -> bool;

    [[nodiscard]] auto can_deal_from_stock() const -> bool;
    [[nodiscard]] auto deal_from_stock() -> bool;

    [[nodiscard]] auto can_undo() const -> bool;
    [[nodiscard]] auto can_redo() const -> bool;
    [[nodiscard]] auto undo() -> bool;
    [[nodiscard]] auto redo() -> bool;

    void restart();
    void start_new_game(std::uint64_t seed);

private:
    GameState state_ {};
    std::vector<GameState> undo_history_ {};
    std::vector<GameState> redo_history_ {};

    void record_state_before_change();
    void clear_history();
};

} // namespace spider
