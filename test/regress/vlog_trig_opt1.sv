module vlog_trig_opt1;
  logic        clk = 0;
  logic        rst_n = 1;
  logic        en = 0;
  logic [7:0]  a = 8'h11;
  logic [7:0]  b = 8'h22;
  logic [7:0]  bus = 8'h00;
  logic [7:0]  mem [0:3];
  logic [1:0]  idx = 2'd0;
  bit   [7:0]  u0, u1;   // No initializer: no t=0 init event

  // 1. always_comb: t=0 run even with no input events, whole signals, dup reads
  logic [7:0]  comb_u;
  bit          comb_u_ran;
  always_comb begin
    comb_u = (u0 + u1) ^ u0 ^ 8'h5A;
    comb_u_ran = 1'b1;
  end

  logic [7:0]  comb_y;
  always_comb begin
    comb_y = (a + b) ^ a;
  end

  // 2. always_comb with partial packed slice (must NOT wake on unrelated bits)
  logic [3:0]  slice_y;
  bit          slice_ran;
  always_comb begin
    slice_y = bus[3:0] + 4'd1;
    slice_ran = 1'b1;
  end

  // 3. always_comb with dynamic-indexed unpacked array
  logic [7:0]  dyn_y;
  always_comb begin
    dyn_y = mem[idx];
  end

  // 4. always_latch: t=0 run + transparent/hold behavior
  logic [7:0]  lat_q;
  always_latch begin
    if (en)
      lat_q = a;
  end

  // 5. always @*: must NOT run at t=0 when inputs have no t=0 event
  logic [7:0]  star_u = 8'hAA;
  bit          star_u_ran;
  always @* begin
    star_u = u0 ^ u1 ^ 8'h5A;
    star_u_ran = 1'b1;
  end

  // 6. always @* with self-written module-level temp (must not self-trigger)
  bit   [7:0]  self_tmp, self_y;
  int          self_runs;
  always @* begin
    self_tmp = u0 + 8'd1;
    self_y = self_tmp + u1;
    self_runs = self_runs + 1;
  end

  // 7. always_ff @(posedge clk or negedge rst_n): keep-armed edge trigger
  logic [7:0]  ff_q = 8'h55;
  int          ff_runs;
  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n)
      ff_q <= 8'h00;
    else
      ff_q <= ff_q + 8'd1;
    ff_runs <= ff_runs + 1;
  end

  // 8. always with iff guard
  int          iff_cnt;
  always @(posedge clk iff en) begin
    iff_cnt = iff_cnt + 1;
  end

  // 9. Multi-wait always block
  int          phase;
  always begin
    @(posedge clk);
    phase = 1;
    @(negedge clk);
    phase = 2;
  end

  initial begin
    mem[0] = 8'h10;
    mem[1] = 8'h20;
    mem[2] = 8'h30;
    mem[3] = 8'h40;
    #1;

    // Check t=0 semantics: always_comb runs at t=0, always @* waits at top
    if (comb_u !== 8'h5A || comb_u_ran !== 1'b1)
      $fatal(1, "always_comb t=0 failed: comb_u=%h ran=%b",
             comb_u, comb_u_ran);
    if (star_u !== 8'hAA || star_u_ran !== 1'b0)
      $fatal(1, "always @* ran at t=0: star_u=%h ran=%b",
             star_u, star_u_ran);
    if (self_runs != 0)
      $fatal(1, "self-write always @* ran at t=0: runs=%0d", self_runs);
    if (comb_y !== ((8'h11 + 8'h22) ^ 8'h11))
      $fatal(1, "comb_y init failed: %h", comb_y);
    if (dyn_y !== 8'h10)
      $fatal(1, "dyn_y init failed: %h", dyn_y);

    // Check partial packed slice: changing bus[7:4] must not wake slice_ran
    slice_ran = 1'b0;
    #1;
    bus[7:4] = 4'hF;
    #1;
    if (slice_ran !== 1'b0)
      $fatal(1, "partial packed slice spuriously woke on bus[7:4]");
    bus[3:0] = 4'h5;
    #1;
    if (slice_y !== 4'h6 || slice_ran !== 1'b1)
      $fatal(1, "partial packed slice missed update: y=%h ran=%b",
             slice_y, slice_ran);

    // Check dynamic index and array element updates
    idx = 2'd2;
    #1;
    if (dyn_y !== 8'h30)
      $fatal(1, "dyn_y idx change failed: %h", dyn_y);
    mem[2] = 8'h99;
    #1;
    if (dyn_y !== 8'h99)
      $fatal(1, "dyn_y elem change failed: %h", dyn_y);

    // Check always_comb, always @*, self-write always @*, and always_latch
    comb_u_ran = 1'b0;
    star_u_ran = 1'b0;
    u0 = 8'h40;
    u1 = 8'h22;
    en = 1'b1;
    a = 8'h40;
    #1;
    if (comb_u !== ((8'h40 + 8'h22) ^ 8'h40 ^ 8'h5A) || comb_u_ran !== 1'b1)
      $fatal(1, "comb_u update failed: %h ran=%b", comb_u, comb_u_ran);
    if (comb_y !== ((8'h40 + 8'h22) ^ 8'h40))
      $fatal(1, "comb_y update failed: %h", comb_y);
    if (star_u !== (8'h40 ^ 8'h22 ^ 8'h5A) || star_u_ran !== 1'b1)
      $fatal(1, "star_u update failed: y=%h ran=%b", star_u, star_u_ran);
    if (self_y !== (8'h41 + 8'h22) || self_runs != 1)
      $fatal(1, "self_y update failed: y=%h runs=%0d", self_y, self_runs);
    if (lat_q !== 8'h40)
      $fatal(1, "lat_q transparent failed: %h", lat_q);

    en = 1'b0;
    #1;
    a = 8'h77;
    #1;
    if (lat_q !== 8'h40)
      $fatal(1, "lat_q hold failed: %h", lat_q);

    // Check always_ff async reset, posedge, negedge, iff, and multi-wait
    rst_n = 1'b0;
    #1;
    if (ff_q !== 8'h00 || ff_runs !== 1)
      $fatal(1, "ff async reset failed: q=%h runs=%0d", ff_q, ff_runs);

    rst_n = 1'b1;   // posedge rst_n must NOT trigger always_ff
    #1;
    if (ff_runs !== 1)
      $fatal(1, "ff woke on posedge rst_n: runs=%0d", ff_runs);

    clk = 1'b1;
    #1;
    if (ff_q !== 8'h01 || ff_runs !== 2 || phase !== 1 || iff_cnt !== 0)
      $fatal(1, "clk posedge 1 failed: q=%h runs=%0d phase=%0d iff=%0d",
             ff_q, ff_runs, phase, iff_cnt);

    clk = 1'b0;     // negedge clk must NOT trigger always_ff, DOES trigger multi-wait
    #1;
    if (ff_q !== 8'h01 || ff_runs !== 2 || phase !== 2)
      $fatal(1, "clk negedge 1 failed: q=%h runs=%0d phase=%0d",
             ff_q, ff_runs, phase);

    en = 1'b1;
    clk = 1'b1;
    #1;
    if (ff_q !== 8'h02 || ff_runs !== 3 || phase !== 1 || iff_cnt !== 1)
      $fatal(1, "clk posedge 2 failed: q=%h runs=%0d phase=%0d iff=%0d",
             ff_q, ff_runs, phase, iff_cnt);

    if (self_runs == 999)
      self_runs = 0;

    $display("PASSED");
    $finish;
  end
endmodule
