#include "spider/game_session.hpp"

namespace spider {

auto GameSession::create_new_game(std::uint64_t seed) -> GameSession
{
    GameSession session;
    session.state_ = GameState::create_new_game(seed);
    return session;
}

auto GameSession::create_restored_session(
    GameState state,
    std::vector<GameState> undo_history,
    std::vector<GameState> redo_history) -> GameSession
{
    GameSession session;
    session.state_ = std::move(state);
    session.undo_history_ = std::move(undo_history);
    session.redo_history_ = std::move(redo_history);
    return session;
}

auto GameSession::state() const -> const GameState&
{
    return state_;
}

auto GameSession::undo_history() const -> const std::vector<GameState>&
{
    return undo_history_;
}

auto GameSession::redo_history() const -> const std::vector<GameState>&
{
    return redo_history_;
}

auto GameSession::can_move_sequence(const Move& move) const -> bool
{
    return state_.can_move_sequence(move);
}

auto GameSession::move_sequence(const Move& move) -> bool
{
    std::vector<CompletedRunEvent> completed_runs;
    return move_sequence(move, completed_runs);
}

auto GameSession::move_sequence(const Move& move, std::vector<CompletedRunEvent>& completed_runs) -> bool
{
    if (!state_.can_move_sequence(move)) {
        return false;
    }

    record_state_before_change();
    return state_.move_sequence(move, completed_runs);
}

auto GameSession::can_deal_from_stock() const -> bool
{
    return state_.can_deal_from_stock();
}

auto GameSession::deal_from_stock() -> bool
{
    std::vector<CompletedRunEvent> completed_runs;
    return deal_from_stock(completed_runs);
}

auto GameSession::deal_from_stock(std::vector<CompletedRunEvent>& completed_runs) -> bool
{
    if (!state_.can_deal_from_stock()) {
        return false;
    }

    record_state_before_change();
    return state_.deal_from_stock(completed_runs);
}

auto GameSession::can_undo() const -> bool
{
    return !undo_history_.empty();
}

auto GameSession::can_redo() const -> bool
{
    return !redo_history_.empty();
}

auto GameSession::undo() -> bool
{
    if (undo_history_.empty()) {
        return false;
    }

    redo_history_.push_back(state_);
    state_ = undo_history_.back();
    undo_history_.pop_back();
    return true;
}

auto GameSession::redo() -> bool
{
    if (redo_history_.empty()) {
        return false;
    }

    undo_history_.push_back(state_);
    state_ = redo_history_.back();
    redo_history_.pop_back();
    return true;
}

void GameSession::restart()
{
    state_ = GameState::create_new_game(state_.seed());
    clear_history();
}

void GameSession::start_new_game(std::uint64_t seed)
{
    state_ = GameState::create_new_game(seed);
    clear_history();
}

void GameSession::record_state_before_change()
{
    undo_history_.push_back(state_);
    redo_history_.clear();
}

void GameSession::clear_history()
{
    undo_history_.clear();
    redo_history_.clear();
}

} // namespace spider
