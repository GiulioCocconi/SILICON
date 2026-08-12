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

// Single source for the SILICON technology-cell ABI.
//
// Default: behavioural models for standalone four-state simulation.
// SILICON_BLACKBOX: synthesis black-box declarations.
// SILICON_EXPORT_MAP: export-only mappings to native Yosys RTL cells.

`ifdef SILICON_BLACKBOX
  `define SILICON_CELL(module_name, map_name, cell_type) \
    (* blackbox *) module module_name
`elsif SILICON_EXPORT_MAP
  `define SILICON_CELL(module_name, map_name, cell_type) \
    (* techmap_celltype = cell_type *) module map_name
`else
  `define SILICON_CELL(module_name, map_name, cell_type) module module_name
`endif

`SILICON_CELL(SILICON_DFF, _silicon_export_dff, "SILICON_DFF") #(
    parameter CLK_POLARITY = 1
) (
    input D, input CLK, output Q, output QN
);
`ifndef SILICON_BLACKBOX
  `ifdef SILICON_EXPORT_MAP
    \$dff #(
        .WIDTH(1), .CLK_POLARITY(CLK_POLARITY)
    ) _TECHMAP_REPLACE_ (
        .D(D), .CLK(CLK), .Q(Q)
    );
  `else
    reg state;
    generate
      if (CLK_POLARITY) always @(posedge CLK) state <= D;
      else              always @(negedge CLK) state <= D;
    endgenerate
    assign Q = state;
  `endif
  assign QN = ~Q;
`endif
endmodule

`SILICON_CELL(SILICON_DFFE, _silicon_export_dffe, "SILICON_DFFE") #(
    parameter CLK_POLARITY = 1,
    parameter EN_POLARITY = 1
) (
    input D, input EN, input CLK, output Q, output QN
);
`ifndef SILICON_BLACKBOX
  `ifdef SILICON_EXPORT_MAP
    \$dffe #(
        .WIDTH(1), .CLK_POLARITY(CLK_POLARITY), .EN_POLARITY(EN_POLARITY)
    ) _TECHMAP_REPLACE_ (
        .D(D), .EN(EN), .CLK(CLK), .Q(Q)
    );
  `else
    reg state;
    generate
      if (CLK_POLARITY) always @(posedge CLK) begin
        if (EN === EN_POLARITY) state <= D;
        else if (EN !== !EN_POLARITY) state <= 1'bx;
      end
      else always @(negedge CLK) begin
        if (EN === EN_POLARITY) state <= D;
        else if (EN !== !EN_POLARITY) state <= 1'bx;
      end
    endgenerate
    assign Q = state;
  `endif
  assign QN = ~Q;
`endif
endmodule

`SILICON_CELL(SILICON_DLATCH, _silicon_export_dlatch, "SILICON_DLATCH") #(
    parameter EN_POLARITY = 1
) (
    input D, input EN, output Q, output QN
);
`ifndef SILICON_BLACKBOX
  `ifdef SILICON_EXPORT_MAP
    \$dlatch #(
        .WIDTH(1), .EN_POLARITY(EN_POLARITY)
    ) _TECHMAP_REPLACE_ (
        .D(D), .EN(EN), .Q(Q)
    );
  `else
    reg state;
    always @* begin
      if (EN === EN_POLARITY) state <= D;
      else if (EN !== !EN_POLARITY) state <= 1'bx;
    end
    assign Q = state;
  `endif
  assign QN = ~Q;
`endif
endmodule

`SILICON_CELL(SILICON_DFFSR, _silicon_export_dffsr, "SILICON_DFFSR") #(
    parameter CLK_POLARITY = 1,
    parameter SET_POLARITY = 1,
    parameter CLR_POLARITY = 1
) (
    input D, input CLK, input SET, input CLR, output Q, output QN
);
`ifndef SILICON_BLACKBOX
  `ifdef SILICON_EXPORT_MAP
    \$dffsr #(
        .WIDTH(1),
        .CLK_POLARITY(CLK_POLARITY),
        .SET_POLARITY(SET_POLARITY),
        .CLR_POLARITY(CLR_POLARITY)
    ) _TECHMAP_REPLACE_ (
        .D(D), .CLK(CLK), .SET(SET), .CLR(CLR), .Q(Q)
    );
  `else
    reg state;
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
      if (clr_active && set_inactive) state <= 1'b0;
      else if (clr_inactive && set_active) state <= 1'b1;
      else if (!clr_inactive || !set_inactive) state <= 1'bx;
      else if (selected_edge) state <= D;
      last_clk = CLK;
    end
    assign Q = state;
  `endif
  assign QN = ~Q;
