#include "spider/card_sprite.hpp"
#include "spider/game_session.hpp"
#include "spider/layout.hpp"
#include "spider/persistence.hpp"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace {

struct AtlasCell {
    float x {};
    float y {};
    float w {};
    float h {};
};

struct Selection {
    std::size_t stack_index {};
    std::size_t start_index {};
};

struct DragState {
    Selection selection {};
    float grab_offset_x {};
    float grab_offset_y {};
    float mouse_x {};
    float mouse_y {};
};

struct Button {
    enum class Action {
        new_game,
        restart,
        undo,
        redo,
        confirm_yes,
        confirm_no,
    };

    Action action {};
    std::string_view label;
    SDL_FRect bounds {};
};

enum class ConfirmationAction {
    new_game,
    restart,
};

using TableauCards = std::array<std::vector<spider::Card>, spider::GameState::kTableauStacks>;

struct OpeningDealAnimation {
    spider::GameSession pending_session {};
    std::vector<spider::GameState::OpeningDealStep> steps {};
    float elapsed_seconds {0.0F};
};

struct StockDealStep {
    spider::Card card {};
    std::size_t stack_index {};
    std::size_t card_index {};
};

struct StockDealAnimation {
    spider::GameSession pending_session {};
    std::vector<StockDealStep> steps {};
    std::vector<spider::CompletedRunEvent> completed_runs {};
    float elapsed_seconds {0.0F};
};

struct AutoMoveAnimation {
    spider::GameSession pending_session {};
    spider::Move move {};
    std::vector<spider::Card> cards {};
    std::vector<spider::CompletedRunEvent> completed_runs {};
    std::size_t destination_card_index {};
    float elapsed_seconds {0.0F};
};

struct CompletedRunFlight {
    spider::CompletedRunEvent run {};
    std::size_t target_pile_index {};
};

struct CompletedRunAnimation {
    std::vector<CompletedRunFlight> flights {};
    float elapsed_seconds {0.0F};
};

constexpr std::array<int, 14> kAtlasX {0, 167, 334, 502, 669, 837, 1004, 1172, 1339, 1507, 1674, 1842, 2009, 2179};
constexpr std::array<int, 6> kAtlasY {0, 243, 487, 730, 972, 1216};
constexpr std::uint64_t kInitialSeed = 20260315ULL;
constexpr float kOpeningDealCardStaggerSeconds = 0.05F;
constexpr float kOpeningDealCardFlightSeconds = 0.28F;
constexpr float kOpeningDealArcHeight = 28.0F;
constexpr float kStockDealCardStaggerSeconds = 0.06F;
constexpr float kStockDealCardFlightSeconds = 0.24F;
constexpr float kStockDealArcHeight = 24.0F;
constexpr float kAutoMoveFlightSeconds = 0.22F;
constexpr float kAutoMoveArcHeight = 20.0F;
constexpr float kCompletedRunCardStaggerSeconds = 0.04F;
constexpr float kCompletedRunFlightSeconds = 0.34F;
constexpr float kCompletedRunArcHeight = 26.0F;
constexpr int kOpeningDeckCardsPerLayer = 10;
constexpr int kMaximumDeckLayers = 8;

auto create_startup_session() -> spider::GameSession
{
    return spider::resume_or_create_session(kInitialSeed);
}

auto atlas_cell(int row, int column) -> AtlasCell
{
    return AtlasCell {
        .x = static_cast<float>(kAtlasX[column]),
        .y = static_cast<float>(kAtlasY[row]),
        .w = static_cast<float>(kAtlasX[column + 1] - kAtlasX[column]),
        .h = static_cast<float>(kAtlasY[row + 1] - kAtlasY[row]),
    };
}

auto atlas_for_card(const spider::Card& card) -> AtlasCell
{
    const int row = spider::atlas_row_for_suit(card.suit);
    const int column = spider::rank_value(card.rank) - 1;
    return atlas_cell(row, column);
}

auto atlas_for_card_back() -> AtlasCell
{
    return atlas_cell(4, 2);
}

auto point_in_rect(float x, float y, const SDL_FRect& rect) -> bool
{
    return x >= rect.x && x <= rect.x + rect.w && y >= rect.y && y <= rect.y + rect.h;
}

auto lerp(float start, float end, float progress) -> float
{
    return start + (end - start) * progress;
}

auto smoothstep(float progress) -> float
{
    const float clamped = std::clamp(progress, 0.0F, 1.0F);
    return clamped * clamped * (3.0F - 2.0F * clamped);
}

auto completed_pile_cards(spider::Suit suit) -> std::array<spider::Card, spider::GameState::kCompletedRunLength>
{
    std::array<spider::Card, spider::GameState::kCompletedRunLength> cards {};
    for (std::size_t index = 0; index < cards.size(); ++index) {
        cards[index] = spider::Card {
            .suit = suit,
            .rank = static_cast<spider::Rank>(static_cast<int>(spider::Rank::ace) + static_cast<int>(index)),
            .face_up = true,
        };
    }
    return cards;
}

auto glyph_rows(char glyph) -> std::array<std::uint8_t, 7>
{
    switch (static_cast<char>(std::toupper(static_cast<unsigned char>(glyph)))) {
    case 'A':
        return {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
    case 'C':
        return {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E};
    case 'D':
        return {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E};
    case 'E':
        return {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F};
    case 'G':
        return {0x0E, 0x11, 0x10, 0x16, 0x11, 0x11, 0x0E};
    case 'I':
        return {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F};
    case 'K':
        return {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11};
    case 'L':
        return {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F};
    case 'M':
        return {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11};
    case 'N':
        return {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11};
    case 'O':
        return {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
    case 'R':
        return {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11};
    case 'S':
        return {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E};
    case 'T':
        return {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};
    case 'U':
        return {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
    case 'V':
        return {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04};
    case 'W':
        return {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A};
    case 'Y':
        return {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04};
    case '0':
        return {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E};
    case '1':
        return {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E};
    case '2':
        return {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F};
    case '3':
        return {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E};
    case '4':
        return {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02};
    case '5':
        return {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E};
    case '6':
        return {0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E};
    case '7':
        return {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08};
    case '8':
        return {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E};
    case '9':
        return {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E};
    case ' ':
        return {0, 0, 0, 0, 0, 0, 0};
    default:
        return {0, 0, 0, 0, 0, 0, 0};
    }
}

void draw_text(SDL_Renderer* renderer, std::string_view text, float x, float y, float scale, SDL_Color color)
{
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);

    float cursor_x = x;
    for (char glyph : text) {
        const auto rows = glyph_rows(glyph);
        for (int row = 0; row < 7; ++row) {
            for (int col = 0; col < 5; ++col) {
                if ((rows[row] & (1 << (4 - col))) == 0) {
                    continue;
                }

                const SDL_FRect pixel {
                    cursor_x + static_cast<float>(col) * scale,
                    y + static_cast<float>(row) * scale,
                    scale,
                    scale,
                };
                SDL_RenderFillRect(renderer, &pixel);
            }
        }
        cursor_x += 6.0F * scale;
    }
}

class App {
public:
    App()
        : session_(create_startup_session())
    {
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            throw std::runtime_error(SDL_GetError());
        }

        window_ = SDL_CreateWindow("Spider Two Suites", 1440, 900, SDL_WINDOW_RESIZABLE);
        if (window_ == nullptr) {
            throw std::runtime_error(SDL_GetError());
        }

        renderer_ = SDL_CreateRenderer(window_, nullptr);
        if (renderer_ == nullptr) {
            throw std::runtime_error(SDL_GetError());
        }

        cards_texture_ = IMG_LoadTexture(renderer_, SPIDER_ASSET_ROOT "/cards.png");
        if (cards_texture_ == nullptr) {
            throw std::runtime_error(SDL_GetError());
        }
    }

    ~App()
    {
        if (cards_texture_ != nullptr) {
            SDL_DestroyTexture(cards_texture_);
        }
        if (renderer_ != nullptr) {
            SDL_DestroyRenderer(renderer_);
        }
        if (window_ != nullptr) {
            SDL_DestroyWindow(window_);
        }
        SDL_Quit();
    }

    auto run() -> int
    {
        using clock = std::chrono::steady_clock;
        auto previous_frame_time = clock::now();

        while (running_) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                handle_event(event);
            }

            const auto current_frame_time = clock::now();
            const float delta_seconds = std::chrono::duration<float>(current_frame_time - previous_frame_time).count();
            previous_frame_time = current_frame_time;

            update_opening_deal_animation(delta_seconds);
            update_stock_deal_animation(delta_seconds);
            update_auto_move_animation(delta_seconds);
            update_completed_run_animation(delta_seconds);
            render();
            SDL_Delay(16);
        }

        return 0;
    }

