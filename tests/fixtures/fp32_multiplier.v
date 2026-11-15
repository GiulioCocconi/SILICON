
module fp32_multiplier (
    input  wire [31:0] a,
    input  wire [31:0] b,
    output reg  [31:0] result
);

    wire sign = a[31] ^ b[31];

    wire [7:0] exp_a = a[30:23];
    wire [7:0] exp_b = b[30:23];

    wire [22:0] frac_a = a[22:0];
    wire [22:0] frac_b = b[22:0];

    wire nan_a = (&exp_a) && (|frac_a);
    wire nan_b = (&exp_b) && (|frac_b);

    wire inf_a = (&exp_a) && !(|frac_a);
    wire inf_b = (&exp_b) && !(|frac_b);

    wire zero_a = (exp_a == 0) && (frac_a == 0);
    wire zero_b = (exp_b == 0) && (frac_b == 0);

    // Subnormals have no implicit leading 1.
    wire [23:0] sig_a = {exp_a != 0, frac_a};
    wire [23:0] sig_b = {exp_b != 0, frac_b};

    wire [47:0] product = sig_a * sig_b;

    // Effective biased exponents.
    wire [8:0] ea = (exp_a == 0) ? 9'd1 : {1'b0, exp_a};
    wire [8:0] eb = (exp_b == 0) ? 9'd1 : {1'b0, exp_b};

    integer i;
    integer leading;
    integer exponent;
    integer shift_amount;

    reg [48:0] rounded;

    // Shift and round to nearest, ties to even.
    function [48:0] round_shift;
        input [47:0] value;
        input integer shift;
        reg [48:0] quotient;
        reg guard_bit;
        reg sticky_bit;
        reg [47:0] mask;
        begin
            if (shift <= 0) begin
                round_shift = {1'b0, value} << (-shift);
            end
            else if (shift > 48) begin
                round_shift = 49'd0;
            end
            else begin
                quotient = {1'b0, value} >> shift;

                guard_bit = value[shift - 1];
                mask = (48'd1 << (shift - 1)) - 48'd1;
                sticky_bit = |(value & mask);

                round_shift = quotient
                    + (guard_bit && (sticky_bit || quotient[0]));
            end
        end
    endfunction

    always @* begin
        result = 32'd0;
        rounded = 49'd0;
        leading = 0;
        exponent = 0;
        shift_amount = 0;

        if (nan_a || nan_b ||
            ((inf_a || inf_b) && (zero_a || zero_b))) begin
            // Canonical quiet NaN.
            result = 32'h7fc00000;
        end
        else if (inf_a || inf_b) begin
            result = {sign, 8'hff, 23'd0};
        end
        else if (zero_a || zero_b) begin
            result = {sign, 31'd0};
        end
        else begin
            // Find the most significant set bit.
            for (i = 0; i < 48; i = i + 1)
                if (product[i])
                    leading = i;

            // Biased exponent of the normalized product.
            exponent = ea + eb + leading - 173;

            if (exponent >= 1) begin
                // Normal result: retain 24 significant bits.
                shift_amount = leading - 23;
                rounded = round_shift(product, shift_amount);

                // Rounding can produce a carry.
                if (rounded[24]) begin
                    rounded = rounded >> 1;
                    exponent = exponent + 1;
                end

                if (exponent >= 255)
                    result = {sign, 8'hff, 23'd0};
                else
                    result = {
                        sign,
                        exponent[7:0],
                        rounded[22:0]
                    };
            end
            else begin
                // Subnormal result: exponent is fixed at -126.
                shift_amount = 151 - (ea + eb);
                rounded = round_shift(product, shift_amount);

                if (rounded >= 49'h800000)
                    // Rounded up to the smallest normal.
                    result = {sign, 8'h01, 23'd0};
                else
                    result = {sign, 8'h00, rounded[22:0]};
            end
        end
    end

endmodule
