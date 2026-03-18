#include "spider/card_sprite.hpp"
#include "spider/game_state.hpp"
#include "spider/game_session.hpp"
#include "spider/layout.hpp"
#include "spider/persistence.hpp"

#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <string>

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
    expect(left.completed_run_suits() == right.completed_run_suits(), message);
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

    expect(left.stock_rows().size() == right.stock_rows().size(), message);
    for (std::size_t row_index = 0; row_index < left.stock_rows().size(); ++row_index) {
        for (std::size_t card_index = 0; card_index < spider::GameState::kStockRowSize; ++card_index) {
            expect(same_card(left.stock_rows()[row_index][card_index], right.stock_rows()[row_index][card_index]), message);
        }
    }
}

void expect_same_session(const spider::GameSession& left, const spider::GameSession& right, const char* message)
{
    expect_same_state(left.state(), right.state(), message);
    expect(left.undo_history().size() == right.undo_history().size(), message);
    expect(left.redo_history().size() == right.redo_history().size(), message);

    for (std::size_t index = 0; index < left.undo_history().size(); ++index) {
        expect_same_state(left.undo_history()[index], right.undo_history()[index], message);
    }

    for (std::size_t index = 0; index < left.redo_history().size(); ++index) {
        expect_same_state(left.redo_history()[index], right.redo_history()[index], message);
    }
}

auto test_save_path(const char* name) -> std::filesystem::path
{
    return std::filesystem::temp_directory_path() / "spider-two-suite-tests" / name;
}

void reset_test_save(const std::filesystem::path& path)
{
    std::error_code ignored;
    std::filesystem::create_directories(path.parent_path(), ignored);
    std::filesystem::remove(path, ignored);
}

void expect_near(float left, float right, float epsilon, const char* message)
{
    expect(std::fabs(left - right) <= epsilon, message);
}

} // namespace

