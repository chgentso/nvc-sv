// Expected values: IEEE 1800-2017 9.2.2.2 (always_comb with unpacked struct),
// 10.3.2 (continuous assignment from unpacked struct members and whole structs).
// Unpacked struct and wide-signal combinational processes must stay on watched
// polling and track both whole-struct assignments and in-place field writes.
package svpollstruct1_pkg;
  typedef struct {
    logic [1:0] en;
    logic pp_en;
    logic od_en;
    logic pu_en;
  } drv_state_t;

  typedef struct {
    drv_state_t drv;
    logic [31:0] tag;
  } nested_drv_t;

  function automatic drv_state_t open_drain();
    drv_state_t d;
    d.en = 2'b11;
    d.pp_en = 1'b0;
    d.od_en = 1'b1;
    d.pu_en = 1'b1;
    return d;
  endfunction

  function automatic drv_state_t push_pull();
    drv_state_t d;
    d.en = 2'b11;
    d.pp_en = 1'b1;
    d.od_en = 1'b0;
    d.pu_en = 1'b0;
    return d;
  endfunction

  function automatic drv_state_t disconnect();
    drv_state_t d;
    d.en = 2'b00;
    d.pp_en = 1'b0;
    d.od_en = 1'b0;
    d.pu_en = 1'b0;
    return d;
  endfunction
endpackage

module svpollstruct1;
  import svpollstruct1_pkg::*;

  logic clk = 0;
  logic side_clk = 0;
  logic rst_ni = 1;
  logic unit_starting = 0;
  logic [1:0] mode = 2'd0;
  logic [1:0] sda_d = 2'b00;
  logic [63:0] wide_ctl = 64'h0;

  drv_state_t sda_drv_q, sda_drv_d, sda_drv_mod;
  nested_drv_t nest_q, nest_d;
  logic [1:0] bus_sda_en;
  logic sda_pu_en;
  drv_state_t whole_out;
  logic [31:0] nest_tag_out;

  // Mirrors always#902: reads unpacked struct sda_drv_q + wide signal + control
  always_comb begin
    sda_drv_d = sda_drv_q;
    if (unit_starting) begin
      case (mode)
        2'd0: sda_drv_d = open_drain();
        2'd1: sda_drv_d = push_pull();
        default: sda_drv_d = disconnect();
      endcase
    end else if (wide_ctl[60]) begin
      sda_drv_d = push_pull();
    end
  end

  // Mirrors always#1078: copies unpacked struct then mutates .en in place
  always_comb begin
    sda_drv_mod = sda_drv_d;
    sda_drv_mod.en = sda_drv_d.en & ~(sda_drv_d.od_en ? sda_d : 2'b00);
  end

  // Nested unpacked struct always_comb
  always_comb begin
    nest_d = nest_q;
    nest_d.drv = sda_drv_mod;
    nest_d.tag = wide_ctl[31:0];
  end

  always_ff @(posedge clk or negedge rst_ni) begin
    if (!rst_ni) begin
      sda_drv_q <= disconnect();
      nest_q <= '{drv: disconnect(), tag: 32'h0};
    end else begin
      sda_drv_q <= sda_drv_mod;
      nest_q <= nest_d;
    end
  end

  // Mirrors assign#1114 and whole-struct assign
  assign bus_sda_en = sda_drv_q.en;
  assign sda_pu_en = sda_drv_q.pu_en;
  assign whole_out = sda_drv_q;
  assign nest_tag_out = nest_q.tag;

  // Unrelated toggling clock
  always #5 side_clk = ~side_clk;

  int errs = 0;

  task automatic check(string tag, logic [1:0] een, logic epu, logic eod,
                       logic epp, logic [31:0] etag);
    if (bus_sda_en !== een || sda_pu_en !== epu || whole_out.od_en !== eod
        || whole_out.pp_en !== epp || nest_tag_out !== etag) begin
      $error("%s: en=%b(%b) pu=%b(%b) od=%b(%b) pp=%b(%b) tag=%h(%h)",
             tag, bus_sda_en, een, sda_pu_en, epu,
             whole_out.od_en, eod, whole_out.pp_en, epp,
             nest_tag_out, etag);
      errs++;
    end
  endtask

  initial begin
    #1; rst_ni = 1'b0;
    #1; check("reset", 2'b00, 1'b0, 1'b0, 1'b0, 32'h0);

    rst_ni = 1'b1;
    unit_starting = 1'b1;
    mode = 2'd0;
    sda_d = 2'b01;
    wide_ctl = 64'h0000_0000_DEAD_BEEF;
    #1; clk = 1'b1; #1; clk = 1'b0;
    check("open_drain_masked", 2'b10, 1'b1, 1'b1, 1'b0, 32'hDEAD_BEEF);

    // Toggle sda_d in place without changing unit_starting or mode
    unit_starting = 1'b0;
    sda_d = 2'b10;
    #1; clk = 1'b1; #1; clk = 1'b0;
    check("open_drain_sda_d_toggle", 2'b00, 1'b1, 1'b1, 1'b0, 32'hDEAD_BEEF);

    // Re-enter open_drain with sda_d=2'b10
    unit_starting = 1'b1;
    mode = 2'd0;
    #1; clk = 1'b1; #1; clk = 1'b0;
    check("open_drain_remasked", 2'b01, 1'b1, 1'b1, 1'b0, 32'hDEAD_BEEF);

    // Trigger via upper bit of 64-bit signal wide_ctl
    unit_starting = 1'b0;
    wide_ctl = 64'h1000_0000_CAFE_BABE;
    #1; clk = 1'b1; #1; clk = 1'b0;
    check("wide_ctl_push_pull", 2'b11, 1'b0, 1'b0, 1'b1, 32'hCAFE_BABE);

    // Disconnect via unit_starting
    wide_ctl = 64'h0000_0000_1234_5678;
    unit_starting = 1'b1;
    mode = 2'd2;
    #1; clk = 1'b1; #1; clk = 1'b0;
    check("disconnect", 2'b00, 1'b0, 1'b0, 1'b0, 32'h1234_5678);

    // Let unrelated side_clk toggle 200 times
    #2000;
    check("after_idle", 2'b00, 1'b0, 1'b0, 1'b0, 32'h1234_5678);

    if (errs == 0) $display("PASSED");
    $finish;
  end
endmodule
