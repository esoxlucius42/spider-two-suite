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
    constexpr float kCardAspectRatio = 1.45F;

    LayoutMetrics layout {};
    (void)hidden_card_counts;

    const float width = static_cast<float>(std::max(window_width, 1));
    const float height = static_cast<float>(std::max(window_height, 1));
    const float preferred_card_height = height / 6.0F;
    const float preferred_card_width = preferred_card_height / kCardAspectRatio;

    layout.outer_margin = width * 0.02F;
    layout.top_controls_height = height / 16.0F;
    layout.stack_gap = width * 0.016F;
    const float available_tableau_width = std::max(0.0F, width - (layout.outer_margin * 2.0F) - (layout.stack_gap * 9.0F));
    layout.card_width = std::min(preferred_card_width, available_tableau_width / 10.0F);
    layout.card_height = preferred_card_height;
    layout.stack_vertical_offset = layout.card_height / 6.0F;
    layout.top_controls_bottom = layout.top_controls_height;
    layout.top_row_gap = layout.card_height * 0.1F;
    layout.top_row_y = layout.top_controls_bottom + layout.top_row_gap;
    layout.top_row_bottom = layout.top_row_y + layout.card_height;
    layout.playfield_top = layout.top_row_bottom + layout.top_row_gap;
    layout.playfield_height = std::max(0.0F, height - layout.playfield_top - layout.outer_margin);
    layout.stack_slot_height = layout.card_height + layout.top_row_gap;
    layout.top_row_slot_x[0] = layout.outer_margin;

    const float completed_row_width = layout.card_width * 8.0F + layout.stack_gap * 7.0F;
    const float completed_row_start = std::max(
        layout.top_row_slot_x[0] + layout.card_width + layout.stack_gap,
        width - layout.outer_margin - completed_row_width);
    for (std::size_t slot_index = 1; slot_index < layout.top_row_slot_x.size(); ++slot_index) {
        layout.top_row_slot_x[slot_index] =
            completed_row_start + static_cast<float>(slot_index - 1) * (layout.card_width + layout.stack_gap);
    }

    const float total_tableau_width = layout.card_width * 10.0F + layout.stack_gap * 9.0F;
    const float centered_tableau_start = layout.outer_margin
        + std::max(0.0F, width - (layout.outer_margin * 2.0F) - total_tableau_width) / 2.0F;

    float tallest_stack = 0.0F;
    for (std::size_t stack_index = 0; stack_index < layout.stack_x.size(); ++stack_index) {
        layout.stack_x[stack_index] =
            centered_tableau_start + static_cast<float>(stack_index) * (layout.card_width + layout.stack_gap);

        const std::size_t total_cards = stack_card_counts[stack_index];

        float stack_height = layout.card_height;
        if (total_cards > 0) {
            stack_height = layout.card_height;
            stack_height += static_cast<float>(total_cards - 1) * layout.stack_vertical_offset;
        }

        tallest_stack = std::max(tallest_stack, stack_height);
    }

    layout.content_height = layout.stack_slot_height + tallest_stack + layout.outer_margin;
    const float max_scroll = std::max(0.0F, layout.content_height - layout.playfield_height);
    layout.scroll_offset = std::clamp(requested_scroll_offset, 0.0F, max_scroll);

    return layout;
}

} // namespace spider