`endif
endmodule

`SILICON_CELL(SILICON_DFFSRE, _silicon_export_dffsre, "SILICON_DFFSRE") #(
    parameter CLK_POLARITY = 1,
    parameter EN_POLARITY = 1,
    parameter SET_POLARITY = 1,
    parameter CLR_POLARITY = 1
) (
    input D, input EN, input CLK, input SET, input CLR, output Q, output QN
);
`ifndef SILICON_BLACKBOX
  `ifdef SILICON_EXPORT_MAP
    \$dffsre #(
        .WIDTH(1),
        .CLK_POLARITY(CLK_POLARITY),
        .EN_POLARITY(EN_POLARITY),
        .SET_POLARITY(SET_POLARITY),
        .CLR_POLARITY(CLR_POLARITY)
    ) _TECHMAP_REPLACE_ (
        .D(D), .EN(EN), .CLK(CLK), .SET(SET), .CLR(CLR), .Q(Q)
    );
  `else
    reg state;
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
      if (clr_active && set_inactive) state <= 1'b0;
      else if (clr_inactive && set_active) state <= 1'b1;
      else if (!clr_inactive || !set_inactive) state <= 1'bx;
      else if (selected_edge && EN === EN_POLARITY) state <= D;
      else if (selected_edge && EN !== !EN_POLARITY) state <= 1'bx;
      last_clk = CLK;
    end
    assign Q = state;
  `endif
  assign QN = ~Q;
`endif
endmodule

`SILICON_CELL(SILICON_JKFF, _silicon_export_jkff, "SILICON_JKFF") #(
    parameter CLK_POLARITY = 1,
    parameter SET_POLARITY = 1,
    parameter CLR_POLARITY = 1
) (
    input J, input K, input CLK, input SET, input CLR, output Q, output QN
);
`ifndef SILICON_BLACKBOX
  `ifdef SILICON_EXPORT_MAP
    wire next_q = (J & ~Q) | (~K & Q);
    \$dffsr #(
        .WIDTH(1),
        .CLK_POLARITY(CLK_POLARITY),
        .SET_POLARITY(SET_POLARITY),
        .CLR_POLARITY(CLR_POLARITY)
    ) _TECHMAP_REPLACE_ (
        .D(next_q), .CLK(CLK), .SET(SET), .CLR(CLR), .Q(Q)
    );
  `else
    reg state;
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
      if (clr_active && set_inactive) state <= 1'b0;
      else if (clr_inactive && set_active) state <= 1'b1;
      else if (!clr_inactive || !set_inactive) state <= 1'bx;
      else if (selected_edge) begin
        case ({J, K})
          2'b00: state <= state;
          2'b01: state <= 1'b0;
          2'b10: state <= 1'b1;
          2'b11: state <= ~state;
          default: state <= 1'bx;
        endcase
      end
      last_clk = CLK;
    end
    assign Q = state;
  `endif
  assign QN = ~Q;
`endif
endmodule

`SILICON_CELL(SILICON_HALF_ADDER, _silicon_export_half_adder,
              "SILICON_HALF_ADDER") (
    input A, input B, output SUM, output COUT
);
`ifndef SILICON_BLACKBOX
  assign SUM = A ^ B;
  assign COUT = A & B;
`endif
endmodule

`SILICON_CELL(SILICON_FULL_ADDER, _silicon_export_full_adder,
              "SILICON_FULL_ADDER") (
    input A, input B, input CIN, output SUM, output COUT
);
`ifndef SILICON_BLACKBOX
  assign SUM = A ^ B ^ CIN;
  assign COUT = (A & B) | (A & CIN) | (B & CIN);
