#include "spider/game_state.hpp"

#include <algorithm>
#include <array>
#include <random>

namespace spider {

namespace {

auto build_shuffled_deck(std::uint64_t seed) -> std::vector<Card>
{
    std::vector<Card> deck;
    deck.reserve(104);

    for (Suit suit : kPlayableSuits) {
        for (std::uint8_t copy = 0; copy < 4; ++copy) {
            for (Rank rank : kRanks) {
                deck.push_back(Card {.suit = suit, .rank = rank, .face_up = false, .copy_index = copy});
            }
        }
    }

    std::mt19937_64 rng(seed);
    std::shuffle(deck.begin(), deck.end(), rng);
    return deck;
}

auto sequence_descends_in_suit(const std::vector<Card>& stack, std::size_t start_index) -> bool
{
    if (start_index >= stack.size()) {
        return false;
    }

    if (!stack[start_index].face_up) {
        return false;
    }

    for (std::size_t index = start_index; index + 1 < stack.size(); ++index) {
        const Card& current = stack[index];
        const Card& next = stack[index + 1];

        if (!next.face_up || current.suit != next.suit || !is_one_lower(next.rank, current.rank)) {
            return false;
        }
    }

    return true;
}

auto top_accepts_card(const std::vector<Card>& destination, const Card& lead_card) -> bool
{
    if (destination.empty()) {
        return true;
    }

    const Card& target = destination.back();
    return target.face_up && is_one_lower(lead_card.rank, target.rank);
}

auto is_completed_run(const std::vector<Card>& stack) -> bool
{
    if (stack.size() < GameState::kCompletedRunLength) {
        return false;
    }

    const std::size_t start_index = stack.size() - GameState::kCompletedRunLength;
    const Suit suit = stack[start_index].suit;

    if (!stack[start_index].face_up || stack[start_index].rank != Rank::king) {
        return false;
    }

    for (std::size_t offset = 0; offset < GameState::kCompletedRunLength; ++offset) {
        const Card& card = stack[start_index + offset];
        const Rank expected = static_cast<Rank>(rank_value(Rank::king) - static_cast<int>(offset));

        if (!card.face_up || card.suit != suit || card.rank != expected) {
            return false;
        }
    }

    return true;
}

} // namespace

auto GameState::create_new_game(std::uint64_t seed) -> GameState
{
    GameState state;
    state.seed_ = seed;

    OpeningDeal opening_deal = build_opening_deal(seed);
    for (const OpeningDealStep& step : opening_deal.tableau_steps) {
        state.tableau_[step.stack_index].push_back(step.card);
    }

    state.stock_rows_ = std::move(opening_deal.stock_rows);
    return state;
}

auto GameState::build_opening_deal(std::uint64_t seed) -> OpeningDeal
{
    OpeningDeal opening_deal;
    opening_deal.tableau_steps.reserve(44);

    std::vector<Card> deck = build_shuffled_deck(seed);
    std::size_t cursor = 0;
    std::array<std::size_t, kTableauStacks> dealt_counts {};

    for (std::size_t round = 0; round < 5; ++round) {
        for (std::size_t stack_index = 0; stack_index < kTableauStacks; ++stack_index) {
            if (round == 4 && stack_index >= 4) {
                continue;
            }

            Card card = deck[cursor++];
            const std::size_t card_index = dealt_counts[stack_index];
            const std::size_t final_stack_size = stack_index < 4 ? 5 : 4;
            card.face_up = (card_index + 1 == final_stack_size);
            opening_deal.tableau_steps.push_back(OpeningDealStep {
                .card = card,
                .stack_index = stack_index,
                .card_index = card_index,
            });
            ++dealt_counts[stack_index];
        }
    }

    opening_deal.stock_rows.reserve(kStockRows);
    for (std::size_t row = 0; row < kStockRows; ++row) {
        std::array<Card, kStockRowSize> stock_row {};
        for (Card& card : stock_row) {
            card = deck[cursor++];
            card.face_up = true;
        }
        opening_deal.stock_rows.push_back(stock_row);
    }

    return opening_deal;
}

auto GameState::create_restored_game(
    std::array<std::vector<Card>, kTableauStacks> tableau,
    std::vector<std::array<Card, kStockRowSize>> stock_rows,
    std::size_t completed_runs,
    std::size_t move_count,
    std::uint64_t seed) -> GameState
{
    GameState state;
    state.tableau_ = std::move(tableau);
    state.stock_rows_ = std::move(stock_rows);
    state.completed_runs_ = completed_runs;
    state.move_count_ = move_count;
    state.seed_ = seed;
    return state;
}

auto GameState::tableau() const -> const std::array<std::vector<Card>, kTableauStacks>&
{
    return tableau_;
}

auto GameState::tableau() -> std::array<std::vector<Card>, kTableauStacks>&
{
    return tableau_;
}

auto GameState::stock_rows() const -> const std::vector<std::array<Card, kStockRowSize>>&
{
    return stock_rows_;
}

auto GameState::stock_rows_remaining() const -> std::size_t
{
    return stock_rows_.size();
}

auto GameState::completed_runs() const -> std::size_t
{
    return completed_runs_;
}

auto GameState::move_count() const -> std::size_t
{
    return move_count_;
}

auto GameState::seed() const -> std::uint64_t
{
    return seed_;
}

auto GameState::has_won() const -> bool
{
    return completed_runs_ == kWinningCompletedRuns;
}

auto GameState::is_movable_sequence(std::size_t stack_index, std::size_t start_index) const -> bool
{
    if (stack_index >= kTableauStacks) {
        return false;
    }

    return sequence_descends_in_suit(tableau_[stack_index], start_index);
}

auto GameState::can_move_sequence(const Move& move) const -> bool
{
    if (move.from_stack >= kTableauStacks || move.to_stack >= kTableauStacks || move.from_stack == move.to_stack) {
        return false;
    }

    const auto& from = tableau_[move.from_stack];
    const auto& to = tableau_[move.to_stack];

    if (!sequence_descends_in_suit(from, move.start_index)) {
        return false;
    }

    return top_accepts_card(to, from[move.start_index]);
}

auto GameState::move_sequence(const Move& move) -> bool
{
    if (!can_move_sequence(move)) {
        return false;
    }

    auto& from = tableau_[move.from_stack];
    auto& to = tableau_[move.to_stack];

    const auto split = from.begin() + static_cast<std::ptrdiff_t>(move.start_index);
    to.insert(to.end(), split, from.end());
    from.erase(split, from.end());

    reveal_top_card(move.from_stack);
    clear_completed_runs(move.to_stack);
    clear_completed_runs(move.from_stack);
    ++move_count_;

    return true;
}

auto GameState::find_auto_move(std::size_t from_stack, std::size_t start_index) const -> std::optional<Move>
{
    if (from_stack >= kTableauStacks || !sequence_descends_in_suit(tableau_[from_stack], start_index)) {
        return std::nullopt;
    }

    for (std::size_t to_stack = 0; to_stack < kTableauStacks; ++to_stack) {
        if (to_stack == from_stack || tableau_[to_stack].empty()) {
            continue;
        }

        const Move move {
            .from_stack = from_stack,
            .start_index = start_index,
            .to_stack = to_stack,
        };
        if (can_move_sequence(move)) {
            return move;
        }
    }

    for (std::size_t to_stack = 0; to_stack < kTableauStacks; ++to_stack) {
        if (to_stack == from_stack || !tableau_[to_stack].empty()) {
            continue;
        }

        const Move move {
            .from_stack = from_stack,
            .start_index = start_index,
            .to_stack = to_stack,
        };
        if (can_move_sequence(move)) {
            return move;
        }
    }

    return std::nullopt;
}

auto GameState::can_deal_from_stock() const -> bool
{
    return !stock_rows_.empty();
}

auto GameState::deal_from_stock() -> bool
{
    if (!can_deal_from_stock()) {
        return false;
    }

    const auto stock_row = stock_rows_.front();
    stock_rows_.erase(stock_rows_.begin());

    for (std::size_t stack_index = 0; stack_index < kTableauStacks; ++stack_index) {
        tableau_[stack_index].push_back(stock_row[stack_index]);
        clear_completed_runs(stack_index);
    }

    ++move_count_;
    return true;
}

auto GameState::top_face_up_index(std::size_t stack_index) const -> std::optional<std::size_t>
{
    if (stack_index >= kTableauStacks) {
        return std::nullopt;
    }

    const auto& stack = tableau_[stack_index];
    for (std::size_t index = 0; index < stack.size(); ++index) {
        if (stack[index].face_up) {
            return index;
        }
    }

    return std::nullopt;
}

auto GameState::hidden_card_count(std::size_t stack_index) const -> std::size_t
{
    if (stack_index >= kTableauStacks) {
        return 0;
    }

    std::size_t hidden = 0;
    for (const Card& card : tableau_[stack_index]) {
        if (!card.face_up) {
            ++hidden;
        }
    }
    return hidden;
}

void GameState::reveal_top_card(std::size_t stack_index)
{
    auto& stack = tableau_[stack_index];
    if (!stack.empty()) {
        stack.back().face_up = true;
    }
}

void GameState::clear_completed_runs(std::size_t stack_index)
{
    auto& stack = tableau_[stack_index];

    while (is_completed_run(stack)) {
        stack.erase(stack.end() - static_cast<std::ptrdiff_t>(kCompletedRunLength), stack.end());
        ++completed_runs_;
        reveal_top_card(stack_index);
    }
}

} // namespace spider
