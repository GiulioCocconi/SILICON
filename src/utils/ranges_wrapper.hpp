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

#ifndef __cpp_lib_ranges_slide
  #include <range/v3/view/sliding.hpp>
#endif

namespace silicon::views {

#ifndef __cpp_lib_ranges_enumerate
static inline constexpr auto enumerate = [](auto&& r) {
  return std::views::zip(std::views::iota(0, static_cast<ptrdiff_t>(r.size())), r);
};

template <typename R> auto operator|(R&& r, decltype(silicon::views::enumerate))
{
  return silicon::views::enumerate(std::forward<R>(r));
}
#else
inline constexpr auto enumerate = std::views::enumerate;
#endif

#ifndef __cpp_lib_ranges_slide
inline constexpr auto slide = ranges::views::sliding;
#else
inline constexpr auto slide = std::views::slide;
#endif

}  // namespace silicon::views
