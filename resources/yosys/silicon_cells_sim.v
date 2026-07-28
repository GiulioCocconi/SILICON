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

// Behavioural simulation models for the SILICON technology-cell ABI.
// Controls have priority CLR, SET, clock/enable.  Simultaneously active SET
// and CLR produce X.  QN is always the logical complement of Q (and therefore
// X whenever Q is X).  SILICON's propagation-delay properties are intentionally
// absent from the interoperability ABI.

module SILICON_DFF #(
    parameter CLK_POLARITY = 1
) (
    input D, input CLK, output reg Q, output QN
);
  assign QN = ~Q;
  generate
    if (CLK_POLARITY) always @(posedge CLK) Q <= D;
    else              always @(negedge CLK) Q <= D;
  endgenerate
endmodule

module SILICON_DFFE #(
    parameter CLK_POLARITY = 1,
    parameter EN_POLARITY = 1
) (
    input D, input EN, input CLK, output reg Q, output QN
);
  assign QN = ~Q;
  generate
    if (CLK_POLARITY) always @(posedge CLK) begin
      if (EN === EN_POLARITY) Q <= D;
      else if (EN !== !EN_POLARITY) Q <= 1'bx;
    end
    else always @(negedge CLK) begin
      if (EN === EN_POLARITY) Q <= D;
      else if (EN !== !EN_POLARITY) Q <= 1'bx;
    end
  endgenerate
endmodule

module SILICON_DLATCH #(
    parameter EN_POLARITY = 1
) (
    input D, input EN, output reg Q, output QN
);
  assign QN = ~Q;
  always @* begin
    if (EN === EN_POLARITY) Q <= D;
    else if (EN !== !EN_POLARITY) Q <= 1'bx;
  end
endmodule

module SILICON_DFFSR #(
    parameter CLK_POLARITY = 1,
    parameter SET_POLARITY = 1,
    parameter CLR_POLARITY = 1
) (
    input D, input CLK, input SET, input CLR, output reg Q, output QN
);
  assign QN = ~Q;
  wire set_active = SET === SET_POLARITY;
  wire clr_active = CLR === CLR_POLARITY;
  wire set_inactive = SET === !SET_POLARITY;
  wire clr_inactive = CLR === !CLR_POLARITY;
  reg last_clk;
  initial last_clk = CLK;
  wire selected_edge = CLK_POLARITY
      ? (last_clk === 1'b0 && CLK === 1'b1)
      : (last_clk === 1'b1 && CLK === 1'b0);
  always @(CLK or SET or CLR) begin
    if (clr_active && set_inactive) Q <= 1'b0;
    else if (clr_inactive && set_active) Q <= 1'b1;
    else if (!clr_inactive || !set_inactive) Q <= 1'bx;
    else if (selected_edge) Q <= D;
    last_clk = CLK;
  end
endmodule

module SILICON_DFFSRE #(
    parameter CLK_POLARITY = 1,
    parameter EN_POLARITY = 1,
    parameter SET_POLARITY = 1,
    parameter CLR_POLARITY = 1
) (
    input D, input EN, input CLK, input SET, input CLR, output reg Q, output QN
);
  assign QN = ~Q;
  wire set_active = SET === SET_POLARITY;
  wire clr_active = CLR === CLR_POLARITY;
  wire set_inactive = SET === !SET_POLARITY;
  wire clr_inactive = CLR === !CLR_POLARITY;
  reg last_clk;
  initial last_clk = CLK;
  wire selected_edge = CLK_POLARITY
      ? (last_clk === 1'b0 && CLK === 1'b1)
      : (last_clk === 1'b1 && CLK === 1'b0);
  always @(CLK or SET or CLR) begin
    if (clr_active && set_inactive) Q <= 1'b0;
    else if (clr_inactive && set_active) Q <= 1'b1;
    else if (!clr_inactive || !set_inactive) Q <= 1'bx;
    else if (selected_edge && EN === EN_POLARITY) Q <= D;
    else if (selected_edge && EN !== !EN_POLARITY) Q <= 1'bx;
    last_clk = CLK;
  end
endmodule

module SILICON_JKFF #(
    parameter CLK_POLARITY = 1,
    parameter SET_POLARITY = 1,
    parameter CLR_POLARITY = 1
) (
    input J, input K, input CLK, input SET, input CLR, output reg Q, output QN
);
  assign QN = ~Q;
  wire set_active = SET === SET_POLARITY;
  wire clr_active = CLR === CLR_POLARITY;
  wire set_inactive = SET === !SET_POLARITY;
  wire clr_inactive = CLR === !CLR_POLARITY;
  reg last_clk;
  initial last_clk = CLK;
  wire selected_edge = CLK_POLARITY
      ? (last_clk === 1'b0 && CLK === 1'b1)
      : (last_clk === 1'b1 && CLK === 1'b0);
  always @(CLK or SET or CLR) begin
    if (clr_active && set_inactive) Q <= 1'b0;
    else if (clr_inactive && set_active) Q <= 1'b1;
    else if (!clr_inactive || !set_inactive) Q <= 1'bx;
    else if (selected_edge) begin
      case ({J, K})
        2'b00: Q <= Q;
        2'b01: Q <= 1'b0;
        2'b10: Q <= 1'b1;
        2'b11: Q <= ~Q;
        default: Q <= 1'bx;
      endcase
    end
    last_clk = CLK;
  end
