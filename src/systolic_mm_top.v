// Top-level INT8 x INT8 -> INT32 matrix multiplier wrapper.
// Loads full NxN A and B matrices on 'start', streams them into the
// systolic_array core with the required diagonal skew, and latches the
// full NxN result once the pipeline has drained.
module systolic_mm_top #(
    parameter N  = 4,
    parameter DW = 8,
    parameter AW = 32
)(
    input                        clk,
    input                        rst_n,
    input                        start,               // pulse for 1 cycle to begin a multiply
    input      [N*N*DW-1:0]      a_flat,              // row-major A[i][j] at (i*N+j)*DW
    input      [N*N*DW-1:0]      b_flat,              // row-major B[i][j] at (i*N+j)*DW
    output reg                   done,                // pulses for 1 cycle when c_flat is valid
    output reg [N*N*AW-1:0]      c_flat               // row-major C[i][j] at (i*N+j)*AW
);

    localparam IDLE   = 2'd0,
               CLEAR   = 2'd1,
               RUN     = 2'd2,
               FINISH  = 2'd3;

    // Margin beyond the theoretical (3N-2) drain latency of an N x N
    // output-stationary array; simplicity/robustness favored over
    // cycle-count optimality for this wrapper.
    localparam RUN_CYCLES = 3 * N;

    localparam CNT_W = $clog2(RUN_CYCLES + 1);
    localparam PTR_W = (N <= 1) ? 1 : $clog2(N);

    reg [1:0]       state;
    reg [CNT_W-1:0] cnt;

    reg signed [DW-1:0] a_mem [0:N-1][0:N-1]; // a_mem[i][k] = A[i][k]
    reg signed [DW-1:0] b_mem [0:N-1][0:N-1]; // b_mem[k][j] = B[k][j]

    reg [PTR_W-1:0] row_ptr [0:N-1];
    reg [PTR_W-1:0] col_ptr [0:N-1];

    wire clear_acc = (state == CLEAR);

    wire [N*DW-1:0]   a_feed;
    wire [N*DW-1:0]   b_feed;
    wire [N*N*AW-1:0] acc_out_flat;

    genvar gi, gj;
    generate
        for (gi = 0; gi < N; gi = gi + 1) begin : unpack_a
            assign a_feed[gi*DW +: DW] =
                (state == RUN && cnt >= gi && cnt < gi + N) ?
                a_mem[gi][row_ptr[gi]] : {DW{1'b0}};
        end
        for (gj = 0; gj < N; gj = gj + 1) begin : unpack_b
            assign b_feed[gj*DW +: DW] =
                (state == RUN && cnt >= gj && cnt < gj + N) ?
                b_mem[col_ptr[gj]][gj] : {DW{1'b0}};
        end
    endgenerate

    systolic_array #(.N(N), .DW(DW), .AW(AW)) u_core (
        .clk          (clk),
        .rst_n        (rst_n),
        .clear_acc    (clear_acc),
        .a_in_flat    (a_feed),
        .b_in_flat    (b_feed),
        .acc_out_flat (acc_out_flat)
    );

    integer li, lj;

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state  <= IDLE;
            cnt    <= {CNT_W{1'b0}};
            done   <= 1'b0;
            c_flat <= {(N*N*AW){1'b0}};
            for (li = 0; li < N; li = li + 1) begin
                row_ptr[li] <= {PTR_W{1'b0}};
                col_ptr[li] <= {PTR_W{1'b0}};
            end
        end else begin
            done <= 1'b0;
            case (state)
                IDLE: begin
                    if (start) begin
                        for (li = 0; li < N; li = li + 1)
                            for (lj = 0; lj < N; lj = lj + 1) begin
                                a_mem[li][lj] <= $signed(a_flat[(li*N+lj)*DW +: DW]);
                                b_mem[li][lj] <= $signed(b_flat[(li*N+lj)*DW +: DW]);
                            end
                        for (li = 0; li < N; li = li + 1) begin
                            row_ptr[li] <= {PTR_W{1'b0}};
                            col_ptr[li] <= {PTR_W{1'b0}};
                        end
                        cnt   <= {CNT_W{1'b0}};
                        state <= CLEAR;
                    end
                end

                CLEAR: begin
                    cnt   <= {CNT_W{1'b0}};
                    state <= RUN;
                end

                RUN: begin
                    for (li = 0; li < N; li = li + 1) begin
                        if (cnt >= li && cnt < li + N)
                            row_ptr[li] <= row_ptr[li] + 1'b1;
                        if (cnt >= li && cnt < li + N)
                            col_ptr[li] <= col_ptr[li] + 1'b1;
                    end
                    if (cnt == RUN_CYCLES[CNT_W-1:0] - 1'b1)
                        state <= FINISH;
                    cnt <= cnt + 1'b1;
                end

                FINISH: begin
                    c_flat <= acc_out_flat;
                    done   <= 1'b1;
                    state  <= IDLE;
                end
            endcase
        end
    end

endmodule
