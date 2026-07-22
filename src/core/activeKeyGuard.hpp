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

#include <format>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

class ActiveKeyGuard {
public:
  ActiveKeyGuard(std::vector<std::string>& stack, std::string key,
                 const std::string_view errorPrefix)
      : stack(stack)
  {
    if (std::ranges::contains(stack, key)) {
      std::ostringstream trace;
      for (const auto& value : stack)
        trace << value << " -> ";
      trace << key;
      throw std::runtime_error(std::format("{}{}", errorPrefix, trace.str()));
    }
    stack.push_back(std::move(key));
    pushed = true;
  }

  ~ActiveKeyGuard()
  {
    if (pushed)
      stack.pop_back();
  }

  ActiveKeyGuard(const ActiveKeyGuard&)            = delete;
  ActiveKeyGuard& operator=(const ActiveKeyGuard&) = delete;

private:
  std::vector<std::string>& stack;
  bool                      pushed = false;
};