endmodule

module SILICON_HALF_ADDER(input A, input B, output SUM, output COUT);
  assign SUM = A ^ B;
  assign COUT = A & B;
endmodule

module SILICON_FULL_ADDER(
    input A, input B, input CIN, output SUM, output COUT
);
  assign SUM = A ^ B ^ CIN;
  assign COUT = (A & B) | (A & CIN) | (B & CIN);
endmodule

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
  assign {COUT, SUM} = {1'b0, A} + {1'b0, B};
endmodule

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
  reg [WIDTH-1:0] state;
  assign OUT = state;
  wire clear_active = CLR === CLR_POLARITY;
  wire clear_inactive = CLR === !CLR_POLARITY;
  reg last_clk;
  initial last_clk = CLK;
  wire selected_edge = CLK_POLARITY
      ? (last_clk === 1'b0 && CLK === 1'b1)
      : (last_clk === 1'b1 && CLK === 1'b0);
  always @(CLK or CLR or EN or DATA) begin
    if (clear_active) state <= {WIDTH{1'b0}};
    else if (!clear_inactive) state <= {WIDTH{1'bx}};
    else if (EN !== EN_POLARITY && EN !== !EN_POLARITY)
      state <= {WIDTH{1'bx}};
    else if (selected_edge && EN === EN_POLARITY) state <= DATA;
    last_clk = CLK;
  end
endmodule

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
  reg [WIDTH-1:0] state;
  assign OUT = state[0];
  wire clear_active = CLR === CLR_POLARITY;
  wire clear_inactive = CLR === !CLR_POLARITY;
  reg last_clk;
  initial last_clk = CLK;
  wire selected_edge = CLK_POLARITY
      ? (last_clk === 1'b0 && CLK === 1'b1)
      : (last_clk === 1'b1 && CLK === 1'b0);
  always @(CLK or CLR or EN or DATA or LOAD) begin
    if (clear_active) state <= {WIDTH{1'b0}};
    else if (!clear_inactive) state <= {WIDTH{1'bx}};
    else if (EN !== EN_POLARITY && EN !== !EN_POLARITY)
      state <= {WIDTH{1'bx}};
    else if (selected_edge && EN === EN_POLARITY) begin
      if (LOAD === LOAD_POLARITY) state <= DATA;
      else if (LOAD !== !LOAD_POLARITY) state <= {WIDTH{1'bx}};
      else state <= {1'b0, state[WIDTH-1:1]};
    end
    last_clk = CLK;
  end
endmodule

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
  reg [WIDTH-1:0] state;
  assign OUT = state;
  wire clear_active = CLR === CLR_POLARITY;
  wire clear_inactive = CLR === !CLR_POLARITY;
  reg last_clk;
  initial last_clk = CLK;
  wire selected_edge = CLK_POLARITY
      ? (last_clk === 1'b0 && CLK === 1'b1)
      : (last_clk === 1'b1 && CLK === 1'b0);
  always @(CLK or CLR or EN or DATA) begin
    if (clear_active) state <= {WIDTH{1'b0}};
    else if (!clear_inactive) state <= {WIDTH{1'bx}};
    else if (EN !== EN_POLARITY && EN !== !EN_POLARITY)
      state <= {WIDTH{1'bx}};
    else if (selected_edge && EN === EN_POLARITY)
      state <= {DATA, state[WIDTH-1:1]};
    last_clk = CLK;
  end
endmodule

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
  reg [WIDTH-1:0] state;
  assign OUT = state[0];
  wire clear_active = CLR === CLR_POLARITY;
  wire clear_inactive = CLR === !CLR_POLARITY;
  reg last_clk;
  initial last_clk = CLK;
  wire selected_edge = CLK_POLARITY
      ? (last_clk === 1'b0 && CLK === 1'b1)
      : (last_clk === 1'b1 && CLK === 1'b0);
  always @(CLK or CLR or EN or DATA) begin
    if (clear_active) state <= {WIDTH{1'b0}};
    else if (!clear_inactive) state <= {WIDTH{1'bx}};
    else if (EN !== EN_POLARITY && EN !== !EN_POLARITY)
      state <= {WIDTH{1'bx}};
    else if (selected_edge && EN === EN_POLARITY)
      state <= {DATA, state[WIDTH-1:1]};
    last_clk = CLK;
  end
endmodule