private:
    SDL_Window* window_ {nullptr};
    SDL_Renderer* renderer_ {nullptr};
    SDL_Texture* cards_texture_ {nullptr};
    spider::GameSession session_;
    float scroll_offset_ {0.0F};
    std::optional<Selection> selection_;
    std::optional<Selection> pressed_selection_;
    std::optional<DragState> drag_state_;
    std::optional<std::size_t> hovered_stack_;
    std::optional<Button::Action> hovered_button_;
    std::optional<ConfirmationAction> pending_confirmation_;
    std::optional<OpeningDealAnimation> opening_deal_animation_;
    std::optional<StockDealAnimation> stock_deal_animation_;
    std::optional<AutoMoveAnimation> auto_move_animation_;
    std::optional<CompletedRunAnimation> completed_run_animation_;
    float press_x_ {0.0F};
    float press_y_ {0.0F};
    std::array<Button, 4> buttons_ {};
    std::array<Button, 2> dialog_buttons_ {};
    bool running_ {true};

    [[nodiscard]] auto opening_deal_active() const -> bool
    {
        return opening_deal_animation_.has_value();
    }

    [[nodiscard]] auto auto_move_active() const -> bool
    {
        return auto_move_animation_.has_value();
    }

    [[nodiscard]] auto stock_deal_active() const -> bool
    {
        return stock_deal_animation_.has_value();
    }

    [[nodiscard]] auto completed_run_active() const -> bool
    {
        return completed_run_animation_.has_value();
    }

    [[nodiscard]] auto state() const -> const spider::GameState&
    {
        return session_.state();
    }

    [[nodiscard]] auto animation_active() const -> bool
    {
        return opening_deal_active() || stock_deal_active() || auto_move_active() || completed_run_active();
    }

    [[nodiscard]] auto visual_state() const -> const spider::GameState&
    {
        if (opening_deal_active()) {
            return opening_deal_animation_->pending_session.state();
        }

        return state();
    }

    [[nodiscard]] auto opening_deal_start_time(std::size_t step_index) const -> float
    {
        return static_cast<float>(step_index) * kOpeningDealCardStaggerSeconds;
    }

    [[nodiscard]] auto opening_deal_finish_time(std::size_t step_index) const -> float
    {
        return opening_deal_start_time(step_index) + kOpeningDealCardFlightSeconds;
    }

    [[nodiscard]] auto opening_deal_total_duration() const -> float
    {
        if (!opening_deal_animation_.has_value() || opening_deal_animation_->steps.empty()) {
            return 0.0F;
        }

        return opening_deal_finish_time(opening_deal_animation_->steps.size() - 1);
    }

    [[nodiscard]] auto opening_deal_started_cards() const -> std::size_t
    {
        if (!opening_deal_animation_.has_value()) {
            return 0;
        }

        const auto& animation = *opening_deal_animation_;
        const std::size_t started = static_cast<std::size_t>(animation.elapsed_seconds / kOpeningDealCardStaggerSeconds) + 1;
        return std::min(started, animation.steps.size());
    }

    [[nodiscard]] auto opening_deck_card_count() const -> std::size_t
    {
        if (!opening_deal_animation_.has_value()) {
            return state().stock_rows_remaining() * spider::GameState::kStockRowSize;
        }

        return opening_deal_animation_->pending_session.state().stock_rows_remaining() * spider::GameState::kStockRowSize
            + opening_deal_animation_->steps.size()
            - opening_deal_started_cards();
    }

    [[nodiscard]] auto stock_deal_start_time(std::size_t step_index) const -> float
    {
        return static_cast<float>(step_index) * kStockDealCardStaggerSeconds;
    }

    [[nodiscard]] auto stock_deal_finish_time(std::size_t step_index) const -> float
    {
        return stock_deal_start_time(step_index) + kStockDealCardFlightSeconds;
    }

    [[nodiscard]] auto stock_deal_total_duration() const -> float
    {
        if (!stock_deal_animation_.has_value() || stock_deal_animation_->steps.empty()) {
            return 0.0F;
        }

        return stock_deal_finish_time(stock_deal_animation_->steps.size() - 1);
    }

    [[nodiscard]] auto stock_deal_started_cards() const -> std::size_t
    {
        if (!stock_deal_animation_.has_value()) {
            return 0;
        }

        const auto& animation = *stock_deal_animation_;
        const std::size_t started = static_cast<std::size_t>(animation.elapsed_seconds / kStockDealCardStaggerSeconds) + 1;
        return std::min(started, animation.steps.size());
    }

    [[nodiscard]] auto stock_deal_deck_card_count() const -> std::size_t
    {
        if (!stock_deal_animation_.has_value()) {
            return state().stock_rows_remaining() * spider::GameState::kStockRowSize;
        }

        return stock_deal_animation_->pending_session.state().stock_rows_remaining() * spider::GameState::kStockRowSize
            + stock_deal_animation_->steps.size()
            - stock_deal_started_cards();
    }

    [[nodiscard]] auto deck_layers_for_card_count(std::size_t card_count) const -> int
    {
        const std::size_t rounded_layers = std::max<std::size_t>(1, (card_count + static_cast<std::size_t>(kOpeningDeckCardsPerLayer) - 1) / static_cast<std::size_t>(kOpeningDeckCardsPerLayer));
        return std::min<int>(static_cast<int>(rounded_layers), kMaximumDeckLayers);
    }

    [[nodiscard]] auto completed_run_start_time(std::size_t card_index) const -> float
    {
        return static_cast<float>(card_index) * kCompletedRunCardStaggerSeconds;
    }

    [[nodiscard]] auto completed_run_finish_time(std::size_t card_index) const -> float
    {
        return completed_run_start_time(card_index) + kCompletedRunFlightSeconds;
    }

    [[nodiscard]] auto completed_run_total_duration() const -> float
    {
        return completed_run_finish_time(spider::GameState::kCompletedRunLength - 1);
    }

    auto current_window_size() const -> std::pair<int, int>
    {
        int width = 0;
        int height = 0;
        SDL_GetWindowSize(window_, &width, &height);
        return {width, height};
    }

    [[nodiscard]] auto can_move_selection_to(std::size_t destination_stack) const -> bool
    {
        if (!selection_.has_value()) {
            return false;
        }

        return session_.can_move_sequence(spider::Move {
            .from_stack = selection_->stack_index,
            .start_index = selection_->start_index,
            .to_stack = destination_stack,
        });
    }

    [[nodiscard]] auto current_stock_deal_tableau() const -> TableauCards
    {
        TableauCards tableau = state().tableau();
        if (!stock_deal_animation_.has_value()) {
            return tableau;
        }

        for (std::size_t step_index = 0; step_index < stock_deal_animation_->steps.size(); ++step_index) {
            if (stock_deal_animation_->elapsed_seconds < stock_deal_finish_time(step_index)) {
                break;
            }

            const auto& step = stock_deal_animation_->steps[step_index];
            tableau[step.stack_index].push_back(step.card);
        }

        return tableau;
    }

    auto try_auto_move_card(const Selection& card) -> bool
    {
        const auto move = state().find_auto_move(card.stack_index, card.start_index);
        if (!move.has_value()) {
            return false;
        }

        const auto& from_stack = state().tableau()[move->from_stack];
        if (move->start_index >= from_stack.size()) {
            return false;
        }

        spider::GameSession next_session = session_;
        std::vector<spider::CompletedRunEvent> completed_runs;
        if (!next_session.move_sequence(*move, completed_runs)) {
            return false;
        }

        auto_move_animation_ = AutoMoveAnimation {
            .pending_session = std::move(next_session),
            .move = *move,
            .cards = std::vector<spider::Card>(from_stack.begin() + static_cast<std::ptrdiff_t>(move->start_index), from_stack.end()),
            .completed_runs = std::move(completed_runs),
            .destination_card_index = state().tableau()[move->to_stack].size(),
            .elapsed_seconds = 0.0F,
        };

        clear_selection_state();
        hovered_button_.reset();
        hovered_stack_.reset();
        return true;
    }

    auto confirmation_panel_bounds(int window_width, int window_height) const -> SDL_FRect
    {
        const float panel_width = std::clamp(static_cast<float>(window_width) * 0.42F, 320.0F, 460.0F);
        const float panel_height = 184.0F;
        return SDL_FRect {
            (static_cast<float>(window_width) - panel_width) * 0.5F,
            (static_cast<float>(window_height) - panel_height) * 0.5F,
            panel_width,
            panel_height,
        };
    }

    auto top_row_slot_rect(const spider::LayoutMetrics& layout, std::size_t slot_index) const -> SDL_FRect
    {
        return SDL_FRect {
            layout.stack_x[slot_index],
            layout.top_row_y,
            layout.card_width,
            layout.card_height,
        };
    }

    void layout_ui(const spider::LayoutMetrics& layout, int window_width, int window_height)
    {
        const float top_y = layout.outer_margin + 7.0F;
        const float button_height = layout.top_controls_height - 20.0F;
        const float button_gap = std::clamp(layout.outer_margin * 0.35F, 10.0F, 18.0F);
        const float right_reserved = 120.0F;
        const float available_width = std::max(448.0F, static_cast<float>(window_width) - layout.outer_margin * 2.0F - right_reserved);
        const float button_width = std::clamp((available_width - button_gap * 3.0F) / 4.0F, 104.0F, 156.0F);

        buttons_[0] = Button {
            .action = Button::Action::new_game,
            .label = "NEW GAME",
            .bounds = SDL_FRect {layout.outer_margin, top_y, button_width, button_height},
        };
        buttons_[1] = Button {
            .action = Button::Action::restart,
            .label = "RESTART",
            .bounds = SDL_FRect {layout.outer_margin + (button_width + button_gap), top_y, button_width, button_height},
        };
        buttons_[2] = Button {
            .action = Button::Action::undo,
            .label = "UNDO",
            .bounds = SDL_FRect {layout.outer_margin + (button_width + button_gap) * 2.0F, top_y, button_width, button_height},
        };
        buttons_[3] = Button {
            .action = Button::Action::redo,
            .label = "REDO",
            .bounds = SDL_FRect {layout.outer_margin + (button_width + button_gap) * 3.0F, top_y, button_width, button_height},
        };

        const SDL_FRect panel = confirmation_panel_bounds(window_width, window_height);
        const float dialog_button_width = (panel.w - 52.0F) * 0.5F;
        const float dialog_button_y = panel.y + panel.h - 58.0F;
        dialog_buttons_[0] = Button {
            .action = Button::Action::confirm_yes,
            .label = "YES",
            .bounds = SDL_FRect {panel.x + 18.0F, dialog_button_y, dialog_button_width, 40.0F},
        };
        dialog_buttons_[1] = Button {
            .action = Button::Action::confirm_no,
            .label = "CANCEL",
            .bounds = SDL_FRect {panel.x + panel.w - dialog_button_width - 18.0F, dialog_button_y, dialog_button_width, 40.0F},
        };
    }

    [[nodiscard]] auto is_button_enabled(Button::Action action) const -> bool
    {
        if (animation_active()) {
            return false;
        }

        switch (action) {
        case Button::Action::new_game:
        case Button::Action::restart:
        case Button::Action::confirm_yes:
        case Button::Action::confirm_no:
            return true;
        case Button::Action::undo:
            return session_.can_undo();
        case Button::Action::redo:
            return session_.can_redo();
        }

        return false;
    }

    void clear_selection_state()
    {
        selection_.reset();
        pressed_selection_.reset();
        drag_state_.reset();
        hovered_stack_.reset();
    }

    void persist_session() const
    {
        spider::save_last_session(session_);
    }

    void reset_for_fresh_layout()
    {
        clear_selection_state();
        scroll_offset_ = 0.0F;
    }

    void open_confirmation(ConfirmationAction action)
    {
        pending_confirmation_ = action;
        clear_selection_state();
    }

    void cancel_confirmation()
    {
        pending_confirmation_.reset();
    }

    void update_hover_state(float x, float y)
    {
        if (animation_active()) {
            hovered_button_.reset();
            hovered_stack_.reset();
            return;
        }

        const auto [window_width, window_height] = current_window_size();
        const auto layout = layout_for_window(window_width, window_height);
        layout_ui(layout, window_width, window_height);

        hovered_button_.reset();
        hovered_stack_.reset();

        if (pending_confirmation_.has_value()) {
            for (const auto& button : dialog_buttons_) {
                if (point_in_rect(x, y, button.bounds)) {
                    hovered_button_ = button.action;
                    return;
                }
            }
            return;
        }

        for (const auto& button : buttons_) {
            if (point_in_rect(x, y, button.bounds)) {
                hovered_button_ = button.action;
                return;
            }
        }

        hovered_stack_ = hit_test_stack(x, y, layout);
    }

    void start_opening_deal_animation(spider::GameSession next_session)
    {
        const auto opening_deal = spider::GameState::build_opening_deal(next_session.state().seed());
        cancel_confirmation();
        reset_for_fresh_layout();
        hovered_button_.reset();
        hovered_stack_.reset();
        stock_deal_animation_.reset();
        auto_move_animation_.reset();
        completed_run_animation_.reset();
        opening_deal_animation_ = OpeningDealAnimation {
            .pending_session = std::move(next_session),
            .steps = opening_deal.tableau_steps,
            .elapsed_seconds = 0.0F,
        };
    }

    void update_opening_deal_animation(float delta_seconds)
    {
        if (!opening_deal_active()) {
            return;
        }

        opening_deal_animation_->elapsed_seconds += std::max(delta_seconds, 0.0F);
        if (opening_deal_animation_->elapsed_seconds < opening_deal_total_duration()) {
            return;
        }

        session_ = std::move(opening_deal_animation_->pending_session);
        opening_deal_animation_.reset();
        persist_session();
    }

    auto start_stock_deal_animation() -> bool
    {
        const auto& stock_rows = state().stock_rows();
        if (stock_rows.empty()) {
            return false;
        }

        spider::GameSession next_session = session_;
        std::vector<spider::CompletedRunEvent> completed_runs;
        if (!next_session.deal_from_stock(completed_runs)) {
            return false;
        }

        StockDealAnimation animation;
        animation.pending_session = std::move(next_session);
        animation.completed_runs = std::move(completed_runs);
        animation.steps.reserve(spider::GameState::kStockRowSize);
        for (std::size_t stack_index = 0; stack_index < spider::GameState::kTableauStacks; ++stack_index) {
            animation.steps.push_back(StockDealStep {
                .card = stock_rows.front()[stack_index],
                .stack_index = stack_index,
                .card_index = state().tableau()[stack_index].size(),
            });
        }

        clear_selection_state();
        cancel_confirmation();
        hovered_button_.reset();
        hovered_stack_.reset();
        auto_move_animation_.reset();
        completed_run_animation_.reset();
        stock_deal_animation_ = std::move(animation);
        return true;
    }

    void update_stock_deal_animation(float delta_seconds)
    {
        if (!stock_deal_active()) {
            return;
        }

        stock_deal_animation_->elapsed_seconds += std::max(delta_seconds, 0.0F);
        if (stock_deal_animation_->elapsed_seconds < stock_deal_total_duration()) {
            return;
        }

        auto completed_runs = std::move(stock_deal_animation_->completed_runs);
        session_ = std::move(stock_deal_animation_->pending_session);
        stock_deal_animation_.reset();
        persist_session();
        if (!completed_runs.empty()) {
            start_completed_run_animation(std::move(completed_runs));
        }
    }

    void update_auto_move_animation(float delta_seconds)
    {
        if (!auto_move_active()) {
            return;
        }

        auto_move_animation_->elapsed_seconds += std::max(delta_seconds, 0.0F);
        if (auto_move_animation_->elapsed_seconds < kAutoMoveFlightSeconds) {
            return;
        }

        auto completed_runs = std::move(auto_move_animation_->completed_runs);
        session_ = std::move(auto_move_animation_->pending_session);
        auto_move_animation_.reset();
        persist_session();
        if (!completed_runs.empty()) {
            start_completed_run_animation(std::move(completed_runs));
        }
    }

    void start_completed_run_animation(std::vector<spider::CompletedRunEvent> completed_runs)
    {
        if (completed_runs.empty()) {
            return;
        }

        const std::size_t first_target_pile = state().completed_runs() - completed_runs.size();
        CompletedRunAnimation animation;
        animation.flights.reserve(completed_runs.size());
        for (std::size_t index = 0; index < completed_runs.size(); ++index) {
            animation.flights.push_back(CompletedRunFlight {
                .run = completed_runs[index],
                .target_pile_index = first_target_pile + index,
            });
        }

        hovered_button_.reset();
        hovered_stack_.reset();
        completed_run_animation_ = std::move(animation);
    }

    void update_completed_run_animation(float delta_seconds)
    {
        if (!completed_run_active()) {
            return;
        }

        completed_run_animation_->elapsed_seconds += std::max(delta_seconds, 0.0F);
        if (completed_run_animation_->elapsed_seconds < completed_run_total_duration()) {
            return;
        }

        completed_run_animation_.reset();
    }

    void start_new_game()
    {
        const auto& current = state();
        const std::uint64_t new_seed = kInitialSeed + current.move_count() + current.completed_runs() + 1;
        start_opening_deal_animation(spider::GameSession::create_new_game(new_seed));
    }

    void restart_game()
    {
        start_opening_deal_animation(spider::GameSession::create_new_game(state().seed()));
    }

    void undo_action()
    {
        if (session_.undo()) {
            persist_session();
            clear_selection_state();
            cancel_confirmation();
            completed_run_animation_.reset();
        }
    }

    void redo_action()
    {
        if (session_.redo()) {
            persist_session();
            clear_selection_state();
            cancel_confirmation();
            completed_run_animation_.reset();
        }
    }

    void deal_stock()
    {
        start_stock_deal_animation();
    }

    auto stack_counts() const -> std::array<std::size_t, 10>
    {
        std::array<std::size_t, 10> counts {};
        if (stock_deal_active()) {
            const TableauCards tableau = current_stock_deal_tableau();
            for (std::size_t stack_index = 0; stack_index < counts.size(); ++stack_index) {
                counts[stack_index] = tableau[stack_index].size();
            }
            return counts;
        }

        for (std::size_t stack_index = 0; stack_index < counts.size(); ++stack_index) {
            counts[stack_index] = visual_state().tableau()[stack_index].size();
        }
        return counts;
    }

    auto hidden_counts() const -> std::array<std::size_t, 10>
    {
        std::array<std::size_t, 10> counts {};
        if (stock_deal_active()) {
            for (std::size_t stack_index = 0; stack_index < counts.size(); ++stack_index) {
                counts[stack_index] = state().hidden_card_count(stack_index);
            }
            return counts;
        }

        for (std::size_t stack_index = 0; stack_index < counts.size(); ++stack_index) {
            counts[stack_index] = visual_state().hidden_card_count(stack_index);
        }
        return counts;
    }

    auto layout_for_window(int width, int height) const -> spider::LayoutMetrics
    {
        return spider::compute_layout(width, height, stack_counts(), hidden_counts(), scroll_offset_);
    }

    [[nodiscard]] auto stack_card_rect(const spider::LayoutMetrics& layout, std::size_t stack_index, std::size_t card_index) const -> SDL_FRect
    {
        return SDL_FRect {
            layout.stack_x[stack_index],
            layout.playfield_top + 12.0F - layout.scroll_offset + static_cast<float>(card_index) * layout.stack_vertical_offset,
            layout.card_width,
            layout.card_height,
        };
    }

    auto card_rects_for_stack(const spider::LayoutMetrics& layout, std::size_t stack_index) const -> std::vector<SDL_FRect>
    {
        std::vector<SDL_FRect> rects;
        const auto& stack = state().tableau()[stack_index];
        rects.reserve(stack.size());

        float y = layout.playfield_top + 12.0F - layout.scroll_offset;
        for (const auto& card : stack) {
            rects.push_back(SDL_FRect {layout.stack_x[stack_index], y, layout.card_width, layout.card_height});
            y += layout.stack_vertical_offset;
        }

        return rects;
    }

    auto hit_test_stack(float x, float y, const spider::LayoutMetrics& layout) const -> std::optional<std::size_t>
    {
        const float top = layout.playfield_top;
        const float bottom = layout.playfield_top + layout.playfield_height;
        if (y < top || y > bottom) {
            return std::nullopt;
        }

        for (std::size_t stack_index = 0; stack_index < 10; ++stack_index) {
            SDL_FRect column_rect {
                layout.stack_x[stack_index] - layout.stack_gap * 0.5F,
                top,
                layout.card_width + layout.stack_gap,
                layout.playfield_height,
            };

            if (point_in_rect(x, y, column_rect)) {
                return stack_index;
            }
        }

        return std::nullopt;
    }

    auto hit_test_card(float x, float y, const spider::LayoutMetrics& layout) const -> std::optional<Selection>
    {
        for (std::size_t stack_index = 0; stack_index < 10; ++stack_index) {
            const auto rects = card_rects_for_stack(layout, stack_index);
            for (std::size_t index = rects.size(); index > 0; --index) {
                const std::size_t card_index = index - 1;
                if (!point_in_rect(x, y, rects[card_index])) {
                    continue;
                }

                if (state().is_movable_sequence(stack_index, card_index)) {
                    return Selection {.stack_index = stack_index, .start_index = card_index};
                }

                return std::nullopt;
            }
        }

        return std::nullopt;
    }

    auto try_move_selection_to(std::size_t destination_stack) -> bool
    {
        if (!selection_.has_value()) {
            return false;
        }

        const spider::Move move {
            .from_stack = selection_->stack_index,
            .start_index = selection_->start_index,
            .to_stack = destination_stack,
        };

        std::vector<spider::CompletedRunEvent> completed_runs;
        if (!session_.move_sequence(move, completed_runs)) {
            return false;
        }

        persist_session();
        clear_selection_state();
        start_completed_run_animation(std::move(completed_runs));
        return true;
    }

    void handle_button_action(Button::Action action)
    {
        if (!is_button_enabled(action)) {
            return;
        }

        switch (action) {
        case Button::Action::new_game:
            open_confirmation(ConfirmationAction::new_game);
            return;
        case Button::Action::restart:
            open_confirmation(ConfirmationAction::restart);
            return;
        case Button::Action::undo:
            undo_action();
            return;
        case Button::Action::redo:
            redo_action();
            return;
        case Button::Action::confirm_yes:
            if (pending_confirmation_ == ConfirmationAction::new_game) {
                start_new_game();
            } else if (pending_confirmation_ == ConfirmationAction::restart) {
                restart_game();
            }
            return;
        case Button::Action::confirm_no:
            cancel_confirmation();
            return;
        }
    }

    void handle_click(float x, float y)
    {
        const auto [window_width, window_height] = current_window_size();
        const auto layout = layout_for_window(window_width, window_height);
        layout_ui(layout, window_width, window_height);

        if (pending_confirmation_.has_value()) {
            for (const auto& button : dialog_buttons_) {
                if (point_in_rect(x, y, button.bounds)) {
                    handle_button_action(button.action);
                    return;
                }
            }
            return;
        }

        for (const auto& button : buttons_) {
            if (point_in_rect(x, y, button.bounds)) {
                handle_button_action(button.action);
                return;
            }
        }

        if (point_in_rect(x, y, top_row_slot_rect(layout, 0)) && state().can_deal_from_stock()) {
            deal_stock();
            return;
        }

        if (const auto card = hit_test_card(x, y, layout); card.has_value()) {
            if (selection_.has_value() && selection_->stack_index == card->stack_index && selection_->start_index == card->start_index) {
                selection_.reset();
                return;
            }

            if (selection_.has_value() && try_move_selection_to(card->stack_index)) {
                return;
            }

            if (!selection_.has_value()) {
                if (try_auto_move_card(*card)) {
                    return;
                }
            }

            selection_ = card;
            return;
        }

        if (const auto stack = hit_test_stack(x, y, layout); stack.has_value()) {
            if (selection_.has_value() && try_move_selection_to(*stack)) {
                return;
            }
        }

        selection_.reset();
        pressed_selection_.reset();
    }

    void start_drag(const Selection& selection, float x, float y)
    {
        const auto [window_width, window_height] = current_window_size();
        const auto layout = layout_for_window(window_width, window_height);
        const auto rects = card_rects_for_stack(layout, selection.stack_index);
        const SDL_FRect& anchor = rects[selection.start_index];

        selection_ = selection;
        drag_state_ = DragState {
            .selection = selection,
            .grab_offset_x = x - anchor.x,
            .grab_offset_y = y - anchor.y,
            .mouse_x = x,
            .mouse_y = y,
        };
    }

    void end_drag(float x, float y)
    {
        if (!drag_state_.has_value()) {
            return;
        }

        const auto [window_width, window_height] = current_window_size();
        const auto layout = layout_for_window(window_width, window_height);

        if (const auto stack = hit_test_stack(x, y, layout); stack.has_value()) {
            if (try_move_selection_to(*stack)) {
                return;
            }
        }

        drag_state_.reset();
    }

    void handle_event(const SDL_Event& event)
    {
        switch (event.type) {
        case SDL_EVENT_QUIT:
            running_ = false;
            return;
        case SDL_EVENT_MOUSE_WHEEL:
            if (pending_confirmation_.has_value() || animation_active()) {
                return;
            }
            scroll_offset_ -= event.wheel.y * 64.0F;
            {
                const auto [window_width, window_height] = current_window_size();
                scroll_offset_ = layout_for_window(window_width, window_height).scroll_offset;
            }
            return;
        case SDL_EVENT_KEY_DOWN:
            if (pending_confirmation_.has_value()) {
                if (event.key.key == SDLK_ESCAPE) {
                    cancel_confirmation();
                }
                return;
            }
            if (animation_active()) {
                return;
            }
            if (event.key.key == SDLK_ESCAPE) {
                clear_selection_state();
                return;
            }
            if (event.key.key == SDLK_N) {
                open_confirmation(ConfirmationAction::new_game);
                return;
            }
            if (event.key.key == SDLK_F) {
                undo_action();
                return;
            }
            if (event.key.key == SDLK_D || event.key.key == SDLK_SPACE) {
                deal_stock();
                return;
            }
            return;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (event.button.button != SDL_BUTTON_LEFT) {
                return;
            }
            if (pending_confirmation_.has_value() || animation_active()) {
                return;
            }
            press_x_ = event.button.x;
            press_y_ = event.button.y;
            {
                const auto [window_width, window_height] = current_window_size();
                const auto layout = layout_for_window(window_width, window_height);
                const auto hit = hit_test_card(event.button.x, event.button.y, layout);
                if (hit.has_value()) {
                    pressed_selection_ = hit;
                } else {
                    pressed_selection_.reset();
                }
            }
            return;
        case SDL_EVENT_MOUSE_MOTION:
            if (animation_active()) {
                return;
            }
            update_hover_state(event.motion.x, event.motion.y);
            if (pending_confirmation_.has_value()) {
                return;
            }
            if (drag_state_.has_value()) {
                drag_state_->mouse_x = event.motion.x;
                drag_state_->mouse_y = event.motion.y;
                return;
            }

            if (!pressed_selection_.has_value()) {
                return;
            }

            if (std::fabs(event.motion.x - press_x_) > 8.0F || std::fabs(event.motion.y - press_y_) > 8.0F) {
                start_drag(*pressed_selection_, event.motion.x, event.motion.y);
                pressed_selection_.reset();
            }
            return;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (event.button.button != SDL_BUTTON_LEFT) {
                return;
            }
            if (animation_active()) {
                return;
            }
            if (drag_state_.has_value()) {
                end_drag(event.button.x, event.button.y);
                return;
            }
            handle_click(event.button.x, event.button.y);
            pressed_selection_.reset();
            return;
        default:
            return;
        }
    }

    void draw_button(const Button& button, bool enabled)
    {
        const bool hovered = hovered_button_.has_value() && *hovered_button_ == button.action;
        SDL_SetRenderDrawColor(renderer_, enabled ? (hovered ? 68 : 54) : 35, enabled ? (hovered ? 109 : 94) : 55, enabled ? (hovered ? 134 : 120) : 63, 255);
        SDL_RenderFillRect(renderer_, &button.bounds);

        SDL_SetRenderDrawColor(renderer_, hovered ? 236 : 180, hovered ? 234 : 214, hovered ? 221 : 226, 255);
        SDL_RenderRect(renderer_, &button.bounds);

        const float horizontal_padding = 16.0F;
        const float width_limited_scale = (button.bounds.w - horizontal_padding) / std::max(1.0F, static_cast<float>(button.label.size() * 6U - 1U));
        const float text_scale = std::clamp(std::min(button.bounds.h / 13.0F, width_limited_scale), 1.6F, 2.6F);
        const float text_width = static_cast<float>(button.label.size()) * 6.0F * text_scale - text_scale;
        const float text_x = button.bounds.x + (button.bounds.w - text_width) * 0.5F;
        const float text_y = button.bounds.y + (button.bounds.h - (7.0F * text_scale)) * 0.5F;
        draw_text(renderer_, button.label, text_x, text_y, text_scale, enabled ? SDL_Color {236, 244, 247, 255} : SDL_Color {140, 154, 163, 255});
    }

    void draw_card_shadow(const SDL_FRect& destination)
    {
        const SDL_FRect shadow {
            destination.x + 4.0F,
            destination.y + 5.0F,
            destination.w,
            destination.h,
        };
        SDL_SetRenderDrawColor(renderer_, 7, 28, 16, 255);
        SDL_RenderFillRect(renderer_, &shadow);
    }

    void render_card_sprite(const SDL_FRect& destination, const spider::Card& card, bool selected)
    {
        draw_card_shadow(destination);
        const AtlasCell atlas = card.face_up ? atlas_for_card(card) : atlas_for_card_back();
        const SDL_FRect source {atlas.x, atlas.y, atlas.w, atlas.h};
        SDL_RenderTexture(renderer_, cards_texture_, &source, &destination);

        if (selected) {
            SDL_SetRenderDrawColor(renderer_, 248, 211, 67, 255);
            SDL_RenderRect(renderer_, &destination);
        }
    }

    void draw_placeholder_slot(const SDL_FRect& slot, bool filled)
    {
        SDL_SetRenderDrawColor(renderer_, filled ? 52 : 38, filled ? 70 : 54, filled ? 84 : 64, 255);
        SDL_RenderFillRect(renderer_, &slot);
        SDL_SetRenderDrawColor(renderer_, filled ? 208 : 110, filled ? 214 : 124, filled ? 223 : 92, 255);
        SDL_RenderRect(renderer_, &slot);
    }

    void render_card_back_stack(const SDL_FRect& slot, int layers)
    {
        for (int index = 0; index < std::max(layers, 1); ++index) {
            const SDL_FRect destination {
                slot.x - static_cast<float>(std::min(index, 3)) * 5.0F,
                slot.y + static_cast<float>(std::min(index, 3)) * 3.0F,
                slot.w,
                slot.h,
            };
            draw_card_shadow(destination);
            const auto atlas = atlas_for_card_back();
            const SDL_FRect source {atlas.x, atlas.y, atlas.w, atlas.h};
            SDL_RenderTexture(renderer_, cards_texture_, &source, &destination);
        }
    }

    [[nodiscard]] auto completed_pile_card_rect(const SDL_FRect& slot, std::size_t ascending_index) const -> SDL_FRect
    {
        const float depth = static_cast<float>(std::min<std::size_t>(spider::GameState::kCompletedRunLength - 1 - ascending_index, 4));
        return SDL_FRect {
            slot.x - depth * 3.0F,
            slot.y + depth * 1.5F,
            slot.w,
            slot.h,
        };
    }

    void render_completed_pile(const SDL_FRect& slot, spider::Suit suit)
    {
        draw_placeholder_slot(slot, true);
        const auto cards = completed_pile_cards(suit);
        for (std::size_t index = 0; index < cards.size(); ++index) {
            render_card_sprite(completed_pile_card_rect(slot, index), cards[index], false);
        }
    }

    [[nodiscard]] auto current_opening_tableau() const -> TableauCards
    {
        TableauCards tableau {};
        if (!opening_deal_animation_.has_value()) {
            return tableau;
        }

        for (std::size_t step_index = 0; step_index < opening_deal_animation_->steps.size(); ++step_index) {
            if (opening_deal_animation_->elapsed_seconds < opening_deal_finish_time(step_index)) {
                break;
            }

            const auto& step = opening_deal_animation_->steps[step_index];
            tableau[step.stack_index].push_back(step.card);
        }

        return tableau;
    }

    void render_opening_deal_flights(const spider::LayoutMetrics& layout)
    {
        if (!opening_deal_active()) {
            return;
        }

        const SDL_FRect source = top_row_slot_rect(layout, 0);
        for (std::size_t step_index = 0; step_index < opening_deal_animation_->steps.size(); ++step_index) {
            const float start_time = opening_deal_start_time(step_index);
            const float finish_time = opening_deal_finish_time(step_index);
            if (opening_deal_animation_->elapsed_seconds < start_time || opening_deal_animation_->elapsed_seconds >= finish_time) {
                continue;
            }

            const auto& step = opening_deal_animation_->steps[step_index];
            const SDL_FRect target = stack_card_rect(layout, step.stack_index, step.card_index);
            const float progress = smoothstep((opening_deal_animation_->elapsed_seconds - start_time) / kOpeningDealCardFlightSeconds);
            const float arc = std::sin(progress * 3.14159265F) * kOpeningDealArcHeight;
            const SDL_FRect destination {
                lerp(source.x, target.x, progress),
                lerp(source.y, target.y, progress) - arc,
                layout.card_width,
                layout.card_height,
            };
            render_card_sprite(destination, step.card, false);
        }
    }

    void render_auto_move_flight(const spider::LayoutMetrics& layout)
    {
        if (!auto_move_active()) {
            return;
        }

        const auto& animation = *auto_move_animation_;
        const float progress = smoothstep(animation.elapsed_seconds / kAutoMoveFlightSeconds);
        const float arc = std::sin(progress * 3.14159265F) * kAutoMoveArcHeight;

        for (std::size_t index = 0; index < animation.cards.size(); ++index) {
            const SDL_FRect source = stack_card_rect(layout, animation.move.from_stack, animation.move.start_index + index);
            const SDL_FRect target = stack_card_rect(layout, animation.move.to_stack, animation.destination_card_index + index);
            const SDL_FRect destination {
                lerp(source.x, target.x, progress),
                lerp(source.y, target.y, progress) - arc,
                layout.card_width,
                layout.card_height,
            };
            render_card_sprite(destination, animation.cards[index], false);
        }
    }

    void render_stock_deal_flights(const spider::LayoutMetrics& layout)
    {
        if (!stock_deal_active()) {
            return;
        }

        const SDL_FRect source = top_row_slot_rect(layout, 0);
        for (std::size_t step_index = 0; step_index < stock_deal_animation_->steps.size(); ++step_index) {
            const float start_time = stock_deal_start_time(step_index);
            const float finish_time = stock_deal_finish_time(step_index);
            if (stock_deal_animation_->elapsed_seconds < start_time || stock_deal_animation_->elapsed_seconds >= finish_time) {
                continue;
            }

            const auto& step = stock_deal_animation_->steps[step_index];
            const SDL_FRect target = stack_card_rect(layout, step.stack_index, step.card_index);
            const float progress = smoothstep((stock_deal_animation_->elapsed_seconds - start_time) / kStockDealCardFlightSeconds);
            const float arc = std::sin(progress * 3.14159265F) * kStockDealArcHeight;
            const SDL_FRect destination {
                lerp(source.x, target.x, progress),
                lerp(source.y, target.y, progress) - arc,
                layout.card_width,
                layout.card_height,
            };
            render_card_sprite(destination, step.card, false);
        }
    }

    void render_completed_run_flights(const spider::LayoutMetrics& layout)
    {
        if (!completed_run_active()) {
            return;
        }

        const float elapsed_seconds = completed_run_animation_->elapsed_seconds;
        for (const CompletedRunFlight& flight : completed_run_animation_->flights) {
            const SDL_FRect target_slot = top_row_slot_rect(layout, 2 + flight.target_pile_index);
            for (std::size_t ascending_index = 0; ascending_index < spider::GameState::kCompletedRunLength; ++ascending_index) {
                const float start_time = completed_run_start_time(ascending_index);
                const float finish_time = completed_run_finish_time(ascending_index);
                if (elapsed_seconds < start_time || elapsed_seconds >= finish_time) {
                    continue;
                }

                const std::size_t stack_offset = spider::GameState::kCompletedRunLength - 1 - ascending_index;
                const SDL_FRect source = stack_card_rect(layout, flight.run.stack_index, flight.run.start_index + stack_offset);
                const SDL_FRect target = completed_pile_card_rect(target_slot, ascending_index);
                const float progress = smoothstep((elapsed_seconds - start_time) / kCompletedRunFlightSeconds);
                const float arc = std::sin(progress * 3.14159265F) * kCompletedRunArcHeight;
                const SDL_FRect destination {
                    lerp(source.x, target.x, progress),
                    lerp(source.y, target.y, progress) - arc,
                    layout.card_width,
                    layout.card_height,
                };
                render_card_sprite(destination, flight.run.cards[stack_offset], false);
            }
        }
    }

    void render_opening_deal_tableau(const spider::LayoutMetrics& layout)
    {
        const TableauCards tableau = current_opening_tableau();
        for (std::size_t stack_index = 0; stack_index < tableau.size(); ++stack_index) {
            const SDL_FRect slot {
                layout.stack_x[stack_index],
                layout.playfield_top + 12.0F,
                layout.card_width,
                layout.card_height,
            };

            SDL_SetRenderDrawColor(renderer_, 36, 108, 56, 255);
            SDL_RenderRect(renderer_, &slot);

            for (std::size_t card_index = 0; card_index < tableau[stack_index].size(); ++card_index) {
                render_card_sprite(stack_card_rect(layout, stack_index, card_index), tableau[stack_index][card_index], false);
            }
        }
    }

    void render_stock_deal_tableau(const spider::LayoutMetrics& layout)
    {
        const TableauCards tableau = current_stock_deal_tableau();
        for (std::size_t stack_index = 0; stack_index < tableau.size(); ++stack_index) {
            const SDL_FRect slot {
                layout.stack_x[stack_index],
                layout.playfield_top + 12.0F,
                layout.card_width,
                layout.card_height,
            };

            SDL_SetRenderDrawColor(renderer_, 36, 108, 56, 255);
            SDL_RenderRect(renderer_, &slot);

            for (std::size_t card_index = 0; card_index < tableau[stack_index].size(); ++card_index) {
                render_card_sprite(stack_card_rect(layout, stack_index, card_index), tableau[stack_index][card_index], false);
            }
        }
    }

    void render_top_row(const spider::LayoutMetrics& layout)
    {
        const SDL_FRect stock_slot = top_row_slot_rect(layout, 0);
        if (opening_deal_active()) {
            render_card_back_stack(stock_slot, deck_layers_for_card_count(opening_deck_card_count()));
        } else if (stock_deal_active()) {
            render_card_back_stack(stock_slot, deck_layers_for_card_count(stock_deal_deck_card_count()));
        } else if (state().stock_rows_remaining() > 0) {
            render_card_back_stack(stock_slot, static_cast<int>(state().stock_rows_remaining()));
        } else {
            draw_placeholder_slot(stock_slot, false);
        }

        const auto& completed_suits = visual_state().completed_run_suits();
        const std::size_t animated_slots = completed_run_active() ? completed_run_animation_->flights.size() : 0;
        const std::size_t visible_completed_slots = completed_suits.size() >= animated_slots ? completed_suits.size() - animated_slots : 0;
        for (std::size_t completed_index = 0; completed_index < spider::GameState::kWinningCompletedRuns; ++completed_index) {
            const std::size_t slot_index = completed_index + 2;
            const SDL_FRect slot = top_row_slot_rect(layout, slot_index);
            if (completed_index < visible_completed_slots) {
                render_completed_pile(slot, completed_suits[completed_index]);
            } else {
                draw_placeholder_slot(slot, false);
            }
        }
    }

    void render_stack(const spider::LayoutMetrics& layout, std::size_t stack_index)
    {
        const SDL_FRect slot {
            layout.stack_x[stack_index],
            layout.playfield_top + 12.0F,
            layout.card_width,
            layout.card_height,
        };

        const bool hovered = hovered_stack_.has_value() && *hovered_stack_ == stack_index;
        const bool valid_drop = hovered && can_move_selection_to(stack_index);
        SDL_SetRenderDrawColor(renderer_, valid_drop ? 190 : (hovered ? 72 : 36), valid_drop ? 184 : (hovered ? 148 : 108), valid_drop ? 78 : (hovered ? 76 : 56), 255);
        SDL_RenderRect(renderer_, &slot);

        const auto& stack = state().tableau()[stack_index];
        const auto rects = card_rects_for_stack(layout, stack_index);

        for (std::size_t card_index = 0; card_index < stack.size(); ++card_index) {
            if (auto_move_active() && auto_move_animation_->move.from_stack == stack_index && card_index >= auto_move_animation_->move.start_index) {
                continue;
            }

            if (drag_state_.has_value() && drag_state_->selection.stack_index == stack_index && card_index >= drag_state_->selection.start_index) {
                continue;
            }

            const bool selected = selection_.has_value()
                && selection_->stack_index == stack_index
                && card_index == selection_->start_index
                && !drag_state_.has_value();

            render_card_sprite(rects[card_index], stack[card_index], selected);
        }
    }

    void render_drag_stack(const spider::LayoutMetrics& layout)
    {
        if (!drag_state_.has_value()) {
            return;
        }

        const auto& selection = drag_state_->selection;
        const auto& stack = state().tableau()[selection.stack_index];
        float y = drag_state_->mouse_y - drag_state_->grab_offset_y;
        const float x = drag_state_->mouse_x - drag_state_->grab_offset_x;

        for (std::size_t index = selection.start_index; index < stack.size(); ++index) {
            const SDL_FRect destination {x, y, layout.card_width, layout.card_height};
            render_card_sprite(destination, stack[index], index == selection.start_index);
            y += layout.stack_vertical_offset;
        }
    }

    void render_confirmation_dialog(int window_width, int window_height)
    {
        if (!pending_confirmation_.has_value()) {
            return;
        }

        const SDL_FRect panel = confirmation_panel_bounds(window_width, window_height);
        SDL_SetRenderDrawColor(renderer_, 34, 36, 40, 255);
        SDL_RenderFillRect(renderer_, &panel);
        SDL_SetRenderDrawColor(renderer_, 184, 189, 192, 255);
        SDL_RenderRect(renderer_, &panel);

        const char* title = pending_confirmation_ == ConfirmationAction::new_game ? "NEW GAME" : "RESTART";
        const char* message = pending_confirmation_ == ConfirmationAction::new_game ? "START A NEW DEAL" : "RESET DEAL TO START";
        draw_text(renderer_, title, panel.x + 22.0F, panel.y + 18.0F, 3.2F, SDL_Color {240, 243, 244, 255});
        draw_text(renderer_, message, panel.x + 22.0F, panel.y + 66.0F, 2.3F, SDL_Color {231, 236, 238, 255});
        draw_text(renderer_, "LOSE GAME DATA", panel.x + 22.0F, panel.y + 100.0F, 2.2F, SDL_Color {223, 202, 146, 255});

        for (const auto& button : dialog_buttons_) {
            draw_button(button, true);
        }
    }

    void render()
    {
        const auto [window_width, window_height] = current_window_size();
        const auto layout = layout_for_window(window_width, window_height);
        layout_ui(layout, window_width, window_height);
        scroll_offset_ = layout.scroll_offset;

        SDL_SetRenderDrawColor(renderer_, 2, 69, 27, 255);
        SDL_RenderClear(renderer_);

        SDL_SetRenderDrawColor(renderer_, 65, 66, 70, 255);
        const SDL_FRect controls_bar {0.0F, 0.0F, static_cast<float>(window_width), layout.top_controls_bottom};
        SDL_RenderFillRect(renderer_, &controls_bar);

        SDL_SetRenderDrawColor(renderer_, 88, 88, 92, 255);
        const SDL_FRect controls_bar_inner {
            layout.outer_margin * 0.45F,
            layout.outer_margin * 0.35F,
            static_cast<float>(window_width) - layout.outer_margin * 0.9F,
            layout.top_controls_bottom - layout.outer_margin * 0.55F,
        };
        SDL_RenderRect(renderer_, &controls_bar_inner);

        SDL_SetRenderDrawColor(renderer_, 44, 46, 52, 255);
        const SDL_FRect top_row_band {
            0.0F,
            layout.top_controls_bottom,
            static_cast<float>(window_width),
            layout.playfield_top - layout.top_controls_bottom,
        };
        SDL_RenderFillRect(renderer_, &top_row_band);

        SDL_SetRenderDrawColor(renderer_, 106, 108, 114, 255);
        const SDL_FRect top_row_band_inner {
            layout.outer_margin * 0.3F,
            layout.top_controls_bottom + layout.top_row_gap * 0.35F,
            static_cast<float>(window_width) - layout.outer_margin * 0.6F,
            std::max(0.0F, layout.playfield_top - layout.top_controls_bottom - layout.top_row_gap * 0.7F),
        };
        SDL_RenderRect(renderer_, &top_row_band_inner);

        SDL_SetRenderDrawColor(renderer_, 118, 120, 128, 255);
        const SDL_FRect controls_divider {
            0.0F,
            layout.top_controls_bottom - 1.0F,
            static_cast<float>(window_width),
            1.0F,
        };
        SDL_RenderFillRect(renderer_, &controls_divider);

        for (const auto& button : buttons_) {
            draw_button(button, is_button_enabled(button.action));
        }
        render_top_row(layout);

        const SDL_Rect clip_rect {
            0,
            static_cast<int>(layout.playfield_top),
            window_width,
            std::max(0, window_height - static_cast<int>(layout.playfield_top)),
        };
        SDL_SetRenderClipRect(renderer_, &clip_rect);

        SDL_SetRenderDrawColor(renderer_, 0, 98, 37, 255);
        const SDL_FRect table_surface {
            0.0F,
            layout.playfield_top,
            static_cast<float>(window_width),
            static_cast<float>(window_height) - layout.playfield_top,
        };
        SDL_RenderFillRect(renderer_, &table_surface);

        SDL_SetRenderDrawColor(renderer_, 4, 128, 48, 255);
        const SDL_FRect inner_surface {
            layout.outer_margin * 0.3F,
            layout.playfield_top + 6.0F,
            static_cast<float>(window_width) - layout.outer_margin * 0.6F,
            static_cast<float>(window_height) - layout.playfield_top - layout.outer_margin * 0.3F,
        };
        SDL_RenderRect(renderer_, &inner_surface);

        if (opening_deal_active()) {
            render_opening_deal_tableau(layout);
            render_opening_deal_flights(layout);
        } else if (stock_deal_active()) {
            render_stock_deal_tableau(layout);
            render_stock_deal_flights(layout);
        } else {
            for (std::size_t stack_index = 0; stack_index < 10; ++stack_index) {
                render_stack(layout, stack_index);
            }

            render_drag_stack(layout);
            render_auto_move_flight(layout);
        }

        SDL_SetRenderClipRect(renderer_, nullptr);
        render_completed_run_flights(layout);
        render_confirmation_dialog(window_width, window_height);
        SDL_RenderPresent(renderer_);
    }
};

} // namespace

auto main() -> int
{
    try {
        App app;
        return app.run();
    } catch (const std::exception& exception) {
        SDL_Log("fatal error: %s", exception.what());
        return 1;
    }
}
