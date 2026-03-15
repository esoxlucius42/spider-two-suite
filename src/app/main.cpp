#include "spider/card_sprite.hpp"
#include "spider/game_session.hpp"
#include "spider/layout.hpp"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include <algorithm>
#include <array>
#include <cctype>
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

constexpr std::array<int, 14> kAtlasX {0, 167, 334, 502, 669, 837, 1004, 1172, 1339, 1507, 1674, 1842, 2009, 2179};
constexpr std::array<int, 6> kAtlasY {0, 243, 487, 730, 972, 1216};
constexpr std::uint64_t kInitialSeed = 20260315ULL;

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
        : session_(spider::GameSession::create_new_game(kInitialSeed))
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
        while (running_) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                handle_event(event);
            }

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
    float press_x_ {0.0F};
    float press_y_ {0.0F};
    std::array<Button, 4> buttons_ {};
    std::array<Button, 2> dialog_buttons_ {};
    bool running_ {true};

    [[nodiscard]] auto state() const -> const spider::GameState&
    {
        return session_.state();
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

    void start_new_game()
    {
        const auto& current = state();
        const std::uint64_t new_seed = kInitialSeed + current.move_count() + current.completed_runs() + 1;
        session_.start_new_game(new_seed);
        cancel_confirmation();
        reset_for_fresh_layout();
    }

    void restart_game()
    {
        session_.restart();
        cancel_confirmation();
        reset_for_fresh_layout();
    }

    void undo_action()
    {
        if (session_.undo()) {
            clear_selection_state();
            cancel_confirmation();
        }
    }

    void redo_action()
    {
        if (session_.redo()) {
            clear_selection_state();
            cancel_confirmation();
        }
    }

    void deal_stock()
    {
        if (session_.deal_from_stock()) {
            clear_selection_state();
        }
    }

    auto stack_counts() const -> std::array<std::size_t, 10>
    {
        std::array<std::size_t, 10> counts {};
        for (std::size_t stack_index = 0; stack_index < counts.size(); ++stack_index) {
            counts[stack_index] = state().tableau()[stack_index].size();
        }
        return counts;
    }

    auto hidden_counts() const -> std::array<std::size_t, 10>
    {
        std::array<std::size_t, 10> counts {};
        for (std::size_t stack_index = 0; stack_index < counts.size(); ++stack_index) {
            counts[stack_index] = state().hidden_card_count(stack_index);
        }
        return counts;
    }

    auto layout_for_window(int width, int height) const -> spider::LayoutMetrics
    {
        return spider::compute_layout(width, height, stack_counts(), hidden_counts(), scroll_offset_);
    }

    auto card_rects_for_stack(const spider::LayoutMetrics& layout, std::size_t stack_index) const -> std::vector<SDL_FRect>
    {
        std::vector<SDL_FRect> rects;
        const auto& stack = state().tableau()[stack_index];
        rects.reserve(stack.size());

        float y = layout.playfield_top + 12.0F - layout.scroll_offset;
        for (const auto& card : stack) {
            rects.push_back(SDL_FRect {layout.stack_x[stack_index], y, layout.card_width, layout.card_height});
            y += card.face_up ? layout.face_up_step : layout.face_down_step;
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

        if (!session_.move_sequence(move)) {
            return false;
        }

        clear_selection_state();
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
            if (pending_confirmation_.has_value()) {
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
            if (event.key.key == SDLK_ESCAPE) {
                clear_selection_state();
                return;
            }
            if (event.key.key == SDLK_N) {
                open_confirmation(ConfirmationAction::new_game);
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
            if (pending_confirmation_.has_value()) {
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

    void render_top_row(const spider::LayoutMetrics& layout)
    {
        const SDL_FRect stock_slot = top_row_slot_rect(layout, 0);
        if (state().stock_rows_remaining() > 0) {
            render_card_back_stack(stock_slot, static_cast<int>(state().stock_rows_remaining()));
        } else {
            draw_placeholder_slot(stock_slot, false);
        }

        const std::size_t completed_slots = std::min<std::size_t>(state().completed_runs(), 8);
        for (std::size_t slot_index = 2; slot_index < 10; ++slot_index) {
            const SDL_FRect slot = top_row_slot_rect(layout, slot_index);
            const bool filled = (slot_index - 2) < completed_slots;
            if (filled) {
                render_card_back_stack(slot, 2);
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
            y += layout.face_up_step;
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

        for (std::size_t stack_index = 0; stack_index < 10; ++stack_index) {
            render_stack(layout, stack_index);
        }

        render_drag_stack(layout);

        SDL_SetRenderClipRect(renderer_, nullptr);
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
