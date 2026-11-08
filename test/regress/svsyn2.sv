// Expected values: IEEE 1800-2017 14 (clocking blocks), 25 (interfaces, modports,
// virtual interfaces), 16.8 / 16.9 / 16.12 (SVA: local variables, $rose, ##[m:n],
// |->, |=>, [*m:n], disable iff), 16.14.3 (cover property counts succeeding
// attempts), 18 (randomize with inline constraints), 19 (covergroups: auto
// bins, explicit bins, cross, coverage = covered bins / bins, 19.11).
// Derived expectations:
//  - (the slave ignores req while ack is visible, otherwise the request still
//    held in the cycle after ack would be accepted a second time);
//  - 24 transfers (12 writes, 12 reads) are driven, all observed and
//    completed by the monitor; reads of addresses written earlier return the
//    data written (errors = 0);
//  - the slave answers after 3+addr[1:0] monitor cycles, so each latency value
//    3,4,5,6 occurs for 6 transfers (addr = i%16, i = 0..23);
//  - coverage: we 100%, addr[1:0] 100%, latency 50% (bin fast {1,2} is never
//    hit, slow {3:5} is; latency 6 is in no bin), cross 100%;
//  - assertion/cover counters equal a procedural evaluation of the same
//    properties on values sampled just before every clock edge.  Pass counts
//    are checked from below by non-vacuous successes only (the LRM's vacuous
//    success handling is not relied on).
// Request/response bus with a pipelined slave, class based driver and monitor
// through a virtual interface, protocol assertions with local variables and
// functional coverage of the observed transfers
interface bus_if (input logic clk);
  logic        rst_n;
  logic        req;
  logic        we;
  logic [7:0]  addr;
  logic [15:0] wdata;
  logic        ack;
  logic [15:0] rdata;

  clocking drv_cb @(posedge clk);
    default input #1step output #1;
    output req, we, addr, wdata;
    input  ack, rdata;
  endclocking

  clocking mon_cb @(posedge clk);
    default input #1step;
    input req, we, addr, wdata, ack, rdata;
  endclocking

  modport drv (clocking drv_cb, input rst_n);
  modport mon (clocking mon_cb, input rst_n);

  // a request is acknowledged within 1..3 cycles and the request is held
  sequence s_req_ack;
    req && !ack ##1 (req && !ack)[*0:2] ##1 ack;
  endsequence
  property p_ack_in_time;
    @(posedge clk) disable iff (!rst_n)
      $rose(req) |-> ##[1:3] ack;
  endproperty
  int n_ack_fail, n_hold_fail, n_ack_pass, n_hold_pass, n_raw_pass, n_raw_fail, c_raw, c_sra;
  a_ack: assert property (p_ack_in_time) n_ack_pass++; else n_ack_fail++;

  property p_hold;
    logic [7:0] a; logic w; logic [15:0] d;
    @(posedge clk) disable iff (!rst_n)
      (req && !ack, a = addr, w = we, d = wdata) |=> (addr == a && we == w && wdata == d);
  endproperty
  a_hold: assert property (p_hold) n_hold_pass++; else n_hold_fail++;

  property p_read_after_write;
    logic [7:0] a; logic [15:0] d;
    @(posedge clk) disable iff (!rst_n)
      (req && ack && we, a = addr, d = wdata) ##[1:20] (req && ack && !we && addr == a) |-> (rdata == d);
  endproperty
  a_raw: assert property (p_read_after_write) n_raw_pass++; else n_raw_fail++;
  c_raw_p: cover property (@(posedge clk) disable iff (!rst_n) (req && ack && we) ##[1:20] (req && ack && !we)) c_raw++;

  cover property (@(posedge clk) disable iff (!rst_n) s_req_ack) c_sra++;
endinterface

module slave (bus_if bus);
  logic [15:0] mem [0:255];
  logic [1:0]  cnt;
  logic        busy;
  always @(posedge bus.clk or negedge bus.rst_n) begin
    if (!bus.rst_n) begin
      bus.ack <= 0; bus.rdata <= 0; cnt <= 0; busy <= 0;
    end
    else begin
      bus.ack <= 0;
      // ignore req in the cycle where ack is visible: the master drops req only
      // after it has sampled ack, so the old request must not be accepted twice
      if (bus.req && !busy && !bus.ack) begin
        busy <= 1; cnt <= bus.addr[1:0];
      end
      else if (busy) begin
        if (cnt == 0) begin
          busy <= 0; bus.ack <= 1;
          if (bus.we) mem[bus.addr] <= bus.wdata;
          else bus.rdata <= mem[bus.addr];
        end
        else cnt <= cnt - 1;
      end
    end
  end
endmodule

package bus_pkg;
  class xfer;
    rand bit        we;
    rand bit [7:0]  addr;
    rand bit [15:0] data;
    bit  [15:0]     rdata;
    int             latency;
  endclass

  class monitor;
    virtual bus_if.mon vif;
    xfer seen[$];
    covergroup cg with function sample(xfer x);
      cp_we: coverpoint x.we;
      cp_addr: coverpoint x.addr[1:0];
      cp_lat: coverpoint x.latency { bins fast = {[1:2]}; bins slow = {[3:5]}; }
      cx: cross cp_we, cp_addr;
    endgroup
    function new(virtual bus_if.mon v); vif = v; cg = new(); endfunction
    task run();
      forever begin
        xfer x = new();
        int start;
        @(vif.mon_cb iff (vif.mon_cb.req && !vif.mon_cb.ack));
        x.we = vif.mon_cb.we; x.addr = vif.mon_cb.addr; x.data = vif.mon_cb.wdata;
        x.latency = 1;
        while (!vif.mon_cb.ack) begin @(vif.mon_cb); x.latency++; end
        x.rdata = vif.mon_cb.rdata;
        seen.push_back(x);
        cg.sample(x);
        @(vif.mon_cb);
      end
    endtask
  endclass

  class driver;
    virtual bus_if.drv vif;
    int done;
    function new(virtual bus_if.drv v); vif = v; endfunction
    task xfer_one(xfer x);
      vif.drv_cb.req <= 1; vif.drv_cb.we <= x.we; vif.drv_cb.addr <= x.addr; vif.drv_cb.wdata <= x.data;
      @(vif.drv_cb);
      while (!vif.drv_cb.ack) @(vif.drv_cb);
      x.rdata = vif.drv_cb.rdata;
      vif.drv_cb.req <= 0;
      @(vif.drv_cb);
      done++;
    endtask
  endclass
endpackage

module svsyn2;
  import bus_pkg::*;
  logic clk = 0;
  always #5 clk = ~clk;
  bus_if bus (clk);
  slave dut (bus);

  initial begin bus.rst_n = 0; bus.req = 0; bus.we = 0; bus.addr = 0; bus.wdata = 0; #22 bus.rst_n = 1; end

  driver drv;
  monitor mon;
  xfer t;
  bit [15:0] shadow [bit [7:0]];
  int errors, nread;

  // ---- sampled values (end of the time step before each clock edge) ----
  localparam int NE = 400;
  logic H_rst[NE], H_req[NE], H_we[NE], H_ack[NE]; logic [7:0] H_addr[NE]; logic [15:0] H_wd[NE], H_rd[NE];
  int he;
  initial begin
    #4;
    forever begin
      #0;
      if (he < NE) begin
        H_rst[he] = bus.rst_n; H_req[he] = bus.req; H_we[he] = bus.we; H_addr[he] = bus.addr;
        H_wd[he] = bus.wdata; H_ack[he] = bus.ack; H_rd[he] = bus.rdata; he++;
      end
      #10;
    end
  end
  function automatic bit T(logic x); return x === 1'b1; endfunction
  // disable iff (!rst_n): rst_n is low only before time 22 (edges 0 and 1)
  function automatic bit dis(int s); return s <= 1; endfunction
  task automatic ck(longint got, longint exp, string what);
    if (got !== exp) begin $error("%s: got %0d expected %0d", what, got, exp); errors++; end
  endtask
  task automatic check();
    int nv_ack, nv_hold, tot_ack, tot_hold, fl_ack, fl_hold, fl_raw, cv_raw, cv_sra, n, lat_cnt[int];
    real c_we, c_addr, c_lat, c_x;
    n = he;
    // a_ack: $rose(req) |-> ##[1:3] ack
    for (int e = 1; e < n; e++) if (!dis(e) && T(H_req[e]) && !T(H_req[e-1])) begin
      if (e + 3 < n) begin
        if (T(H_ack[e+1]) || T(H_ack[e+2]) || T(H_ack[e+3])) nv_ack++; else fl_ack++;
      end
    end
    for (int e = 0; e + 1 < n; e++) if (!dis(e)) begin
      // a_hold: (req && !ack, locals = addr,we,wdata) |=> locals unchanged
      if (T(H_req[e]) && !T(H_ack[e])) begin
        if (H_addr[e+1] == H_addr[e] && H_we[e+1] == H_we[e] && H_wd[e+1] == H_wd[e]) nv_hold++; else fl_hold++;
      end
    end
    // a_raw: write-ack ##[1:20] read-ack to the same address |-> rdata == written data
    for (int e = 0; e < n; e++) if (!dis(e) && T(H_req[e]) && T(H_ack[e]) && T(H_we[e])) begin
      bit failed = 0, matched = 0;
      for (int j = 1; j <= 20 && e + j < n; j++)
        if (T(H_req[e+j]) && T(H_ack[e+j]) && !T(H_we[e+j]) && H_addr[e+j] == H_addr[e]) begin
          matched = 1;
          if (H_rd[e+j] !== H_wd[e]) failed = 1;
        end
      if (failed) fl_raw++;
    end
    // c_raw_p: (req&&ack&&we) ##[1:20] (req&&ack&&!we), one count per succeeding attempt
    for (int e = 0; e < n; e++) if (!dis(e) && T(H_req[e]) && T(H_ack[e]) && T(H_we[e])) begin
      bit m = 0;
      for (int j = 1; j <= 20 && e + j < n; j++) if (T(H_req[e+j]) && T(H_ack[e+j]) && !T(H_we[e+j])) m = 1;
      if (m) cv_raw++;
    end
    // c_sra: req&&!ack ##1 (req&&!ack)[*0:2] ##1 ack
    for (int e = 0; e < n; e++) if (!dis(e) && T(H_req[e]) && !T(H_ack[e])) begin
      bit m = 0;
      for (int k = 1; k <= 3 && e + k < n; k++) begin
        bit ok = T(H_ack[e+k]);
        for (int q = 1; q < k; q++) ok &= T(H_req[e+q]) && !T(H_ack[e+q]);
        if (ok) m = 1;
      end
      if (m) cv_sra++;
    end
    ck(bus.n_ack_fail, fl_ack, "a_ack failures");
    ck(bus.n_hold_fail, fl_hold, "a_hold failures");
    ck(bus.n_raw_fail, fl_raw, "a_raw failures");
    if (bus.n_ack_pass < nv_ack) begin errors++; $error("a_ack passes %0d < non-vacuous %0d", bus.n_ack_pass, nv_ack); end
    if (bus.n_hold_pass < nv_hold) begin errors++; $error("a_hold passes %0d < non-vacuous %0d", bus.n_hold_pass, nv_hold); end
    ck(bus.c_raw, cv_raw, "c_raw cover");
    ck(bus.c_sra, cv_sra, "c_sra cover");
    if (nv_ack == 0 || nv_hold == 0) begin errors++; $error("model sanity: a_ack %0d a_hold %0d non-vacuous", nv_ack, nv_hold); end
    // transfers and scoreboard
    ck(drv.done, 24, "driver transfers"); ck(mon.seen.size(), 24, "monitor transfers");
    ck(nread, 12, "reads"); ck(errors > 0 ? 1 : 0, 0, "data mismatches so far");
    ck(mon.seen.sum() with (int'(item.latency == 1)), 0, "latency 1 count");
    ck(mon.seen.sum() with (int'(item.latency == 2)), 0, "latency 2 count");
    ck(mon.seen.sum() with (int'(item.latency == 3)), 6, "latency 3 count");
    ck(mon.seen.sum() with (int'(item.latency == 4)), 6, "latency 4 count");
    ck(mon.seen.sum() with (int'(item.latency == 5)), 6, "latency 5 count");
    ck(mon.seen.sum() with (int'(item.latency == 6)), 6, "latency 6 count");
    foreach (mon.seen[i]) begin
      if (mon.seen[i].latency != 3 + mon.seen[i].addr[1:0]) begin errors++; $error("transfer %0d latency %0d, expected %0d", i, mon.seen[i].latency, 3 + mon.seen[i].addr[1:0]); end
      if (mon.seen[i].we != (i < 8 || i >= 20)) begin errors++; $error("monitor transfer %0d we mismatch", i); end
    end
    c_we = mon.cg.cp_we.get_inst_coverage(); c_addr = mon.cg.cp_addr.get_inst_coverage();
    c_lat = mon.cg.cp_lat.get_inst_coverage(); c_x = mon.cg.cx.get_inst_coverage();
    if (c_we != 100.0 || c_addr != 100.0 || c_lat != 50.0 || c_x != 100.0) begin
      errors++; $error("coverage we=%f addr=%f lat=%f cross=%f, expected 100/100/50/100", c_we, c_addr, c_lat, c_x);
    end
    if (errors == 0) $display("PASSED");
  endtask

  initial begin
    drv = new(bus.drv);
    mon = new(bus.mon);
    fork mon.run(); join_none
    @(posedge bus.rst_n);
    repeat (2) @(posedge clk);
    for (int i = 0; i < 24; i++) begin
      t = new();
      if (!t.randomize() with { addr == (i % 16); data == 16'(i * 37 + 11); we == (i < 8 || i >= 20); }) begin $error("randomize failed"); errors++; end
      drv.xfer_one(t);
      if (t.we) shadow[t.addr] = t.data;
      else begin
        nread++;
        if (shadow.exists(t.addr) && shadow[t.addr] !== t.rdata) begin
          errors++;
          $error("MISMATCH addr=%0d exp=%h got=%h", t.addr, shadow[t.addr], t.rdata);
        end
      end
    end
    repeat (5) @(posedge clk);
    #1;  // let the action blocks of the last edge run
    check();
    $finish;
  end
endmodule
