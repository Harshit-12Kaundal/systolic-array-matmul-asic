// Wishbone-lite (zero-wait-state) 32-bit register interface wrapping
// systolic_mm_top, so a CPU/software driver can control it via simple
// memory-mapped reads and writes instead of raw wide flat buses.
//
// Register map (word-addressed, byte addresses shown, 32-bit accesses only):
//   A_BASE   .. A_BASE+4*(NUM_A_WORDS-1)   : matrix A words (write)
//   B_BASE   .. B_BASE+4*(NUM_B_WORDS-1)   : matrix B words (write)
//   CTRL_ADDR                              : bit0 = start (write 1 to launch)
//   STATUS_ADDR                            : bit0 = done  (read; clear-on-read)
//   C_BASE   .. C_BASE+4*(NUM_C_WORDS-1)   : result C words (read-only)
//
// For the default N=4/DW=8/AW=32 config:
//   A_BASE=0x00 (4 words), B_BASE=0x10 (4 words),
//   CTRL_ADDR=0x20, STATUS_ADDR=0x24, C_BASE=0x28 (16 words)
module systolic_mm_wb #(
    parameter N  = 4,
    parameter DW = 8,
    parameter AW = 32
)(
    input                   clk,
    input                   rst_n,

    // Wishbone-lite bus (single-cycle, zero-wait-state slave)
    input      [31:0]       wb_addr,   // byte address
    input      [31:0]       wb_wdata,
    input                   wb_we,
    input                   wb_valid,  // request strobe
    output     [31:0]       wb_rdata,  // combinational, valid same cycle as wb_ack
    output                  wb_ack     // combinational, same-cycle ack
);

    localparam A_BITS = N * N * DW;
    localparam B_BITS = N * N * DW;
    localparam C_BITS = N * N * AW;

    localparam NUM_A_WORDS = (A_BITS + 31) / 32;
    localparam NUM_B_WORDS = (B_BITS + 31) / 32;
    localparam NUM_C_WORDS = (C_BITS + 31) / 32;

    localparam A_BASE      = 32'h0000_0000;
    localparam B_BASE      = A_BASE + 4 * NUM_A_WORDS;
    localparam CTRL_ADDR   = B_BASE + 4 * NUM_B_WORDS;
    localparam STATUS_ADDR = CTRL_ADDR + 4;
    localparam C_BASE      = STATUS_ADDR + 4;

    reg [31:0] a_reg [0:NUM_A_WORDS-1];
    reg [31:0] b_reg [0:NUM_B_WORDS-1];

    wire [NUM_A_WORDS*32-1:0] a_flat_padded;
    wire [NUM_B_WORDS*32-1:0] b_flat_padded;

    genvar gk;
    generate
        for (gk = 0; gk < NUM_A_WORDS; gk = gk + 1) begin : flatten_a
            assign a_flat_padded[gk*32 +: 32] = a_reg[gk];
        end
        for (gk = 0; gk < NUM_B_WORDS; gk = gk + 1) begin : flatten_b
            assign b_flat_padded[gk*32 +: 32] = b_reg[gk];
        end
    endgenerate

    wire [A_BITS-1:0] a_flat = a_flat_padded[A_BITS-1:0];
    wire [B_BITS-1:0] b_flat = b_flat_padded[B_BITS-1:0];

    wire                core_done;
    wire [C_BITS-1:0]   c_flat;
    reg                 start_pulse;

    systolic_mm_top #(.N(N), .DW(DW), .AW(AW)) u_core (
        .clk    (clk),
        .rst_n  (rst_n),
        .start  (start_pulse),
        .a_flat (a_flat),
        .b_flat (b_flat),
        .done   (core_done),
        .c_flat (c_flat)
    );

    wire [NUM_C_WORDS*32-1:0] c_flat_padded = {{(NUM_C_WORDS*32-C_BITS){1'b0}}, c_flat};

    wire is_a_word   = wb_valid && (wb_addr >= A_BASE)   && (wb_addr < A_BASE + 4*NUM_A_WORDS);
    wire is_b_word   = wb_valid && (wb_addr >= B_BASE)   && (wb_addr < B_BASE + 4*NUM_B_WORDS);
    wire is_ctrl     = wb_valid && (wb_addr == CTRL_ADDR);
    wire is_status   = wb_valid && (wb_addr == STATUS_ADDR);
    wire is_c_word   = wb_valid && (wb_addr >= C_BASE)   && (wb_addr < C_BASE + 4*NUM_C_WORDS);

    wire [31:0] a_word_idx = (wb_addr - A_BASE) >> 2;
    wire [31:0] b_word_idx = (wb_addr - B_BASE) >> 2;
    wire [31:0] c_word_idx = (wb_addr - C_BASE) >> 2;

    assign wb_ack = wb_valid; // zero-wait-state: every request acks the same cycle

    reg done_sticky;

    reg [31:0] rdata_mux;
    always @(*) begin
        rdata_mux = 32'h0;
        if (is_a_word && !wb_we)
            rdata_mux = a_reg[a_word_idx];
        else if (is_b_word && !wb_we)
            rdata_mux = b_reg[b_word_idx];
        else if (is_status && !wb_we)
            rdata_mux = {31'h0, done_sticky};
        else if (is_c_word && !wb_we)
            rdata_mux = c_flat_padded[c_word_idx*32 +: 32];
    end
    assign wb_rdata = rdata_mux;

    integer wi;
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            done_sticky <= 1'b0;
            start_pulse <= 1'b0;
            for (wi = 0; wi < NUM_A_WORDS; wi = wi + 1) a_reg[wi] <= 32'h0;
            for (wi = 0; wi < NUM_B_WORDS; wi = wi + 1) b_reg[wi] <= 32'h0;
        end else begin
            start_pulse <= 1'b0;

            if (is_a_word && wb_we)
                a_reg[a_word_idx] <= wb_wdata;
            if (is_b_word && wb_we)
                b_reg[b_word_idx] <= wb_wdata;
            if (is_ctrl && wb_we && wb_wdata[0]) begin
                start_pulse <= 1'b1;
                done_sticky <= 1'b0;
            end

            if (core_done)
                done_sticky <= 1'b1;
            else if (is_status && !wb_we)
                done_sticky <= 1'b0;
        end
    end

endmodule
