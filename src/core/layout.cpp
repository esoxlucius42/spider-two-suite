#include "spider/layout.hpp"

#include <algorithm>

namespace spider {

auto compute_layout(
    int window_width,
    int window_height,
    const std::array<std::size_t, 10>& stack_card_counts,
    const std::array<std::size_t, 10>& hidden_card_counts,
    float requested_scroll_offset) -> LayoutMetrics
{
    LayoutMetrics layout {};

    const float width = static_cast<float>(std::max(window_width, 640));
    const float height = static_cast<float>(std::max(window_height, 480));

    layout.outer_margin = std::clamp(width * 0.03F, 16.0F, 48.0F);
    layout.top_controls_height = std::clamp(height * 0.085F, 52.0F, 88.0F);
    layout.stack_gap = std::clamp(width * 0.011F, 8.0F, 22.0F);
    layout.card_width = (width - (layout.outer_margin * 2.0F) - (layout.stack_gap * 9.0F)) / 10.0F;
    layout.card_width = std::max(layout.card_width, 56.0F);
    layout.card_height = layout.card_width * 1.45F;
    const float raw_face_down_step = std::clamp(layout.card_height * 0.10F, 14.0F, 24.0F);
    const float raw_face_up_step = std::clamp(layout.card_height * 0.22F, 26.0F, 52.0F);
    layout.top_controls_bottom = layout.outer_margin + layout.top_controls_height;
    layout.top_row_gap = std::clamp(layout.card_height * 0.12F, 12.0F, 22.0F);
    layout.top_row_y = layout.top_controls_bottom + layout.top_row_gap;
    layout.top_row_bottom = layout.top_row_y + layout.card_height;
    layout.playfield_top = layout.top_row_bottom + layout.top_row_gap;
    layout.playfield_height = height - layout.playfield_top - layout.outer_margin;
    layout.stack_slot_height = layout.card_height + 18.0F;

    float max_extra_height = 0.0F;
    for (std::size_t stack_index = 0; stack_index < layout.stack_x.size(); ++stack_index) {
        const std::size_t total_cards = stack_card_counts[stack_index];
        const std::size_t hidden_cards = std::min(hidden_card_counts[stack_index], total_cards);
        const std::size_t face_up_cards = total_cards - hidden_cards;

        float extra_height = 0.0F;
        if (hidden_cards > 1) {
            extra_height += static_cast<float>(hidden_cards - 1) * raw_face_down_step;
        }
        if (face_up_cards > 1) {
            extra_height += static_cast<float>(face_up_cards - 1) * raw_face_up_step;
        }
        max_extra_height = std::max(max_extra_height, extra_height);
    }

    const float preferred_extra_height = std::max(layout.playfield_height * 1.05F - layout.card_height, layout.card_height * 0.75F);
    const float compression = max_extra_height > 0.0F ? std::min(1.0F, preferred_extra_height / max_extra_height) : 1.0F;
    layout.face_down_step = std::max(10.0F, raw_face_down_step * compression);
    layout.face_up_step = std::max(18.0F, raw_face_up_step * compression);

    float tallest_stack = 0.0F;
    for (std::size_t stack_index = 0; stack_index < layout.stack_x.size(); ++stack_index) {
        layout.stack_x[stack_index] = layout.outer_margin + static_cast<float>(stack_index) * (layout.card_width + layout.stack_gap);

        const std::size_t total_cards = stack_card_counts[stack_index];
        const std::size_t hidden_cards = std::min(hidden_card_counts[stack_index], total_cards);
        const std::size_t face_up_cards = total_cards - hidden_cards;

        float stack_height = layout.card_height;
        if (total_cards > 0) {
            stack_height = layout.card_height;
            if (hidden_cards > 1) {
                stack_height += static_cast<float>(hidden_cards - 1) * layout.face_down_step;
            }
            if (face_up_cards > 1) {
                stack_height += static_cast<float>(face_up_cards - 1) * layout.face_up_step;
            }
        }

        tallest_stack = std::max(tallest_stack, stack_height);
    }

    layout.content_height = layout.stack_slot_height + tallest_stack + layout.outer_margin;
    const float max_scroll = std::max(0.0F, layout.content_height - layout.playfield_height);
    layout.scroll_offset = std::clamp(requested_scroll_offset, 0.0F, max_scroll);

    return layout;
}

} // namespace spider
