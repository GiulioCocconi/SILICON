/*
  Copyright (c) 2026. Giulio Cocconi

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once
#include <ranges>
#include <vector>
#include <version>

#ifndef __cpp_lib_ranges_zip
  #include <range/v3/functional/pipeable.hpp>
  #include <range/v3/view/empty.hpp>
  #include <range/v3/view/sliding.hpp>
  #include <range/v3/view/transform.hpp>
  #include <tuple>
  #include <utility>
#endif

#ifndef __cpp_lib_ranges_slide
  #include <range/v3/view/sliding.hpp>
#endif

#ifndef __cpp_lib_ranges_chunk
  #include <range/v3/view/chunk.hpp>
#endif

namespace SILICON::views {

#ifndef __cpp_lib_ranges_zip
template <std::size_t N>
inline constexpr auto adjacent = ranges::make_pipeable([](auto&& r) {
  if constexpr (N == 0) {
    return ranges::views::empty<std::tuple<>>;
  } else {
    return std::forward<decltype(r)>(r) | ranges::views::sliding(N)
           | ranges::views::transform([](auto window) {
               return [&]<std::size_t... I>(std::index_sequence<I...>) {
                 return std::tuple<decltype(*ranges::next(ranges::begin(window), I))...>(
                     *ranges::next(ranges::begin(window), I)...);
               }(std::make_index_sequence<N>{});
             });
  }
});
#else
template <std::size_t N> inline constexpr auto adjacent = std::views::adjacent<N>;
#endif

#ifndef __cpp_lib_ranges_enumerate
static inline constexpr auto enumerate = [](auto&& r) {
  return std::views::zip(std::views::iota(0, static_cast<ptrdiff_t>(r.size())), r);
};

template <typename R> auto operator|(R&& r, decltype(SILICON::views::enumerate))
{
  return SILICON::views::enumerate(std::forward<R>(r));
}
#else
inline constexpr auto enumerate = std::views::enumerate;
#endif

#ifndef __cpp_lib_ranges_slide
inline constexpr auto slide = ranges::views::sliding;
#else
inline constexpr auto slide = std::views::slide;
#endif

#ifndef __cpp_lib_ranges_chunk
inline constexpr auto chunk = ranges::views::chunk;
#else
inline constexpr auto chunk = std::views::chunk;
#endif

}  // namespace SILICON::views
