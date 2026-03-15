#include "spider/game_state.hpp"
#include "spider/game_session.hpp"
#include "spider/layout.hpp"

#include <array>
#include <cstdlib>
#include <iostream>

namespace {

void expect(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "test failure: " << message << '\n';
        std::exit(1);
    }
}

auto same_card(const spider::Card& left, const spider::Card& right) -> bool
{
    return left.suit == right.suit
        && left.rank == right.rank
        && left.face_up == right.face_up
        && left.copy_index == right.copy_index;
}

void expect_same_state(const spider::GameState& left, const spider::GameState& right, const char* message)
{
    expect(left.stock_rows_remaining() == right.stock_rows_remaining(), message);
    expect(left.completed_runs() == right.completed_runs(), message);
    expect(left.move_count() == right.move_count(), message);
    expect(left.seed() == right.seed(), message);

    for (std::size_t stack_index = 0; stack_index < spider::GameState::kTableauStacks; ++stack_index) {
        const auto& left_stack = left.tableau()[stack_index];
        const auto& right_stack = right.tableau()[stack_index];
        expect(left_stack.size() == right_stack.size(), message);
        for (std::size_t card_index = 0; card_index < left_stack.size(); ++card_index) {
            expect(same_card(left_stack[card_index], right_stack[card_index]), message);
        }
    }
}

} // namespace

int main()
{
    using spider::GameState;
    using spider::Move;

    {
        GameState state = GameState::create_new_game(7);

        std::size_t tableau_cards = 0;
        for (std::size_t stack_index = 0; stack_index < GameState::kTableauStacks; ++stack_index) {
            const auto& stack = state.tableau()[stack_index];
            tableau_cards += stack.size();
            expect(!stack.empty(), "each tableau stack should receive cards");
            expect(stack.back().face_up, "top tableau card should start face up");
        }

        expect(tableau_cards == 54, "initial tableau should contain 54 cards");
        expect(state.stock_rows_remaining() == GameState::kStockRows, "game should start with five stock rows");
    }

    {
        GameState state = GameState::create_new_game(99);
        const std::size_t before = state.stock_rows_remaining();
        expect(state.deal_from_stock(), "dealing from stock should succeed while stock rows remain");
        expect(state.stock_rows_remaining() + 1 == before, "dealing should consume one stock row");
    }

    {
        GameState state = GameState::create_new_game(1);
        auto& stacks = state.tableau();

        stacks[0] = {
            {.suit = spider::Suit::spades, .rank = spider::Rank::king, .face_up = true},
            {.suit = spider::Suit::spades, .rank = spider::Rank::queen, .face_up = true},
        };
        stacks[1] = {
            {.suit = spider::Suit::hearts, .rank = spider::Rank::ace, .face_up = true},
        };

        expect(state.can_move_sequence(Move {.from_stack = 0, .start_index = 1, .to_stack = 1}) == false, "queen cannot move onto ace");

        stacks[1] = {
            {.suit = spider::Suit::hearts, .rank = spider::Rank::king, .face_up = true},
        };

        expect(state.can_move_sequence(Move {.from_stack = 0, .start_index = 1, .to_stack = 1}), "queen should move onto king");
        expect(state.move_sequence(Move {.from_stack = 0, .start_index = 1, .to_stack = 1}), "valid move should execute");
        expect(stacks[1].size() == 2, "destination stack should gain moved card");
    }

    {
        GameState state = GameState::create_new_game(2);
        auto& stacks = state.tableau();

        stacks[0].clear();
        for (int rank = static_cast<int>(spider::Rank::king); rank >= static_cast<int>(spider::Rank::two); --rank) {
            stacks[0].push_back({
                .suit = spider::Suit::spades,
                .rank = static_cast<spider::Rank>(rank),
                .face_up = true,
            });
        }

        stacks[1] = {
            {.suit = spider::Suit::spades, .rank = spider::Rank::ace, .face_up = true},
        };

        expect(state.move_sequence(Move {.from_stack = 1, .start_index = 0, .to_stack = 0}), "ace should complete a same-suit run");
        expect(stacks[0].empty(), "completed run should be removed from the tableau");
        expect(state.completed_runs() == 1, "completed run should be counted");
    }

    {
        const std::array<std::size_t, 10> small_counts {6, 6, 6, 6, 5, 5, 5, 5, 5, 5};
        const std::array<std::size_t, 10> small_hidden {5, 5, 5, 5, 4, 4, 4, 4, 4, 4};
        const std::array<std::size_t, 10> tall_counts {22, 21, 23, 20, 22, 21, 24, 20, 21, 22};
        const std::array<std::size_t, 10> tall_hidden {7, 7, 8, 6, 7, 7, 8, 6, 7, 7};

        const auto roomy = spider::compute_layout(1600, 900, small_counts, small_hidden, 0.0F);
        const auto tight = spider::compute_layout(900, 640, tall_counts, tall_hidden, 400.0F);

        expect(roomy.stack_x[1] > roomy.stack_x[0], "stack x positions should increase left to right");
        expect(tight.card_width < roomy.card_width, "card width should shrink on narrower windows");
        expect(tight.face_up_step <= roomy.face_up_step, "face-up spacing should compress for tighter layouts");
        expect(tight.scroll_offset >= 0.0F, "scroll offset should remain non-negative");
        expect(tight.content_height >= tight.playfield_height, "tall layouts should produce scrollable content");
    }

    {
        spider::GameSession session = spider::GameSession::create_new_game(33);
        const GameState initial = session.state();

        expect(!session.can_undo(), "new session should not have undo history");
        expect(!session.can_redo(), "new session should not have redo history");
        expect(session.deal_from_stock(), "dealing through the session should succeed");
        expect(session.can_undo(), "session should allow undo after a stock deal");
        expect(!session.can_redo(), "redo should remain empty until an undo occurs");

        expect(session.undo(), "undo should restore the previous state");
        expect_same_state(session.state(), initial, "undo should restore the initial deal");
        expect(session.can_redo(), "redo should become available after undo");
        expect(session.redo(), "redo should reapply the undone state");
        expect(session.state().stock_rows_remaining() + 1 == initial.stock_rows_remaining(), "redo should restore the dealt stock count");
    }

    {
        spider::GameSession session = spider::GameSession::create_new_game(44);
        const GameState initial = session.state();
        expect(session.deal_from_stock(), "stock deal should succeed before restart");

        session.restart();

        expect_same_state(session.state(), initial, "restart should restore the current seed's initial deal");
        expect(!session.can_undo(), "restart should clear undo history");
        expect(!session.can_redo(), "restart should clear redo history");
    }

    return 0;
}
