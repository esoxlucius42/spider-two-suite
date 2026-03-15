#pragma once

#include "spider/card.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace spider {

struct Move {
    std::size_t from_stack {};
    std::size_t start_index {};
    std::size_t to_stack {};
};

class GameState {
public:
    static constexpr std::size_t kTableauStacks = 10;
    static constexpr std::size_t kCompletedRunLength = 13;
    static constexpr std::size_t kStockRowSize = 10;
    static constexpr std::size_t kStockRows = 6;
    static constexpr std::size_t kWinningCompletedRuns = 8;

    [[nodiscard]] static auto create_new_game(std::uint64_t seed) -> GameState;
    [[nodiscard]] static auto create_restored_game(
        std::array<std::vector<Card>, kTableauStacks> tableau,
        std::vector<std::array<Card, kStockRowSize>> stock_rows,
        std::size_t completed_runs,
        std::size_t move_count,
        std::uint64_t seed) -> GameState;

    [[nodiscard]] auto tableau() -> std::array<std::vector<Card>, kTableauStacks>&;
    [[nodiscard]] auto tableau() const -> const std::array<std::vector<Card>, kTableauStacks>&;
    [[nodiscard]] auto stock_rows() const -> const std::vector<std::array<Card, kStockRowSize>>&;
    [[nodiscard]] auto stock_rows_remaining() const -> std::size_t;
    [[nodiscard]] auto completed_runs() const -> std::size_t;
    [[nodiscard]] auto move_count() const -> std::size_t;
    [[nodiscard]] auto seed() const -> std::uint64_t;
    [[nodiscard]] auto has_won() const -> bool;

    [[nodiscard]] auto is_movable_sequence(std::size_t stack_index, std::size_t start_index) const -> bool;
    [[nodiscard]] auto can_move_sequence(const Move& move) const -> bool;
    [[nodiscard]] auto move_sequence(const Move& move) -> bool;

    [[nodiscard]] auto can_deal_from_stock() const -> bool;
    [[nodiscard]] auto deal_from_stock() -> bool;

    [[nodiscard]] auto top_face_up_index(std::size_t stack_index) const -> std::optional<std::size_t>;
    [[nodiscard]] auto hidden_card_count(std::size_t stack_index) const -> std::size_t;

private:
    std::array<std::vector<Card>, kTableauStacks> tableau_ {};
    std::vector<std::array<Card, kStockRowSize>> stock_rows_ {};
    std::size_t completed_runs_ {0};
    std::size_t move_count_ {0};
    std::uint64_t seed_ {0};

    void reveal_top_card(std::size_t stack_index);
    void clear_completed_runs(std::size_t stack_index);
};

} // namespace spider
