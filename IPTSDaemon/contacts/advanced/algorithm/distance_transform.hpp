#pragma once

#include <common/types.hpp>
#include <container/image.hpp>

#include <queue>
#include <numeric>

using namespace iptsd::container;


namespace iptsd::contacts::advanced::alg {
namespace wdt {

template<typename T>
struct QItem {
    index_t idx;
    T cost;
};

template<typename T>
auto operator== (struct QItem<T> const& a, struct QItem<T> const& b) noexcept -> bool
{
    return a.cost == b.cost;
}

template<typename T>
auto operator!= (struct QItem<T> const& a, struct QItem<T> const& b) noexcept -> bool
{
    return a.cost != b.cost;
}

template<typename T>
auto operator<= (struct QItem<T> const& a, struct QItem<T> const& b) noexcept -> bool
{
    return a.cost <= b.cost;
}

template<typename T>
auto operator>= (struct QItem<T> const& a, struct QItem<T> const& b) noexcept -> bool
{
    return a.cost >= b.cost;
}

template<typename T>
auto operator< (struct QItem<T> const& a, struct QItem<T> const& b) noexcept -> bool
{
    return a.cost < b.cost;
}

template<typename T>
auto operator> (struct QItem<T> const& a, struct QItem<T> const& b) noexcept -> bool
{
    return a.cost > b.cost;
}


namespace impl {

template<typename T>
auto is_masked(T& mask, index_t i) -> bool
{
    return !mask(i);
}

template<typename T>
auto is_foreground(T& bin, index_t i) -> bool
{
    return bin(i);
}

template<typename B, typename M>
auto is_compute(B& bin, M& mask, index_t i) -> bool
{
    return !is_foreground(bin, i) && !is_masked(mask, i);
}

template<index_t DX, index_t DY, typename T, typename Q, typename B, typename M, typename C>
inline void evaluate(Image<T>& out, Q& queue, B& bin, M& mask, C& cost, index_t i,
                     index_t stride, T limit)
{
    if (!is_compute(bin, mask, i + stride))
        return;

    auto const c = out[i] + cost.template get_cost<DX, DY>(i);

    if (c < out[i + stride] && c < limit) {
        queue.push({ i + stride, c });
    }
}

} /* namespace impl */
} /* namespace wdt */


template<int N=8, typename T, typename F, typename M, typename C, typename Q>
void weighted_distance_transform(Image<T>& out, F& bin, M& mask, C& cost, Q& q,
                                 T limit=std::numeric_limits<T>::max())
{
    using wdt::impl::evaluate;
    using wdt::impl::is_foreground;
    using wdt::impl::is_masked;

    static_assert(N == 4 || N == 8);

    // strides
    index_t const s_left      = -1;
    index_t const s_right     =  1;
    index_t const s_top       = -out.stride();
    index_t const s_top_left  = s_top + s_left;
    index_t const s_top_right = s_top + s_right;
    index_t const s_bot       = -s_top;
    index_t const s_bot_left  = s_bot + s_left;
    index_t const s_bot_right = s_bot + s_right;

    // step 1: initialize output, queue all non-masked pixels
    index_t i = 0;

    // x = 0, y = 0
    if (!is_foreground(bin, i)) {
        out[i] = std::numeric_limits<T>::max();

        if (!is_masked(mask, i)) {
            auto c = std::numeric_limits<T>::max();

            if (is_foreground(bin, i + s_right)) {
                c = std::min(c, cost.template get_cost<-1, 0>(i + s_right));
            }

            if (is_foreground(bin, i + s_bot)) {
                c = std::min(c, cost.template get_cost<0, -1>(i + s_bot));
            }

            if (N == 8 && is_foreground(bin, i + s_bot_right)) {
                c = std::min(c, cost.template get_cost<-1, -1>(i + s_bot_right));
            }

            if (c < limit) {
                q.push({ i, c });
            }
        }
    } else {
        out[i] = static_cast<T>(0);
    }
    ++i;

    // 0 < x < n - 1, y = 0
    for (; i < out.size().x - 1; ++i) {
        if (is_foreground(bin, i)) {
            out[i] = static_cast<T>(0);
            continue;
        }

        out[i] = std::numeric_limits<T>::max();

        if (is_masked(mask, i))
            continue;

        auto c = std::numeric_limits<T>::max();

        if (is_foreground(bin, i + s_left)) {
            c = std::min(c, cost.template get_cost<1, 0>(i + s_left));
        }

        if (is_foreground(bin, i + s_right)) {
            c = std::min(c, cost.template get_cost<-1, 0>(i + s_right));
        }

        if (N == 8 && is_foreground(bin, i + s_bot_left)) {
            c = std::min(c, cost.template get_cost<1, -1>(i + s_bot_left));
        }

        if (is_foreground(bin, i + s_bot)) {
            c = std::min(c, cost.template get_cost<0, -1>(i + s_bot));
        }

        if (N == 8 && is_foreground(bin, i + s_bot_right)) {
            c = std::min(c, cost.template get_cost<-1, -1>(i + s_bot_right));
        }

        if (c < limit) {
            q.push({ i, c });
        }
    }

    // x = n - 1, y = 0
    if (!is_foreground(bin, i)) {
        out[i] = std::numeric_limits<T>::max();

        if (!is_masked(mask, i)) {
            auto c = std::numeric_limits<T>::max();

            if (is_foreground(bin, i + s_left)) {
                c = std::min(c, cost.template get_cost<1, 0>(i + s_left));
            }

            if (N == 8 && is_foreground(bin, i + s_bot_left)) {
                c = std::min(c, cost.template get_cost<1, -1>(i + s_bot_left));
            }

            if (is_foreground(bin, i + s_bot)) {
                c = std::min(c, cost.template get_cost<0, -1>(i + s_bot));
            }

            if (c < limit) {
                q.push({ i, c });
            }
        }
    } else {
        out[i] = static_cast<T>(0);
    }
    ++i;

    // 0 < y < n - 1
    while (i < out.size().x * (out.size().y - 1)) {
        // x = 0
        if (!is_foreground(bin, i)) {
            out[i] = std::numeric_limits<T>::max();

            if (!is_masked(mask, i)) {
                auto c = std::numeric_limits<T>::max();

                if (is_foreground(bin, i + s_right)) {
                    c = std::min(c, cost.template get_cost<-1, 0>(i + s_right));
                }

                if (is_foreground(bin, i + s_top)) {
                    c = std::min(c, cost.template get_cost<0, 1>(i + s_top));
                }

                if (N == 8 && is_foreground(bin, i + s_top_right)) {
                    c = std::min(c, cost.template get_cost<-1, 1>(i + s_top_right));
                }

                if (is_foreground(bin, i + s_bot)) {
                    c = std::min(c, cost.template get_cost<0, -1>(i + s_bot));
                }

                if (N == 8 && is_foreground(bin, i + s_bot_right)) {
                    c = std::min(c, cost.template get_cost<-1, -1>(i + s_bot_right));
                }

                if (c < limit) {
                    q.push({ i, c });
                }
            }
        } else {
            out[i] = static_cast<T>(0);
        }
        ++i;

        // 0 < x < n - 1
        auto const limit = i + out.size().x - 2;
        for (; i < limit; ++i) {
            // if this is a foreground pixel, set it to zero and skip the rest
            if (is_foreground(bin, i)) {
                out[i] = static_cast<T>(0);
                continue;
            }

            // initialize all background pixels to maximum
            out[i] = std::numeric_limits<T>::max();

            // don't evaluate pixels that are excluded by mask
            if (is_masked(mask, i))
                continue;

            // compute minimum cost to any neighboring foreground pixel, if available
            auto c = std::numeric_limits<T>::max();

            if (is_foreground(bin, i + s_left)) {
                c = std::min(c, cost.template get_cost<1, 0>(i + s_left));
            }

            if (is_foreground(bin, i + s_right)) {
                c = std::min(c, cost.template get_cost<-1, 0>(i + s_right));
            }

            if (N == 8 && is_foreground(bin, i + s_top_left)) {
                c = std::min(c, cost.template get_cost<1, 1>(i + s_top_left));
            }

            if (is_foreground(bin, i + s_top)) {
                c = std::min(c, cost.template get_cost<0, 1>(i + s_top));
            }

            if (N == 8 && is_foreground(bin, i + s_top_right)) {
                c = std::min(c, cost.template get_cost<-1, 1>(i + s_top_right));
            }

            if (N == 8 && is_foreground(bin, i + s_bot_left)) {
                c = std::min(c, cost.template get_cost<1, -1>(i + s_bot_left));
            }

            if (is_foreground(bin, i + s_bot)) {
                c = std::min(c, cost.template get_cost<0, -1>(i + s_bot));
            }

            if (N == 8 && is_foreground(bin, i + s_bot_right)) {
                c = std::min(c, cost.template get_cost<-1, -1>(i + s_bot_right));
            }

            // if we have a finite projected cost, add this pixel
            if (c < limit) {
                q.push({ i, c });
            }
        }

        // x = n - 1
        if (!is_foreground(bin, i)) {
            out[i] = std::numeric_limits<T>::max();

            if (!is_masked(mask, i)) {
                auto c = std::numeric_limits<T>::max();

                if (is_foreground(bin, i + s_left)) {
                    c = std::min(c, cost.template get_cost<1, 0>(i + s_left));
                }

                if (N == 8 && is_foreground(bin, i + s_top_left)) {
                    c = std::min(c, cost.template get_cost<1, 1>(i + s_top_left));
                }

                if (is_foreground(bin, i + s_top)) {
                    c = std::min(c, cost.template get_cost<0, 1>(i + s_top));
                }

                if (N == 8 && is_foreground(bin, i + s_bot_left)) {
                    c = std::min(c, cost.template get_cost<1, -1>(i + s_bot_left));
                }

                if (is_foreground(bin, i + s_bot)) {
                    c = std::min(c, cost.template get_cost<0, -1>(i + s_bot));
                }

                if (c < limit) {
                    q.push({ i, c });
                }
            }
        } else {
            out[i] = static_cast<T>(0);
        }
        ++i;
    }

    // x = 0, y = n - 1
    if (!is_foreground(bin, i)) {
        out[i] = std::numeric_limits<T>::max();

        if (!is_masked(mask, i)) {
            auto c = std::numeric_limits<T>::max();

            if (is_foreground(bin, i + s_right)) {
                c = std::min(c, cost.template get_cost<-1, 0>(i + s_right));
            }

            if (is_foreground(bin, i + s_top)) {
                c = std::min(c, cost.template get_cost<0, 1>(i + s_top));
            }

            if (N == 8 && is_foreground(bin, i + s_top_right)) {
                c = std::min(c, cost.template get_cost<-1, 1>(i + s_top_right));
            }

            if (c < limit) {
                q.push({ i, c });
            }
        }
    } else {
        out[i] = static_cast<T>(0);
    }
    ++i;

    // 0 < x < n - 1, y = n - 1
    for (; i < out.size().span() - 1; ++i) {
        if (is_foreground(bin, i)) {
            out[i] = static_cast<T>(0);
            continue;
        }

        out[i] = std::numeric_limits<T>::max();

        if (is_masked(mask, i))
            continue;

        auto c = std::numeric_limits<T>::max();

        if (is_foreground(bin, i + s_left)) {
            c = std::min(c, cost.template get_cost<1, 0>(i + s_left));
        }

        if (is_foreground(bin, i + s_right)) {
            c = std::min(c, cost.template get_cost<-1, 0>(i + s_right));
        }

        if (N == 8 && is_foreground(bin, i + s_top_left)) {
            c = std::min(c, cost.template get_cost<1, 1>(i + s_top_left));
        }

        if (is_foreground(bin, i + s_top)) {
            c = std::min(c, cost.template get_cost<0, 1>(i + s_top));
        }

        if (N == 8 && is_foreground(bin, i + s_top_right)) {
            c = std::min(c, cost.template get_cost<-1, 1>(i + s_top_right));
        }

        if (c < limit) {
            q.push({ i, c });
        }
    }

    // x = n - 1, y = n - 1
    if (!is_foreground(bin, i)) {
        out[i] = std::numeric_limits<T>::max();

        if (!is_masked(mask, i)) {
            auto c = std::numeric_limits<T>::max();

            if (is_foreground(bin, i + s_left)) {
                c = std::min(c, cost.template get_cost<1, 0>(i + s_left));
            }

            if (N == 8 && is_foreground(bin, i + s_top_left)) {
                c = std::min(c, cost.template get_cost<1, 1>(i + s_top_left));
            }

            if (is_foreground(bin, i + s_top)) {
                c = std::min(c, cost.template get_cost<0, 1>(i + s_top));
            }

            if (c < limit) {
                q.push({ i, c });
            }
        }
    } else {
        out[i] = static_cast<T>(0);
    }

    // step 2: while queue is not empty, get next pixel, write down cost, and add neighbors
    while (!q.empty()) {
        // get next pixel and remove it from queue
        const wdt::QItem<T> pixel = q.top();
        q.pop();

        // check if someone has been here before; if so, skip this one
        if (out[pixel.idx] <= pixel.cost)
            continue;

        // no one has been here before, so we're guaranteed to be on the lowes cost path
        out[pixel.idx] = pixel.cost;

        // evaluate neighbors
        auto const [x, y] = Image<T>::unravel(out.size(), pixel.idx);

        if (x > 0) {
            evaluate<-1, 0>(out, q, bin, mask, cost, pixel.idx, s_left, limit);
        }

        if (x < out.size().x - 1) {
            evaluate<1, 0>(out, q, bin, mask, cost, pixel.idx, s_right, limit);
        }

        if (y > 0) {
            if (N == 8 && x > 0) {
                evaluate<-1, -1>(out, q, bin, mask, cost, pixel.idx, s_top_left, limit);
            }

            evaluate<0, -1>(out, q, bin, mask, cost, pixel.idx, s_top, limit);

            if (N == 8 && x < out.size().x - 1) {
                evaluate<1, -1>(out, q, bin, mask, cost, pixel.idx, s_top_right, limit);
            }
        }

        if (y < out.size().y - 1) {
            if (N == 8 && x > 0) {
                evaluate<-1, 1>(out, q, bin, mask, cost, pixel.idx, s_bot_left, limit);
            }

            evaluate<0, 1>(out, q, bin, mask, cost, pixel.idx, s_bot, limit);

            if (N == 8 && x < out.size().x - 1) {
                evaluate<1, 1>(out, q, bin, mask, cost, pixel.idx, s_bot_right, limit);
            }
        }
    }
}

} /* namespace iptsd::contacts::advanced::alg */
