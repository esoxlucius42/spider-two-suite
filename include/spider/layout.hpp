#pragma once

#include <array>
#include <cstddef>

namespace spider {

struct LayoutMetrics {
    float outer_margin {};
    float top_controls_height {};
    float top_controls_bottom {};
    float top_row_gap {};
    float top_row_y {};
    float top_row_bottom {};
    float playfield_top {};
    float playfield_height {};
    float card_width {};
    float card_height {};
    float stack_gap {};
    float stack_vertical_offset {};
    float stack_slot_height {};
    float content_height {};
    float scroll_offset {};
    std::array<float, 10> stack_x {};
};

[[nodiscard]] auto compute_layout(
    int window_width,
    int window_height,
    const std::array<std::size_t, 10>& stack_card_counts,
    const std::array<std::size_t, 10>& hidden_card_counts,
    float requested_scroll_offset) -> LayoutMetrics;

} // namespace spider