int main()
{
    using spider::GameState;
    using spider::Move;

    {
        expect(spider::atlas_row_for_suit(spider::Suit::spades) == 3, "spades should use the spade sprite row");
        expect(spider::atlas_row_for_suit(spider::Suit::hearts) == 2, "hearts should use the heart sprite row");
    }

    {
        GameState state = GameState::create_new_game(7);
        const std::array<std::size_t, 10> expected_counts {5, 5, 5, 5, 4, 4, 4, 4, 4, 4};
        const std::array<std::size_t, 10> expected_hidden {4, 4, 4, 4, 3, 3, 3, 3, 3, 3};

        std::size_t tableau_cards = 0;
        for (std::size_t stack_index = 0; stack_index < GameState::kTableauStacks; ++stack_index) {
            const auto& stack = state.tableau()[stack_index];
            tableau_cards += stack.size();
            expect(!stack.empty(), "each tableau stack should receive cards");
            expect(stack.size() == expected_counts[stack_index], "fresh game should deal the expected count into each tableau stack");
            expect(state.hidden_card_count(stack_index) == expected_hidden[stack_index], "fresh game should hide the expected number of cards in each tableau stack");
            expect(stack.back().face_up, "top tableau card should start face up");
        }

        expect(tableau_cards == 44, "initial tableau should contain 44 cards");
        expect(state.stock_rows_remaining() == GameState::kStockRows, "game should start with six stock rows");
    }

    {
        const auto opening_deal = GameState::build_opening_deal(7);
        expect(opening_deal.tableau_steps.size() == 44, "opening deal helper should emit all tableau cards");
        expect(opening_deal.stock_rows.size() == GameState::kStockRows, "opening deal helper should retain all stock rows");

        for (std::size_t index = 0; index < 10; ++index) {
            expect(opening_deal.tableau_steps[index].stack_index == index, "opening deal should start with a left-to-right round");
            expect(opening_deal.tableau_steps[index].card_index == 0, "opening deal first round should target the bottom slot");
            expect(!opening_deal.tableau_steps[index].card.face_up, "opening deal first round should stay face down");
        }

        for (std::size_t offset = 0; offset < 10; ++offset) {
            const auto& step = opening_deal.tableau_steps[30 + offset];
            expect(step.stack_index == offset, "opening deal fourth round should still go left to right");
            expect(step.card_index == 3, "opening deal fourth round should target the fourth slot");
            expect(step.card.face_up == (offset >= 4), "opening deal fourth round should expose the short stacks only");
        }

        for (std::size_t offset = 0; offset < 4; ++offset) {
            const auto& step = opening_deal.tableau_steps[40 + offset];
            expect(step.stack_index == offset, "opening deal final round should only target the first four stacks");
            expect(step.card_index == 4, "opening deal final round should target the fifth slot");
            expect(step.card.face_up, "opening deal final round should expose the tall stacks");
        }

        const GameState state = GameState::create_new_game(7);
        for (const auto& step : opening_deal.tableau_steps) {
            expect(same_card(state.tableau()[step.stack_index][step.card_index], step.card), "opening deal helper should match the created game state");
        }
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
        GameState state = GameState::create_new_game(3);
        auto& stacks = state.tableau();

        for (auto& stack : stacks) {
            stack.clear();
        }

        stacks[4] = {
            {.suit = spider::Suit::spades, .rank = spider::Rank::king, .face_up = true},
            {.suit = spider::Suit::spades, .rank = spider::Rank::queen, .face_up = true},
            {.suit = spider::Suit::spades, .rank = spider::Rank::jack, .face_up = true},
        };
        stacks[1] = {
            {.suit = spider::Suit::hearts, .rank = spider::Rank::king, .face_up = true},
        };
        stacks[6] = {
            {.suit = spider::Suit::spades, .rank = spider::Rank::king, .face_up = true},
        };

        const auto move = state.find_auto_move(4, 1);
        expect(move.has_value(), "movable non-top tail should find an automatic destination");
        expect(move->to_stack == 1, "automatic move should pick the leftmost legal non-empty stack");
        expect(state.move_sequence(*move), "automatic move should execute through the normal move path");
        expect(stacks[1].size() == 3, "automatic move should transfer the clicked card and cards above it");
        expect(stacks[1][1].rank == spider::Rank::queen, "automatic move should place the clicked card first");
        expect(stacks[1][2].rank == spider::Rank::jack, "automatic move should preserve the tail order");
    }

    {
        GameState state = GameState::create_new_game(4);
        auto& stacks = state.tableau();

        for (auto& stack : stacks) {
            stack.clear();
        }

        stacks[5] = {
            {.suit = spider::Suit::spades, .rank = spider::Rank::queen, .face_up = true},
        };
        stacks[0].clear();
        stacks[2] = {
            {.suit = spider::Suit::hearts, .rank = spider::Rank::ace, .face_up = true},
        };

        const auto move = state.find_auto_move(5, 0);
        expect(move.has_value(), "automatic move should fall back to an empty stack when needed");
        expect(move->to_stack == 0, "automatic move should pick the leftmost empty stack after non-empty stacks fail");
    }

    {
        GameState state = GameState::create_new_game(5);
        auto& stacks = state.tableau();

        for (auto& stack : stacks) {
            stack.clear();
        }

        stacks[3] = {
            {.suit = spider::Suit::spades, .rank = spider::Rank::queen, .face_up = true},
        };
        stacks[0].clear();
        stacks[1] = {
            {.suit = spider::Suit::hearts, .rank = spider::Rank::king, .face_up = true},
        };

        const auto move = state.find_auto_move(3, 0);
        expect(move.has_value(), "automatic move should still prefer a non-empty legal destination");
        expect(move->to_stack == 1, "automatic move should ignore earlier empty stacks during the first scan");
    }

    {
        GameState state = GameState::create_new_game(6);
        auto& stacks = state.tableau();

        for (auto& stack : stacks) {
            stack.clear();
        }

        stacks[2] = {
            {.suit = spider::Suit::spades, .rank = spider::Rank::queen, .face_up = true},
            {.suit = spider::Suit::hearts, .rank = spider::Rank::jack, .face_up = true},
        };
        stacks[7] = {
            {.suit = spider::Suit::hearts, .rank = spider::Rank::king, .face_up = true},
        };

        expect(!state.find_auto_move(2, 0).has_value(), "automatic move should do nothing for a non-movable mixed-suit tail");
        expect(!state.find_auto_move(9, 0).has_value(), "automatic move should reject an out-of-range source stack");
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

        std::vector<spider::CompletedRunEvent> completed_runs;
        expect(state.move_sequence(Move {.from_stack = 1, .start_index = 0, .to_stack = 0}, completed_runs), "ace should complete a same-suit run");
        expect(stacks[0].empty(), "completed run should be removed from the tableau");
        expect(state.completed_runs() == 1, "completed run should be counted");
        expect(state.completed_run_suits().size() == 1 && state.completed_run_suits()[0] == spider::Suit::spades, "completed run should preserve its suit");
        expect(completed_runs.size() == 1, "completed move should report one completed run event");
        expect(completed_runs[0].stack_index == 0, "completed run event should point at the destination stack");
        expect(completed_runs[0].start_index == 0, "completed run event should record the top run start");
        expect(completed_runs[0].cards[0].rank == spider::Rank::king, "completed run event should keep king at the front");
        expect(completed_runs[0].cards[12].rank == spider::Rank::ace, "completed run event should keep ace at the tail");
    }

    {
        std::array<std::vector<spider::Card>, GameState::kTableauStacks> tableau {};
        tableau[0] = {
            {.suit = spider::Suit::spades, .rank = spider::Rank::king, .face_up = true},
            {.suit = spider::Suit::spades, .rank = spider::Rank::queen, .face_up = true},
            {.suit = spider::Suit::spades, .rank = spider::Rank::jack, .face_up = true},
            {.suit = spider::Suit::spades, .rank = spider::Rank::ten, .face_up = true},
            {.suit = spider::Suit::spades, .rank = spider::Rank::nine, .face_up = true},
            {.suit = spider::Suit::spades, .rank = spider::Rank::eight, .face_up = true},
            {.suit = spider::Suit::spades, .rank = spider::Rank::seven, .face_up = true},
            {.suit = spider::Suit::spades, .rank = spider::Rank::six, .face_up = true},
            {.suit = spider::Suit::spades, .rank = spider::Rank::five, .face_up = true},
            {.suit = spider::Suit::spades, .rank = spider::Rank::four, .face_up = true},
            {.suit = spider::Suit::spades, .rank = spider::Rank::three, .face_up = true},
            {.suit = spider::Suit::spades, .rank = spider::Rank::two, .face_up = true},
        };

        std::vector<std::array<spider::Card, GameState::kStockRowSize>> stock_rows {
            std::array<spider::Card, GameState::kStockRowSize> {{
                {.suit = spider::Suit::spades, .rank = spider::Rank::ace, .face_up = true},
                {.suit = spider::Suit::hearts, .rank = spider::Rank::king, .face_up = true},
                {.suit = spider::Suit::hearts, .rank = spider::Rank::queen, .face_up = true},
                {.suit = spider::Suit::hearts, .rank = spider::Rank::jack, .face_up = true},
                {.suit = spider::Suit::hearts, .rank = spider::Rank::ten, .face_up = true},
                {.suit = spider::Suit::hearts, .rank = spider::Rank::nine, .face_up = true},
                {.suit = spider::Suit::hearts, .rank = spider::Rank::eight, .face_up = true},
                {.suit = spider::Suit::hearts, .rank = spider::Rank::seven, .face_up = true},
                {.suit = spider::Suit::hearts, .rank = spider::Rank::six, .face_up = true},
                {.suit = spider::Suit::hearts, .rank = spider::Rank::five, .face_up = true},
            }},
        };
        GameState state = GameState::create_restored_game(std::move(tableau), std::move(stock_rows), 0, 0, 11);

        std::vector<spider::CompletedRunEvent> completed_runs;
        expect(state.deal_from_stock(completed_runs), "stock deal should report completed runs");
        expect(completed_runs.size() == 1, "stock deal should emit one completed run when a dealt ace closes the stack");
        expect(completed_runs[0].stack_index == 0, "stock completion should point at the completed stack");
        expect(state.completed_run_suits().size() == 1 && state.completed_run_suits()[0] == spider::Suit::spades, "stock completion should preserve suit metadata");
    }

    {
        const std::array<std::size_t, 10> small_counts {5, 5, 5, 5, 4, 4, 4, 4, 4, 4};
        const std::array<std::size_t, 10> small_hidden {4, 4, 4, 4, 3, 3, 3, 3, 3, 3};
        const std::array<std::size_t, 10> tall_counts {22, 21, 23, 20, 22, 21, 24, 20, 21, 22};
        const std::array<std::size_t, 10> tall_hidden {7, 7, 8, 6, 7, 7, 8, 6, 7, 7};
        const auto squeezed = spider::compute_layout(560, 900, tall_counts, tall_hidden, 0.0F);

        const auto roomy = spider::compute_layout(1600, 900, small_counts, small_hidden, 0.0F);
        const auto tight = spider::compute_layout(950, 640, tall_counts, tall_hidden, 400.0F);
        const float roomy_top_row_spacer = roomy.top_row_slot_x[1] - (roomy.top_row_slot_x[0] + roomy.card_width);
        const float tight_top_row_spacer = tight.top_row_slot_x[1] - (tight.top_row_slot_x[0] + tight.card_width);

        expect(roomy.stack_x[1] > roomy.stack_x[0], "stack x positions should increase left to right");
        expect(tight.card_width < roomy.card_width, "card width should shrink on narrower windows");
        expect_near(roomy.top_controls_height, 900.0F / 16.0F, 0.01F, "roomy layout should size the controls band from window height");
        expect_near(tight.top_controls_height, 640.0F / 16.0F, 0.01F, "tight layout should size the controls band from window height");
        expect_near(roomy.card_height, 900.0F / 6.0F, 0.01F, "roomy layout should size cards from window height");
        expect_near(tight.card_height, 640.0F / 6.0F, 0.01F, "tight layout should size cards from window height");
        expect_near(squeezed.card_height, 900.0F / 6.0F, 0.01F, "squeezed layout should keep the requested card height");
        expect_near(roomy.top_controls_bottom, roomy.top_controls_height, 0.01F, "roomy layout should align the controls bottom with the controls height");
        expect_near(tight.top_controls_bottom, tight.top_controls_height, 0.01F, "tight layout should align the controls bottom with the controls height");
        expect(roomy.top_row_y >= roomy.top_controls_bottom, "top row should start below the controls band");
        expect(roomy.top_row_bottom <= roomy.playfield_top, "top row should fit before the playfield begins");
        expect(tight.top_row_y >= tight.top_controls_bottom, "tight layout should keep the top row below controls");
        expect(tight.top_row_bottom <= tight.playfield_top, "tight layout should keep the top row above the playfield");
        expect_near(roomy.top_row_y - roomy.top_controls_bottom, roomy.top_row_gap, 0.01F, "roomy layout should reserve the configured gap above the top row");
        expect_near(roomy.top_row_bottom - roomy.top_row_y, roomy.card_height, 0.01F, "roomy top row should reuse the card height");
        expect_near(tight.top_row_y - tight.top_controls_bottom, tight.top_row_gap, 0.01F, "tight layout should reserve the configured gap above the top row");
        expect_near(tight.top_row_bottom - tight.top_row_y, tight.card_height, 0.01F, "tight top row should reuse the card height");
        expect_near(roomy.top_row_slot_x[0], roomy.outer_margin, 0.01F, "roomy layout should keep the stock pile left-aligned");
        expect_near(tight.top_row_slot_x[0], tight.outer_margin, 0.01F, "tight layout should keep the stock pile left-aligned");
        expect_near(roomy.top_row_slot_x[8] + roomy.card_width, 1600.0F - roomy.outer_margin, 0.01F, "roomy layout should right-align the last completed pile slot");
        expect_near(tight.top_row_slot_x[8] + tight.card_width, 950.0F - tight.outer_margin, 0.01F, "tight layout should right-align the last completed pile slot");
        expect_near(roomy.top_row_slot_x[2] - roomy.top_row_slot_x[1], roomy.card_width + roomy.stack_gap, 0.01F, "roomy completed pile slots should keep the configured spacing");
        expect_near(tight.top_row_slot_x[2] - tight.top_row_slot_x[1], tight.card_width + tight.stack_gap, 0.01F, "tight completed pile slots should keep the configured spacing");
        expect(roomy_top_row_spacer > tight_top_row_spacer, "wider windows should grow the empty top-row spacer");
        expect(tight_top_row_spacer >= tight.stack_gap - 0.01F, "tight layouts should still leave a gap between stock and completed piles");
        expect_near(roomy.playfield_top - roomy.top_row_bottom, roomy.top_row_gap, 0.01F, "roomy layout should reserve the configured gap below the top row");
        expect_near(tight.playfield_top - tight.top_row_bottom, tight.top_row_gap, 0.01F, "tight layout should reserve the configured gap below the top row");
        expect_near(roomy.stack_vertical_offset, roomy.card_height / 6.0F, 0.01F, "roomy layout should derive stack spacing from card height");
        expect_near(tight.stack_vertical_offset, tight.card_height / 6.0F, 0.01F, "tight layout should derive stack spacing from card height");
        expect_near(roomy.card_width, roomy.card_height / 1.45F, 0.01F, "roomy layout should preserve the existing card aspect ratio when width allows it");
        expect_near(tight.card_width, tight.card_height / 1.45F, 0.01F, "tight layout should preserve the existing card aspect ratio when width allows it");
        expect(squeezed.card_width < squeezed.card_height / 1.45F, "squeezed layout should compress card width to keep the tableau visible");
        const float roomy_tableau_width = roomy.card_width * 10.0F + roomy.stack_gap * 9.0F;
        const float tight_tableau_width = tight.card_width * 10.0F + tight.stack_gap * 9.0F;
        expect_near(
            roomy.stack_x[0],
            roomy.outer_margin + ((1600.0F - roomy.outer_margin * 2.0F) - roomy_tableau_width) / 2.0F,
            0.01F,
            "roomy layout should center the tableau band");
        expect_near(
            tight.stack_x[0],
            tight.outer_margin + ((950.0F - tight.outer_margin * 2.0F) - tight_tableau_width) / 2.0F,
            0.01F,
            "tight layout should center the tableau band");
        expect_near(
            roomy.stack_x[9] + roomy.card_width,
            1600.0F - roomy.stack_x[0],
            0.01F,
            "roomy layout should balance left and right tableau margins");
        expect_near(
            tight.stack_x[9] + tight.card_width,
            950.0F - tight.stack_x[0],
            0.01F,
            "tight layout should balance left and right tableau margins");
        expect(tight.stack_vertical_offset < roomy.stack_vertical_offset, "stack spacing should recompute when resize changes card size");
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

    {
        const auto save_path = test_save_path("round-trip-session.txt");
        reset_test_save(save_path);

        spider::GameSession session = spider::GameSession::create_new_game(55);
        expect(session.deal_from_stock(), "first stock deal should succeed for persistence");
        expect(session.deal_from_stock(), "second stock deal should succeed for persistence");
        expect(session.undo(), "undo should succeed before persistence round trip");

        spider::save_last_session(save_path, session);
        const auto load_result = spider::load_last_session(save_path);
        expect(load_result.status == spider::LoadLastSessionStatus::loaded, "saved session should load successfully");
        expect(load_result.session.has_value(), "saved session should contain a restored session");
        expect_same_session(*load_result.session, session, "loaded session should match the saved session");

        spider::clear_last_session(save_path);
    }

    {
        const auto save_path = test_save_path("completed-session.txt");
        reset_test_save(save_path);

        spider::GameState finished = spider::GameState::create_restored_game({}, {}, spider::GameState::kWinningCompletedRuns, 88, 1234);
        spider::save_last_session(save_path, spider::GameSession::create_restored_session(finished, {}, {}));

        spider::GameSession resumed = spider::resume_or_create_session(save_path, 9999);
        expect(resumed.state().seed() == 9999, "completed saved games should start a fresh deal");
        expect(!resumed.state().has_won(), "fresh deal should not start in a won state");
        expect(spider::load_last_session(save_path).status == spider::LoadLastSessionStatus::not_found, "completed save should be cleared after restart fallback");
    }

    {
        const auto save_path = test_save_path("completed-run-round-trip.txt");
        reset_test_save(save_path);

        std::array<std::vector<spider::Card>, GameState::kTableauStacks> tableau {};
        GameState state = GameState::create_restored_game(
            std::move(tableau),
            {},
            GameState::kWinningCompletedRuns,
            19,
            2468,
            {
                spider::Suit::spades,
                spider::Suit::spades,
                spider::Suit::spades,
                spider::Suit::spades,
                spider::Suit::hearts,
                spider::Suit::hearts,
                spider::Suit::hearts,
                spider::Suit::hearts,
            });
        spider::GameSession session = spider::GameSession::create_restored_session(state, {}, {});

        spider::save_last_session(save_path, session);
        const auto load_result = spider::load_last_session(save_path);
        expect(load_result.status == spider::LoadLastSessionStatus::loaded, "completed-run session should load successfully");
        expect(load_result.session.has_value(), "completed-run session should restore a session");
        expect_same_session(*load_result.session, session, "completed-run suit metadata should survive a persistence round trip");
    }

    {
        const auto save_path = test_save_path("invalid-session.txt");
        reset_test_save(save_path);

        std::ofstream output(save_path, std::ios::trunc);
        expect(static_cast<bool>(output), "test should be able to write an invalid save file");
        output << "not a valid spider save";
        output.close();

        spider::GameSession resumed = spider::resume_or_create_session(save_path, 4321);
        expect(resumed.state().seed() == 4321, "invalid saves should fall back to a fresh deal");
        expect(spider::load_last_session(save_path).status == spider::LoadLastSessionStatus::not_found, "invalid save should be cleared after fallback");
    }

    {
        const auto save_path = test_save_path("legacy-v1-session.txt");
        reset_test_save(save_path);

        spider::GameSession session = spider::GameSession::create_new_game(77);
        spider::save_last_session(save_path, session);

        std::ifstream input(save_path);
        expect(static_cast<bool>(input), "test should be able to read a saved session");
        std::string save_text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        input.close();
        expect(save_text.rfind("SPIDER_SESSION_V3\n", 0) == 0, "saved sessions should use the current persistence header");

        save_text.replace(0, std::string("SPIDER_SESSION_V3").size(), "SPIDER_SESSION_V1");

        std::ofstream output(save_path, std::ios::trunc);
        expect(static_cast<bool>(output), "test should be able to overwrite a saved session");
        output << save_text;
        output.close();

        spider::GameSession resumed = spider::resume_or_create_session(save_path, 9876);
        expect(resumed.state().seed() == 9876, "legacy v1 saves should fall back to a fresh deal");
        expect(spider::load_last_session(save_path).status == spider::LoadLastSessionStatus::not_found, "legacy v1 saves should be cleared after fallback");
    }

    return 0;
}
