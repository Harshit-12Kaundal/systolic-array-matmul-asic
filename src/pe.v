// Single Processing Element (PE) for an output-stationary systolic array.
// Passes a_in -> a_out (rightward) and b_in -> b_out (downward) one cycle
// later, and accumulates a_in*b_in into acc_out every cycle it is not cleared.
module pe #(
    parameter DW = 8,   // operand data width (signed INT8 by default)
    parameter AW = 32   // accumulator width
)(
    input                        clk,
    input                        rst_n,
    input                        clear_acc,
    input  signed [DW-1:0]       a_in,
    input  signed [DW-1:0]       b_in,
    output reg signed [DW-1:0]   a_out,
    output reg signed [DW-1:0]   b_out,
    output reg signed [AW-1:0]   acc_out
);

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            a_out   <= {DW{1'b0}};
            b_out   <= {DW{1'b0}};
            acc_out <= {AW{1'b0}};
        end else begin
            a_out <= a_in;
            b_out <= b_in;
            if (clear_acc)
                acc_out <= {AW{1'b0}};
            else
                acc_out <= acc_out + (a_in * b_in);
        end
    end

endmodule
