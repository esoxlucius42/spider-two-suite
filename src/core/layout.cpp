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
    (void)hidden_card_counts;

    const float width = static_cast<float>(std::max(window_width, 640));
    const float height = static_cast<float>(std::max(window_height, 480));

    layout.outer_margin = std::clamp(width * 0.03F, 16.0F, 48.0F);
    layout.top_controls_height = std::clamp(height * 0.085F, 52.0F, 88.0F);
    layout.stack_gap = std::clamp(width * 0.011F, 8.0F, 22.0F);
    layout.card_width = (width - (layout.outer_margin * 2.0F) - (layout.stack_gap * 9.0F)) / 10.0F;
    layout.card_width = std::max(layout.card_width, 56.0F);
    layout.card_height = layout.card_width * 1.45F;
    layout.stack_vertical_offset = layout.card_height / 6.0F;
    layout.top_controls_bottom = layout.outer_margin + layout.top_controls_height;
    layout.top_row_gap = std::clamp(layout.card_height * 0.12F, 12.0F, 22.0F);
    layout.top_row_y = layout.top_controls_bottom + layout.top_row_gap;
    layout.top_row_bottom = layout.top_row_y + layout.card_height;
    layout.playfield_top = layout.top_row_bottom + layout.top_row_gap;
    layout.playfield_height = height - layout.playfield_top - layout.outer_margin;
    layout.stack_slot_height = layout.card_height + 18.0F;

    float tallest_stack = 0.0F;
    for (std::size_t stack_index = 0; stack_index < layout.stack_x.size(); ++stack_index) {
        layout.stack_x[stack_index] = layout.outer_margin + static_cast<float>(stack_index) * (layout.card_width + layout.stack_gap);

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
