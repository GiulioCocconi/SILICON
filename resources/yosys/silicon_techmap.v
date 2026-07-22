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

// Mapping from supported Yosys cells to the stable SILICON technology-cell ABI.

(* techmap_celltype = "$dff" *)
module _silicon_map_dff #(
    parameter WIDTH = 1,
    parameter CLK_POLARITY = 1
) (input CLK, input [WIDTH-1:0] D, output [WIDTH-1:0] Q);
  generate
    if (WIDTH == 1) begin
      wire qn;
      SILICON_DFF #(.CLK_POLARITY(CLK_POLARITY))
        _TECHMAP_REPLACE_ (.D(D), .CLK(CLK), .Q(Q), .QN(qn));
    end else begin
      wire _TECHMAP_FAIL_ = 1;
    end
  endgenerate
endmodule

(* techmap_celltype = "$dffe" *)
module _silicon_map_dffe #(
    parameter WIDTH = 1,
    parameter CLK_POLARITY = 1,
    parameter EN_POLARITY = 1
) (input CLK, input EN, input [WIDTH-1:0] D, output [WIDTH-1:0] Q);
  generate
    if (WIDTH == 1) begin
      wire qn;
      SILICON_DFFE #(.CLK_POLARITY(CLK_POLARITY), .EN_POLARITY(EN_POLARITY))
        _TECHMAP_REPLACE_ (.D(D), .EN(EN), .CLK(CLK), .Q(Q), .QN(qn));
    end else begin
      wire _TECHMAP_FAIL_ = 1;
    end
  endgenerate
endmodule

(* techmap_celltype = "$dffsr" *)
module _silicon_map_dffsr #(
    parameter WIDTH = 1,
    parameter CLK_POLARITY = 1,
    parameter SET_POLARITY = 1,
    parameter CLR_POLARITY = 1
) (
    input CLK,
    input [WIDTH-1:0] SET,
    input [WIDTH-1:0] CLR,
    input [WIDTH-1:0] D,
    output [WIDTH-1:0] Q
);
  generate
    if (WIDTH == 1) begin
      wire qn;
      SILICON_DFFSR #(
          .CLK_POLARITY(CLK_POLARITY),
          .SET_POLARITY(SET_POLARITY),
          .CLR_POLARITY(CLR_POLARITY)
      ) _TECHMAP_REPLACE_ (.D(D), .CLK(CLK), .SET(SET), .CLR(CLR), .Q(Q), .QN(qn));
    end else begin
      wire _TECHMAP_FAIL_ = 1;
    end
  endgenerate
endmodule

(* techmap_celltype = "$dffsre" *)
module _silicon_map_dffsre #(
    parameter WIDTH = 1,
    parameter CLK_POLARITY = 1,
    parameter EN_POLARITY = 1,
    parameter SET_POLARITY = 1,
    parameter CLR_POLARITY = 1
) (
    input CLK,
    input EN,
    input [WIDTH-1:0] SET,
    input [WIDTH-1:0] CLR,
    input [WIDTH-1:0] D,
    output [WIDTH-1:0] Q
);
  generate
    if (WIDTH == 1) begin
      wire qn;
      SILICON_DFFSRE #(
          .CLK_POLARITY(CLK_POLARITY),
          .EN_POLARITY(EN_POLARITY),
          .SET_POLARITY(SET_POLARITY),
          .CLR_POLARITY(CLR_POLARITY)
      ) _TECHMAP_REPLACE_ (
          .D(D), .EN(EN), .CLK(CLK), .SET(SET), .CLR(CLR), .Q(Q), .QN(qn)
      );
    end else begin
      wire _TECHMAP_FAIL_ = 1;
    end
  endgenerate
endmodule

(* techmap_celltype = "$add" *)
module _silicon_map_add #(
    parameter A_SIGNED = 0,
    parameter B_SIGNED = 0,
    parameter A_WIDTH = 1,
    parameter B_WIDTH = 1,
    parameter Y_WIDTH = 1
) (
    input [A_WIDTH-1:0] A,
    input [B_WIDTH-1:0] B,
    output [Y_WIDTH-1:0] Y
);
  generate
    if (!A_SIGNED && !B_SIGNED && A_WIDTH == B_WIDTH && Y_WIDTH == A_WIDTH) begin
      wire cout;
      SILICON_ADDER #(.WIDTH(A_WIDTH), .A_SIGNED(0), .B_SIGNED(0))
        _TECHMAP_REPLACE_ (.A(A), .B(B), .SUM(Y), .COUT(cout));
    end else if (!A_SIGNED && !B_SIGNED && A_WIDTH == B_WIDTH &&
                 Y_WIDTH == A_WIDTH + 1) begin
      SILICON_ADDER #(.WIDTH(A_WIDTH), .A_SIGNED(0), .B_SIGNED(0))
        _TECHMAP_REPLACE_ (.A(A), .B(B), .SUM(Y[A_WIDTH-1:0]), .COUT(Y[A_WIDTH]));
    end else begin
      wire _TECHMAP_FAIL_ = 1;
    end
  endgenerate
endmodule

(* techmap_celltype = "$fa" *)
module _silicon_map_fa #(
    parameter WIDTH = 1,
    parameter [WIDTH-1:0] _TECHMAP_CONSTMSK_C_ = 0,
    parameter [WIDTH-1:0] _TECHMAP_CONSTVAL_C_ = 0
) (
    input [WIDTH-1:0] A,
    input [WIDTH-1:0] B,
    input [WIDTH-1:0] C,
    output [WIDTH-1:0] X,
    output [WIDTH-1:0] Y
);
  generate
    if (WIDTH != 1) begin
      wire _TECHMAP_FAIL_ = 1;
    end else if (_TECHMAP_CONSTMSK_C_[0] && !_TECHMAP_CONSTVAL_C_[0]) begin
      SILICON_HALF_ADDER _TECHMAP_REPLACE_ (.A(A), .B(B), .SUM(Y), .COUT(X));
    end else begin
      SILICON_FULL_ADDER _TECHMAP_REPLACE_ (
          .A(A), .B(B), .CIN(C), .SUM(Y), .COUT(X)
      );
    end
  endgenerate
endmodule

// extract_fa operates on fine-grained gates.  Convert unmatched primitives back
// to the generic cells handled by SILICON's compatibility importer.
(* techmap_celltype = "$_AND_" *)
module _silicon_unmap_and(input A, input B, output Y);
  assign Y = A & B;
endmodule

(* techmap_celltype = "$_OR_" *)
module _silicon_unmap_or(input A, input B, output Y);
  assign Y = A | B;
endmodule

(* techmap_celltype = "$_XOR_" *)
module _silicon_unmap_xor(input A, input B, output Y);
  assign Y = A ^ B;
endmodule

(* techmap_celltype = "$_NOT_" *)
module _silicon_unmap_not(input A, output Y);
  assign Y = ~A;
endmodule
