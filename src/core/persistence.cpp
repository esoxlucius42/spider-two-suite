#include "spider/persistence.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <system_error>

namespace spider {

namespace {

constexpr std::string_view kCurrentFormatHeader = "SPIDER_SESSION_V2";
constexpr std::string_view kLegacyFormatHeader = "SPIDER_SESSION_V1";

auto card_identifier_index(const Card& card) -> std::size_t
{
    return (static_cast<std::size_t>(card.suit) * kRanks.size() + static_cast<std::size_t>(rank_value(card.rank) - 1U)) * 4U
        + static_cast<std::size_t>(card.copy_index);
}

auto is_valid_card(const Card& card) -> bool
{
    const auto suit_value = static_cast<std::uint8_t>(card.suit);
    const auto rank_number = static_cast<std::uint8_t>(card.rank);
    return suit_value < kPlayableSuits.size()
        && rank_number >= static_cast<std::uint8_t>(Rank::ace)
        && rank_number <= static_cast<std::uint8_t>(Rank::king)
        && card.copy_index < 4;
}

auto validate_state(const GameState& state, std::string& error) -> bool
{
    if (state.completed_runs() > GameState::kWinningCompletedRuns) {
        error = "completed run count exceeds the winning total";
        return false;
    }

    if (state.stock_rows().size() > GameState::kStockRows) {
        error = "stock row count exceeds the maximum";
        return false;
    }

    std::array<bool, kPlayableSuits.size() * kRanks.size() * 4> seen {};
    std::size_t live_cards = 0;

    for (const auto& stack : state.tableau()) {
        live_cards += stack.size();
        for (const Card& card : stack) {
            if (!is_valid_card(card)) {
                error = "tableau contains an invalid card";
                return false;
            }

            const std::size_t identifier = card_identifier_index(card);
            if (seen[identifier]) {
                error = "tableau contains a duplicate live card";
                return false;
            }
            seen[identifier] = true;
        }
    }

    for (const auto& row : state.stock_rows()) {
        live_cards += row.size();
        for (const Card& card : row) {
            if (!is_valid_card(card)) {
                error = "stock contains an invalid card";
                return false;
            }

            const std::size_t identifier = card_identifier_index(card);
            if (seen[identifier]) {
                error = "stock contains a duplicate live card";
                return false;
            }
            seen[identifier] = true;
        }
    }

    const std::size_t total_cards = live_cards + state.completed_runs() * GameState::kCompletedRunLength;
    if (total_cards != 104) {
        error = "live card count does not match a two-suit Spider deck";
        return false;
    }

    return true;
}

auto write_card(std::ostream& output, const Card& card) -> void
{
    output << "CARD "
           << static_cast<unsigned int>(static_cast<std::uint8_t>(card.suit)) << ' '
           << static_cast<unsigned int>(static_cast<std::uint8_t>(card.rank)) << ' '
           << (card.face_up ? 1 : 0) << ' '
           << static_cast<unsigned int>(card.copy_index) << '\n';
}

auto write_state(std::ostream& output, const GameState& state) -> void
{
    output << "STATE_BEGIN\n";
    output << "SEED " << state.seed() << '\n';
    output << "COMPLETED_RUNS " << state.completed_runs() << '\n';
    output << "MOVE_COUNT " << state.move_count() << '\n';
    output << "TABLEAU_STACKS " << GameState::kTableauStacks << '\n';
    for (std::size_t stack_index = 0; stack_index < GameState::kTableauStacks; ++stack_index) {
        const auto& stack = state.tableau()[stack_index];
        output << "STACK " << stack_index << ' ' << stack.size() << '\n';
        for (const Card& card : stack) {
            write_card(output, card);
        }
    }

    output << "STOCK_ROWS " << state.stock_rows().size() << '\n';
    for (std::size_t row_index = 0; row_index < state.stock_rows().size(); ++row_index) {
        output << "ROW " << row_index << '\n';
        for (const Card& card : state.stock_rows()[row_index]) {
            write_card(output, card);
        }
    }
    output << "STATE_END\n";
}

auto write_session(std::ostream& output, const GameSession& session) -> void
{
    output << kCurrentFormatHeader << '\n';
    output << "CURRENT_STATE\n";
    write_state(output, session.state());
    output << "UNDO_COUNT " << session.undo_history().size() << '\n';
    for (const GameState& state : session.undo_history()) {
        write_state(output, state);
    }
    output << "REDO_COUNT " << session.redo_history().size() << '\n';
    for (const GameState& state : session.redo_history()) {
        write_state(output, state);
    }
    output << "SESSION_END\n";
}

auto read_label(std::istream& input, std::string_view expected, std::string& error) -> bool
{
    std::string actual;
    if (!(input >> actual)) {
        error = "unexpected end of save data";
        return false;
    }

    if (actual != expected) {
        error = "expected label '" + std::string(expected) + "' but found '" + actual + "'";
        return false;
    }

    return true;
}

template <typename T>
auto read_value(std::istream& input, T& value, std::string& error, std::string_view what) -> bool
{
    if (!(input >> value)) {
        error = "expected " + std::string(what);
        return false;
    }
    return true;
}

auto read_card(std::istream& input, Card& card, std::string& error) -> bool
{
    if (!read_label(input, "CARD", error)) {
        return false;
    }

    unsigned int suit_value = 0;
    unsigned int rank_value = 0;
    unsigned int face_up_value = 0;
    unsigned int copy_value = 0;

    if (!read_value(input, suit_value, error, "card suit")
        || !read_value(input, rank_value, error, "card rank")
        || !read_value(input, face_up_value, error, "card face-up flag")
        || !read_value(input, copy_value, error, "card copy index")) {
        return false;
    }

    if (face_up_value > 1) {
        error = "card face-up flag must be 0 or 1";
        return false;
    }

    card = Card {
        .suit = static_cast<Suit>(static_cast<std::uint8_t>(suit_value)),
        .rank = static_cast<Rank>(static_cast<std::uint8_t>(rank_value)),
        .face_up = face_up_value == 1,
        .copy_index = static_cast<std::uint8_t>(copy_value),
    };

    if (!is_valid_card(card)) {
        error = "card fields are out of range";
        return false;
    }

    return true;
}

auto read_state(std::istream& input, std::string& error) -> std::optional<GameState>
{
    if (!read_label(input, "STATE_BEGIN", error)) {
        return std::nullopt;
    }

    std::uint64_t seed = 0;
    std::size_t completed_runs = 0;
    std::size_t move_count = 0;
    std::size_t tableau_count = 0;

    if (!read_label(input, "SEED", error) || !read_value(input, seed, error, "seed")
        || !read_label(input, "COMPLETED_RUNS", error) || !read_value(input, completed_runs, error, "completed run count")
        || !read_label(input, "MOVE_COUNT", error) || !read_value(input, move_count, error, "move count")
        || !read_label(input, "TABLEAU_STACKS", error) || !read_value(input, tableau_count, error, "tableau stack count")) {
        return std::nullopt;
    }

    if (tableau_count != GameState::kTableauStacks) {
        error = "save data contains an unexpected tableau stack count";
        return std::nullopt;
    }

    std::array<std::vector<Card>, GameState::kTableauStacks> tableau {};
    for (std::size_t stack_index = 0; stack_index < GameState::kTableauStacks; ++stack_index) {
        std::size_t serialized_stack_index = 0;
        std::size_t card_count = 0;
        if (!read_label(input, "STACK", error)
            || !read_value(input, serialized_stack_index, error, "stack index")
            || !read_value(input, card_count, error, "stack card count")) {
            return std::nullopt;
        }

        if (serialized_stack_index != stack_index) {
            error = "save data stack ordering is invalid";
            return std::nullopt;
        }

        auto& stack = tableau[stack_index];
        stack.reserve(card_count);
        for (std::size_t card_index = 0; card_index < card_count; ++card_index) {
            Card card;
            if (!read_card(input, card, error)) {
                return std::nullopt;
            }
            stack.push_back(card);
        }
    }

    std::size_t stock_row_count = 0;
    if (!read_label(input, "STOCK_ROWS", error) || !read_value(input, stock_row_count, error, "stock row count")) {
        return std::nullopt;
    }

    std::vector<std::array<Card, GameState::kStockRowSize>> stock_rows;
    stock_rows.reserve(stock_row_count);
    for (std::size_t row_index = 0; row_index < stock_row_count; ++row_index) {
        std::size_t serialized_row_index = 0;
        if (!read_label(input, "ROW", error) || !read_value(input, serialized_row_index, error, "stock row index")) {
            return std::nullopt;
        }

        if (serialized_row_index != row_index) {
            error = "save data stock row ordering is invalid";
            return std::nullopt;
        }

        std::array<Card, GameState::kStockRowSize> row {};
        for (Card& card : row) {
            if (!read_card(input, card, error)) {
                return std::nullopt;
            }
        }
        stock_rows.push_back(row);
    }

    if (!read_label(input, "STATE_END", error)) {
        return std::nullopt;
    }

    GameState state = GameState::create_restored_game(std::move(tableau), std::move(stock_rows), completed_runs, move_count, seed);
    if (!validate_state(state, error)) {
        return std::nullopt;
    }

    return state;
}

auto parse_session(std::string_view text, std::string& error) -> std::optional<GameSession>
{
    std::istringstream input {std::string(text)};

    std::string format_header;
    if (!(input >> format_header)) {
        error = "unexpected end of save data";
        return std::nullopt;
    }
    if (format_header == kLegacyFormatHeader) {
        error = "save format v1 is no longer supported after the opening deal change";
        return std::nullopt;
    }
    if (format_header != kCurrentFormatHeader) {
        error = "expected supported save format header";
        return std::nullopt;
    }

    if (!read_label(input, "CURRENT_STATE", error)) {
        return std::nullopt;
    }

    const auto current_state = read_state(input, error);
    if (!current_state.has_value()) {
        return std::nullopt;
    }

    std::size_t undo_count = 0;
    if (!read_label(input, "UNDO_COUNT", error) || !read_value(input, undo_count, error, "undo history count")) {
        return std::nullopt;
    }

    std::vector<GameState> undo_history;
    undo_history.reserve(undo_count);
    for (std::size_t index = 0; index < undo_count; ++index) {
        const auto state = read_state(input, error);
        if (!state.has_value()) {
            return std::nullopt;
        }
        undo_history.push_back(*state);
    }

    std::size_t redo_count = 0;
    if (!read_label(input, "REDO_COUNT", error) || !read_value(input, redo_count, error, "redo history count")) {
        return std::nullopt;
    }

    std::vector<GameState> redo_history;
    redo_history.reserve(redo_count);
    for (std::size_t index = 0; index < redo_count; ++index) {
        const auto state = read_state(input, error);
        if (!state.has_value()) {
            return std::nullopt;
        }
        redo_history.push_back(*state);
    }

    if (!read_label(input, "SESSION_END", error)) {
        return std::nullopt;
    }

    std::string trailing;
    if (input >> trailing) {
        error = "save data contains unexpected trailing content";
        return std::nullopt;
    }

    return GameSession::create_restored_session(*current_state, std::move(undo_history), std::move(redo_history));
}

auto determine_last_session_path() -> std::filesystem::path
{
    if (const char* override_path = std::getenv("SPIDER_LAST_SESSION_PATH"); override_path != nullptr && override_path[0] != '\0') {
        return override_path;
    }

    if (const char* xdg_state_home = std::getenv("XDG_STATE_HOME"); xdg_state_home != nullptr && xdg_state_home[0] != '\0') {
        return std::filesystem::path(xdg_state_home) / "spider-two-suite" / "last-session.txt";
    }

    if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
        return std::filesystem::path(home) / ".local" / "state" / "spider-two-suite" / "last-session.txt";
    }

