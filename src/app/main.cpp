#include "spider/game_state.hpp"
#include "spider/layout.hpp"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <optional>
#include <random>
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
    std::string_view label;
    SDL_FRect bounds {};
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
    const int row = card.suit == spider::Suit::spades ? 0 : 1;
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
        : state_(spider::GameState::create_new_game(kInitialSeed))
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
    spider::GameState state_;
    float scroll_offset_ {0.0F};
    std::optional<Selection> selection_;
    std::optional<Selection> pressed_selection_;
    std::optional<DragState> drag_state_;
    std::optional<std::size_t> hovered_stack_;
    std::optional<std::size_t> hovered_button_;
    float press_x_ {0.0F};
    float press_y_ {0.0F};
    std::array<Button, 2> buttons_ {};
    bool running_ {true};

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

        return state_.can_move_sequence(spider::Move {
            .from_stack = selection_->stack_index,
            .start_index = selection_->start_index,
            .to_stack = destination_stack,
        });
    }

    void update_hover_state(float x, float y)
    {
        hovered_button_.reset();
        for (std::size_t button_index = 0; button_index < buttons_.size(); ++button_index) {
            if (point_in_rect(x, y, buttons_[button_index].bounds)) {
                hovered_button_ = button_index;
                break;
            }
        }

        const auto [window_width, window_height] = current_window_size();
        const auto layout = layout_for_window(window_width, window_height);
        hovered_stack_ = hit_test_stack(x, y, layout);
    }

    void start_new_game()
    {
        const std::uint64_t new_seed = kInitialSeed + state_.move_count() + state_.completed_runs() + 1;
        state_ = spider::GameState::create_new_game(new_seed);
        selection_.reset();
        pressed_selection_.reset();
        drag_state_.reset();
        scroll_offset_ = 0.0F;
    }

    void deal_stock()
    {
        if (state_.deal_from_stock()) {
            selection_.reset();
        }
    }

    auto stack_counts() const -> std::array<std::size_t, 10>
    {
        std::array<std::size_t, 10> counts {};
        for (std::size_t stack_index = 0; stack_index < counts.size(); ++stack_index) {
            counts[stack_index] = state_.tableau()[stack_index].size();
        }
        return counts;
    }

    auto hidden_counts() const -> std::array<std::size_t, 10>
    {
        std::array<std::size_t, 10> counts {};
        for (std::size_t stack_index = 0; stack_index < counts.size(); ++stack_index) {
            counts[stack_index] = state_.hidden_card_count(stack_index);
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
        const auto& stack = state_.tableau()[stack_index];
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

                if (state_.is_movable_sequence(stack_index, card_index)) {
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

        if (!state_.move_sequence(move)) {
            return false;
        }

        selection_.reset();
        drag_state_.reset();
        return true;
    }

    void handle_click(float x, float y)
    {
        for (const auto& button : buttons_) {
            if (point_in_rect(x, y, button.bounds)) {
                if (button.label == "NEW") {
                    start_new_game();
                } else if (button.label == "DEAL") {
                    deal_stock();
                }
                return;
            }
        }

        const auto [window_width, window_height] = current_window_size();
        const auto layout = layout_for_window(window_width, window_height);

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
            scroll_offset_ -= event.wheel.y * 64.0F;
            {
                const auto [window_width, window_height] = current_window_size();
                scroll_offset_ = layout_for_window(window_width, window_height).scroll_offset;
            }
            return;
        case SDL_EVENT_KEY_DOWN:
            if (event.key.key == SDLK_ESCAPE) {
                selection_.reset();
                drag_state_.reset();
                return;
            }
            if (event.key.key == SDLK_N) {
                start_new_game();
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
        const bool hovered = hovered_button_.has_value() && buttons_[*hovered_button_].label == button.label;
        SDL_SetRenderDrawColor(renderer_, enabled ? (hovered ? 68 : 54) : 35, enabled ? (hovered ? 109 : 94) : 55, enabled ? (hovered ? 134 : 120) : 63, 255);
        SDL_RenderFillRect(renderer_, &button.bounds);

        SDL_SetRenderDrawColor(renderer_, hovered ? 236 : 180, hovered ? 234 : 214, hovered ? 221 : 226, 255);
        SDL_RenderRect(renderer_, &button.bounds);

        const float text_scale = std::max(2.5F, button.bounds.h / 12.0F);
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

    void draw_stat_panel(float x, float y, float w, float h, std::string_view label, int value)
    {
        const SDL_FRect panel {x, y, w, h};
        SDL_SetRenderDrawColor(renderer_, 58, 60, 64, 255);
        SDL_RenderFillRect(renderer_, &panel);
        SDL_SetRenderDrawColor(renderer_, 162, 170, 174, 255);
        SDL_RenderRect(renderer_, &panel);

        const float label_scale = std::max(2.1F, h / 22.0F);
        const float value_scale = std::max(2.8F, h / 15.0F);
        draw_text(renderer_, label, x + 10.0F, y + 8.0F, label_scale, SDL_Color {210, 214, 216, 255});
        draw_text(renderer_, std::to_string(value), x + 10.0F, y + h * 0.48F, value_scale, SDL_Color {248, 248, 244, 255});
    }

    void render_stock_pile(const spider::LayoutMetrics& layout, int window_width)
    {
        const int rows = static_cast<int>(state_.stock_rows_remaining());
        const float pile_width = layout.card_width * 0.58F;
        const float pile_height = layout.card_height * 0.58F;
        const float pile_x = static_cast<float>(window_width) - layout.outer_margin - pile_width - 8.0F;
        const float pile_y = layout.outer_margin + 7.0F;

        for (int index = 0; index < std::max(rows, 1); ++index) {
            const SDL_FRect destination {
                pile_x - static_cast<float>(std::min(index, 3)) * 6.0F,
                pile_y + static_cast<float>(std::min(index, 3)) * 4.0F,
                pile_width,
                pile_height,
            };
            draw_card_shadow(destination);
            const auto atlas = atlas_for_card_back();
            const SDL_FRect source {atlas.x, atlas.y, atlas.w, atlas.h};
            SDL_RenderTexture(renderer_, cards_texture_, &source, &destination);
        }

        draw_text(renderer_, "STOCK", pile_x - 8.0F, pile_y + pile_height + 12.0F, 2.4F, SDL_Color {234, 236, 233, 255});
        draw_text(renderer_, std::to_string(rows), pile_x + pile_width - 16.0F, pile_y + pile_height + 12.0F, 2.8F, SDL_Color {250, 245, 214, 255});
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
        draw_text(renderer_, std::to_string(static_cast<int>(stack_index + 1)), slot.x + 6.0F, slot.y - 18.0F, 2.0F, SDL_Color {216, 226, 213, 255});

        const auto& stack = state_.tableau()[stack_index];
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
        const auto& stack = state_.tableau()[selection.stack_index];
        float y = drag_state_->mouse_y - drag_state_->grab_offset_y;
        const float x = drag_state_->mouse_x - drag_state_->grab_offset_x;

        for (std::size_t index = selection.start_index; index < stack.size(); ++index) {
            const SDL_FRect destination {x, y, layout.card_width, layout.card_height};
            render_card_sprite(destination, stack[index], index == selection.start_index);
            y += layout.face_up_step;
        }
    }

    void render()
    {
        const auto [window_width, window_height] = current_window_size();
        const auto layout = layout_for_window(window_width, window_height);
        scroll_offset_ = layout.scroll_offset;

        buttons_[0] = Button {
            .label = "NEW",
            .bounds = SDL_FRect {layout.outer_margin, layout.outer_margin + 7.0F, 120.0F, layout.top_controls_height - 20.0F},
        };
        buttons_[1] = Button {
            .label = "DEAL",
            .bounds = SDL_FRect {layout.outer_margin + 136.0F, layout.outer_margin + 7.0F, 120.0F, layout.top_controls_height - 20.0F},
        };

        SDL_SetRenderDrawColor(renderer_, 2, 69, 27, 255);
        SDL_RenderClear(renderer_);

        SDL_SetRenderDrawColor(renderer_, 65, 66, 70, 255);
        const SDL_FRect top_bar {0.0F, 0.0F, static_cast<float>(window_width), layout.playfield_top};
        SDL_RenderFillRect(renderer_, &top_bar);

        SDL_SetRenderDrawColor(renderer_, 88, 88, 92, 255);
        const SDL_FRect top_bar_inner {layout.outer_margin * 0.45F, layout.outer_margin * 0.35F, static_cast<float>(window_width) - layout.outer_margin * 0.9F, layout.playfield_top - layout.outer_margin * 0.55F};
        SDL_RenderRect(renderer_, &top_bar_inner);

        draw_button(buttons_[0], true);
        draw_button(buttons_[1], state_.can_deal_from_stock());
        draw_stat_panel(layout.outer_margin + 286.0F, layout.outer_margin + 7.0F, 118.0F, layout.top_controls_height - 20.0F, "MOVES", static_cast<int>(state_.move_count()));
        draw_stat_panel(layout.outer_margin + 420.0F, layout.outer_margin + 7.0F, 118.0F, layout.top_controls_height - 20.0F, "RUNS", static_cast<int>(state_.completed_runs()));
        render_stock_pile(layout, window_width);

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
