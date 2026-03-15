#pragma once

#include "spider/card.hpp"

namespace spider {

constexpr auto atlas_row_for_suit(Suit suit) -> int
{
    switch (suit) {
    case Suit::spades:
        return 3;
    case Suit::hearts:
        return 2;
    }

    return 3;
}

} // namespace spider
