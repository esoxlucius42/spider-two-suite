#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace spider {

enum class Suit : std::uint8_t {
    spades,
    hearts,
};

enum class Rank : std::uint8_t {
    ace = 1,
    two,
    three,
    four,
    five,
    six,
    seven,
    eight,
    nine,
    ten,
    jack,
    queen,
    king,
};

struct Card {
    Suit suit {};
    Rank rank {};
    bool face_up {false};
    std::uint8_t copy_index {0};
};

constexpr std::array<Rank, 13> kRanks {
    Rank::ace,
    Rank::two,
    Rank::three,
    Rank::four,
    Rank::five,
    Rank::six,
    Rank::seven,
    Rank::eight,
    Rank::nine,
    Rank::ten,
    Rank::jack,
    Rank::queen,
    Rank::king,
};

constexpr std::array<Suit, 2> kPlayableSuits {
    Suit::spades,
    Suit::hearts,
};

constexpr auto rank_value(Rank rank) -> int
{
    return static_cast<int>(rank);
}

constexpr auto is_one_lower(Rank lower, Rank higher) -> bool
{
    return rank_value(lower) + 1 == rank_value(higher);
}

constexpr auto suit_name(Suit suit) -> std::string_view
{
    switch (suit) {
    case Suit::spades:
        return "spades";
    case Suit::hearts:
        return "hearts";
    }
    return "unknown";
}

} // namespace spider