    throw std::runtime_error("unable to determine a save-file location");
}

} // namespace

auto default_last_session_path() -> std::filesystem::path
{
    return determine_last_session_path();
}

auto load_last_session() -> LoadLastSessionResult
{
    return load_last_session(default_last_session_path());
}

auto load_last_session(const std::filesystem::path& path) -> LoadLastSessionResult
{
    std::error_code exists_error;
    const bool exists = std::filesystem::exists(path, exists_error);
    if (exists_error) {
        throw std::runtime_error("failed to inspect save file '" + path.string() + "': " + exists_error.message());
    }

    if (!exists) {
        return {};
    }

    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("failed to open save file '" + path.string() + "'");
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    if (!input.good() && !input.eof()) {
        throw std::runtime_error("failed while reading save file '" + path.string() + "'");
    }

    std::string error;
    const auto session = parse_session(buffer.str(), error);
    if (!session.has_value()) {
        return LoadLastSessionResult {
            .status = LoadLastSessionStatus::invalid_data,
            .session = std::nullopt,
            .message = error,
        };
    }

    return LoadLastSessionResult {
        .status = LoadLastSessionStatus::loaded,
        .session = session,
        .message = {},
    };
}

auto resume_or_create_session(std::uint64_t new_game_seed) -> GameSession
{
    return resume_or_create_session(default_last_session_path(), new_game_seed);
}

