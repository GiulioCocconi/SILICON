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

// Stable SILICON technology-cell ABI for Yosys interchange.
//
// These declarations are synthesis black boxes.  Behavioural models live in
// silicon_cells_sim.v and must not be loaded by the default synthesis pipeline.

(* blackbox *)
module SILICON_DFF #(
    parameter CLK_POLARITY = 1
) (
    input D,
    input CLK,
    output Q,
    output QN
);
endmodule

(* blackbox *)
module SILICON_DFFE #(
    parameter CLK_POLARITY = 1,
    parameter EN_POLARITY = 1
) (
    input D,
    input EN,
    input CLK,
    output Q,
    output QN
);
endmodule

(* blackbox *)
module SILICON_DFFSR #(
    parameter CLK_POLARITY = 1,
    parameter SET_POLARITY = 1,
    parameter CLR_POLARITY = 1
) (
    input D,
    input CLK,
    input SET,
    input CLR,
    output Q,
    output QN
);
endmodule

(* blackbox *)
module SILICON_DFFSRE #(
    parameter CLK_POLARITY = 1,
    parameter EN_POLARITY = 1,
    parameter SET_POLARITY = 1,
    parameter CLR_POLARITY = 1
) (
    input D,
    input EN,
    input CLK,
    input SET,
    input CLR,
    output Q,
    output QN
);
endmodule

(* blackbox *)
module SILICON_JKFF #(
    parameter CLK_POLARITY = 1,
    parameter SET_POLARITY = 1,
    parameter CLR_POLARITY = 1
) (
    input J,
    input K,
    input CLK,
    input SET,
    input CLR,
    output Q,
    output QN
);
endmodule

(* blackbox *)
module SILICON_HALF_ADDER (
    input A,
    input B,
    output SUM,
    output COUT
);
endmodule

(* blackbox *)
module SILICON_FULL_ADDER (
    input A,
    input B,
    input CIN,
    output SUM,
    output COUT
);
endmodule

(* blackbox *)
module SILICON_ADDER #(
    parameter WIDTH = 1,
    parameter A_SIGNED = 0,
    parameter B_SIGNED = 0
) (
    input [WIDTH-1:0] A,
    input [WIDTH-1:0] B,
    output [WIDTH-1:0] SUM,
    output COUT
);
endmodule

(* blackbox *)
module SILICON_PIPO #(
    parameter WIDTH = 2,
    parameter CLK_POLARITY = 1,
    parameter EN_POLARITY = 1,
    parameter CLR_POLARITY = 1
) (
    input [WIDTH-1:0] DATA,
    input CLK,
    input EN,
    input CLR,
    output [WIDTH-1:0] OUT
);
endmodule

(* blackbox *)
module SILICON_PISO #(
    parameter WIDTH = 2,
    parameter CLK_POLARITY = 1,
    parameter EN_POLARITY = 1,
    parameter CLR_POLARITY = 1,
    parameter LOAD_POLARITY = 1
) (
    input [WIDTH-1:0] DATA,
    input CLK,
    input EN,
    input CLR,
    input LOAD,
    output OUT
);
endmodule

(* blackbox *)
module SILICON_SIPO #(
    parameter WIDTH = 2,
    parameter CLK_POLARITY = 1,
    parameter EN_POLARITY = 1,
    parameter CLR_POLARITY = 1
) (
    input DATA,
    input CLK,
    input EN,
    input CLR,
    output [WIDTH-1:0] OUT
);
endmodule

(* blackbox *)
module SILICON_SISO #(
    parameter WIDTH = 2,
    parameter CLK_POLARITY = 1,
    parameter EN_POLARITY = 1,
    parameter CLR_POLARITY = 1
) (
    input DATA,
    input CLK,
    input EN,
    input CLR,
    output OUT
);
endmodule
