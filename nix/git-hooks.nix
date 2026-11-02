#  Copyright (c) 2026. Giulio Cocconi
#
#  This program is free software: you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation, either version 3 of the License, or
#  (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program.  If not, see <http://www.gnu.org/licenses/>.
{ pkgs, git-hooks, system }:
git-hooks.lib.${system}.run {
  src = ../.;
  hooks = {
    format-code = {
      enable = true;
      name = "Check staged C++ formatting";
      entry = "${pkgs.python3}/bin/python ${../ci/git-clang-format} --diff --staged --extensions cpp,hpp --binary ${pkgs.clang-tools}/bin/clang-format --";
      files = "\\.(cpp|hpp)$";
      stages = [ "pre-commit" ];
    };

    pr-compliance = {
      enable = true;
      name = "Check PR compliance";
      entry = "${pkgs.python3}/bin/python ${../ci/check-pr-compliance.py}";
      pass_filenames = false;
      stages = [ "manual" ];
      always_run = true;
    };
  };
}