`endif
endmodule

`SILICON_CELL(SILICON_ADDER, _silicon_export_adder, "SILICON_ADDER") #(
    parameter WIDTH = 1,
    parameter A_SIGNED = 0,
    parameter B_SIGNED = 0
) (
    input [WIDTH-1:0] A,
    input [WIDTH-1:0] B,
    output [WIDTH-1:0] SUM,
    output COUT
);
`ifndef SILICON_BLACKBOX
  `ifdef SILICON_EXPORT_MAP
    wire _TECHMAP_FAIL_ = A_SIGNED || B_SIGNED;
    assign {COUT, SUM} = A + B;
  `else
    assign {COUT, SUM} = {1'b0, A} + {1'b0, B};
  `endif
`endif
endmodule

`SILICON_CELL(SILICON_PIPO, _silicon_export_pipo, "SILICON_PIPO") #(
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
`ifndef SILICON_BLACKBOX
  `ifdef SILICON_EXPORT_MAP
    \$adffe #(
        .WIDTH(WIDTH),
        .CLK_POLARITY(CLK_POLARITY),
        .EN_POLARITY(EN_POLARITY),
        .ARST_POLARITY(CLR_POLARITY),
        .ARST_VALUE({WIDTH{1'b0}})
    ) _TECHMAP_REPLACE_ (
        .D(DATA), .CLK(CLK), .EN(EN), .ARST(CLR), .Q(OUT)
    );
  `else
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
  `endif
`endif
endmodule

`SILICON_CELL(SILICON_PISO, _silicon_export_piso, "SILICON_PISO") #(
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
`ifndef SILICON_BLACKBOX
  `ifdef SILICON_EXPORT_MAP
    wire [WIDTH-1:0] state;
    wire [WIDTH-1:0] shifted = {1'b0, state[WIDTH-1:1]};
    wire [WIDTH-1:0] next_state = LOAD == LOAD_POLARITY ? DATA : shifted;
    \$adffe #(
        .WIDTH(WIDTH),
        .CLK_POLARITY(CLK_POLARITY),
        .EN_POLARITY(EN_POLARITY),
        .ARST_POLARITY(CLR_POLARITY),
        .ARST_VALUE({WIDTH{1'b0}})
    ) _TECHMAP_REPLACE_ (
        .D(next_state), .CLK(CLK), .EN(EN), .ARST(CLR), .Q(state)
    );
    assign OUT = state[0];
  `else
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
  `endif
`endif
endmodule

`SILICON_CELL(SILICON_SIPO, _silicon_export_sipo, "SILICON_SIPO") #(
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
`ifndef SILICON_BLACKBOX
  `ifdef SILICON_EXPORT_MAP
    wire [WIDTH-1:0] next_state = {DATA, OUT[WIDTH-1:1]};
    \$adffe #(
        .WIDTH(WIDTH),
        .CLK_POLARITY(CLK_POLARITY),
        .EN_POLARITY(EN_POLARITY),
        .ARST_POLARITY(CLR_POLARITY),
        .ARST_VALUE({WIDTH{1'b0}})
    ) _TECHMAP_REPLACE_ (
        .D(next_state), .CLK(CLK), .EN(EN), .ARST(CLR), .Q(OUT)
    );
  `else
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
  `endif
`endif
endmodule

`SILICON_CELL(SILICON_SISO, _silicon_export_siso, "SILICON_SISO") #(
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
`ifndef SILICON_BLACKBOX
  `ifdef SILICON_EXPORT_MAP
    wire [WIDTH-1:0] state;
    wire [WIDTH-1:0] next_state = {DATA, state[WIDTH-1:1]};
    \$adffe #(
        .WIDTH(WIDTH),
        .CLK_POLARITY(CLK_POLARITY),
        .EN_POLARITY(EN_POLARITY),
        .ARST_POLARITY(CLR_POLARITY),
        .ARST_VALUE({WIDTH{1'b0}})
    ) _TECHMAP_REPLACE_ (
        .D(next_state), .CLK(CLK), .EN(EN), .ARST(CLR), .Q(state)
    );
    assign OUT = state[0];
  `else
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
  `endif
`endif
endmodule

`undef SILICON_CELL
