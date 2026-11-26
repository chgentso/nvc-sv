`timescale 1ns/1ns

// Test single-word direct slice loads and stores on wide (>64-bit) packed
// island vectors: 64-bit aligned elements, 32-bit half-word elements,
// non-64-aligned top-word slices (70-bit vector), and unpacked array of wide
// packed vectors, across both combinational and FF (NBA) domains.

module wide_slice_dut (
   input  logic        clk,
   input  logic        rst_n,
   input  logic        en,
   input  logic [1:0]  sel,
   input  logic [31:0] din,
   output logic [63:0] cnt_out,
   output logic [31:0] cfg_out,
   output logic [5:0]  top70_out,
   output logic [63:0] arr_out
);
   logic [3:0][63:0] cnt_q, cnt_inc;
   logic [3:0][31:0] cfg_q;
   logic [69:0]      wide70_q;
   logic [127:0]     arr_q [0:1];

   genvar i;
   for (i = 0; i < 4; i++) begin : g_inc
      assign cnt_inc[i] = cnt_q[i] + 64'd1;
   end

   for (i = 0; i < 4; i++) begin : g_cnt_ff
      if (i == 3) begin : g_tie
         assign cnt_q[i] = 64'h0123_4567_89ab_cdef;
         assign cfg_q[i] = 32'hdead_beef;
      end else begin : g_reg
         always_ff @(posedge clk or negedge rst_n) begin
            if (!rst_n) begin
               cnt_q[i] <= 64'd0;
               cfg_q[i] <= 32'd0;
            end else if (en) begin
               cnt_q[i] <= cnt_inc[i] + {32'd0, din};
               cfg_q[i] <= din ^ (32'h1111_0000 + i);
            end
         end
      end
   end

   always_ff @(posedge clk or negedge rst_n) begin
      if (!rst_n) begin
         wide70_q <= 70'd0;
         arr_q[0] <= 128'd0;
         arr_q[1] <= 128'd0;
      end else if (en) begin
         wide70_q[69:64] <= din[5:0];
         wide70_q[63:0]  <= {din, ~din};
         arr_q[0][127:64] <= cnt_inc[0];
         arr_q[0][63:0]   <= cnt_inc[1];
         arr_q[1][127:64] <= {32'h55aa_0000, cfg_q[0]};
         arr_q[1][63:0]   <= {32'haa55_0000, cfg_q[1]};
      end
   end

   assign cnt_out   = cnt_q[sel];
   assign cfg_out   = cfg_q[sel];
   assign top70_out = wide70_q[69:64];
   assign arr_out   = sel[0] ? arr_q[1][127:64] : arr_q[0][127:64];
endmodule

module vlog_island_wide_slice1;
   logic        clk = 1'b0;
   logic        rst_n = 1'b0;
   logic        en = 1'b0;
   logic [1:0]  sel = 2'd0;
   logic [31:0] din = 32'd0;
   logic [63:0] cnt_out, arr_out;
   logic [31:0] cfg_out;
   logic [5:0]  top70_out;

   wide_slice_dut dut (
      .clk       (clk),
      .rst_n     (rst_n),
      .en        (en),
      .sel       (sel),
      .din       (din),
      .cnt_out   (cnt_out),
      .cfg_out   (cfg_out),
      .top70_out (top70_out),
      .arr_out   (arr_out)
   );

   always #5 clk = ~clk;

   initial begin
      #12 rst_n = 1'b1;
      en = 1'b1;
      din = 32'h0000_002a;
      @(negedge clk); #1; // after posedge 15ns: cnt[0..2]=0x2b, cfg[0]=0x1111002a, top70=6'h2a, arr[0][127:64]=1
      sel = 2'd0; #1;
      if (cnt_out !== 64'h2b || cfg_out !== 32'h1111_002a || top70_out !== 6'h2a
          || arr_out !== 64'd1) begin
         $display("FAIL c1 s0: cnt=%h cfg=%h top70=%h arr=%h",
                  cnt_out, cfg_out, top70_out, arr_out);
         $finish(1);
      end
      sel = 2'd3; #1;
      if (cnt_out !== 64'h0123_4567_89ab_cdef || cfg_out !== 32'hdead_beef) begin
         $display("FAIL c1 s3: cnt=%h cfg=%h", cnt_out, cfg_out);
         $finish(1);
      end

      din = 32'h0000_0015;
      @(negedge clk); #1; // after posedge 25ns: cnt[0..2]=0x41, arr[0][127:64]=0x2c, arr[1][127:64]={55aa0000,1111002a}
      sel = 2'd0; #1;
      if (cnt_out !== 64'h41 || cfg_out !== 32'h1111_0015 || top70_out !== 6'h15
          || arr_out !== 64'h2c) begin
         $display("FAIL c2 s0: cnt=%h cfg=%h top70=%h arr=%h",
                  cnt_out, cfg_out, top70_out, arr_out);
         $finish(1);
      end
      sel = 2'd1; #1;
      if (cfg_out !== (32'h0000_0015 ^ 32'h1111_0001)
          || arr_out !== 64'h55aa_0000_1111_002a) begin
         $display("FAIL c2 s1: cfg=%h arr=%h", cfg_out, arr_out);
         $finish(1);
      end

      $display("PASSED");
      $finish;
   end
endmodule
