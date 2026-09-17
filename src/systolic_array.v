// N x N output-stationary systolic array core.
// a_in_flat: N operands entering from the left, one per row, concatenated
//            {a_in[N-1], ..., a_in[1], a_in[0]} each DW bits.
// b_in_flat: N operands entering from the top, one per column, same packing.
// acc_out_flat: row-major flattened accumulators; PE(i,j) result lives at
//               bits [(i*N+j)*AW +: AW].
module systolic_array #(
    parameter N  = 4,
    parameter DW = 8,
    parameter AW = 32
)(
    input                     clk,
    input                     rst_n,
    input                     clear_acc,
    input      [N*DW-1:0]     a_in_flat,
    input      [N*DW-1:0]     b_in_flat,
    output     [N*N*AW-1:0]   acc_out_flat
);

    wire signed [DW-1:0] a_grid [0:N-1][0:N];
    wire signed [DW-1:0] b_grid [0:N][0:N-1];
    wire signed [AW-1:0] acc_grid [0:N-1][0:N-1];

    genvar gi, gj;
    generate
        for (gi = 0; gi < N; gi = gi + 1) begin : gen_a_edge
            assign a_grid[gi][0] = $signed(a_in_flat[gi*DW +: DW]);
        end
        for (gj = 0; gj < N; gj = gj + 1) begin : gen_b_edge
            assign b_grid[0][gj] = $signed(b_in_flat[gj*DW +: DW]);
        end

        for (gi = 0; gi < N; gi = gi + 1) begin : row
            for (gj = 0; gj < N; gj = gj + 1) begin : col
                pe #(.DW(DW), .AW(AW)) u_pe (
                    .clk       (clk),
                    .rst_n     (rst_n),
                    .clear_acc (clear_acc),
                    .a_in      (a_grid[gi][gj]),
                    .b_in      (b_grid[gi][gj]),
                    .a_out     (a_grid[gi][gj+1]),
                    .b_out     (b_grid[gi+1][gj]),
                    .acc_out   (acc_grid[gi][gj])
                );
                assign acc_out_flat[(gi*N+gj)*AW +: AW] = acc_grid[gi][gj];
            end
        end
    endgenerate

endmodule