auto resume_or_create_session(const std::filesystem::path& path, std::uint64_t new_game_seed) -> GameSession
{
    const LoadLastSessionResult load_result = load_last_session(path);
    if (load_result.status == LoadLastSessionStatus::loaded && load_result.session.has_value()) {
        if (!load_result.session->state().has_won()) {
            return *load_result.session;
        }

        clear_last_session(path);
        return GameSession::create_new_game(new_game_seed);
    }

    if (load_result.status == LoadLastSessionStatus::invalid_data) {
        std::fprintf(stderr, "ignoring invalid save file '%s': %s\n", path.string().c_str(), load_result.message.c_str());
        clear_last_session(path);
    }

    return GameSession::create_new_game(new_game_seed);
}

void save_last_session(const GameSession& session)
{
    save_last_session(default_last_session_path(), session);
}

void save_last_session(const std::filesystem::path& path, const GameSession& session)
{
    std::error_code directory_error;
    const auto parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, directory_error);
        if (directory_error) {
            throw std::runtime_error("failed to create save directory '" + parent.string() + "': " + directory_error.message());
        }
    }

    const std::filesystem::path temporary_path = path.string() + ".tmp";
    {
        std::ofstream output(temporary_path, std::ios::trunc);
        if (!output) {
            throw std::runtime_error("failed to open temporary save file '" + temporary_path.string() + "'");
        }

        write_session(output, session);
        output.flush();
        if (!output) {
            throw std::runtime_error("failed to write temporary save file '" + temporary_path.string() + "'");
        }
    }

    std::error_code remove_error;
    std::filesystem::remove(path, remove_error);
    if (remove_error) {
        std::filesystem::remove(temporary_path);
        throw std::runtime_error("failed to replace save file '" + path.string() + "': " + remove_error.message());
    }

    std::error_code rename_error;
    std::filesystem::rename(temporary_path, path, rename_error);
    if (rename_error) {
        std::filesystem::remove(temporary_path);
        throw std::runtime_error("failed to finalize save file '" + path.string() + "': " + rename_error.message());
    }
}

void clear_last_session()
{
    clear_last_session(default_last_session_path());
}

void clear_last_session(const std::filesystem::path& path)
{
    std::error_code exists_error;
    const bool exists = std::filesystem::exists(path, exists_error);
    if (exists_error) {
        throw std::runtime_error("failed to inspect save file '" + path.string() + "': " + exists_error.message());
    }

    if (!exists) {
        return;
    }

    std::error_code remove_error;
    const bool removed = std::filesystem::remove(path, remove_error);
    if (remove_error) {
        throw std::runtime_error("failed to remove save file '" + path.string() + "': " + remove_error.message());
    }

    if (!removed) {
        throw std::runtime_error("failed to remove save file '" + path.string() + "'");
    }
}

} // namespace spider
