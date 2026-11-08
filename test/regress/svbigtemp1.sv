// A very large struct passed as a task output argument and copied through temporaries (self-checking).
// Expected values: IEEE 1800-2017 13.5 (output arguments), 14.3/14.4 (clocking block with #1step input skew,
// @(cb) waits for the next clocking event), 6.24/7.2 (struct member selects), 21.3 ($sformatf, %x), 6.16
// (string len/substr), 8.5/8.26 (virtual interface), 9.3.2 (fork/join_none).
// Reference model: clk rises at 10,30,...; the request (v=r=1,d=abcd) is applied at t=2005, so the first
// sample after the process started is taken at the edge at 2010, the 101st edge, with the pre-edge values.
// The second call starts in the same time step and waits for the next edge (2030); v=r=1,d=99 are applied
// at t=2625, so it returns at the edge 2630 after 31 edges (2030..2630).
`timescale 1ns/1ns
typedef struct {
  bit [3:0] id;
  bit [256:0][511:0] wdata;
  bit [256:0][63:0] wstrb;
  int cnt;
} big_s;

interface bfm(input bit clk, input logic v, input logic r, input logic [511:0] d);
  string msgs[$];
  clocking cb @(posedge clk);
    default input #1step output #1step;
    input v, r, d;
  endclocking
  task sample(output big_s req);
    static int i = 0;
    automatic int n = 0;
    string msg;
    do begin
      @(cb);
      n++;
    end while (cb.v !== 1 || cb.r !== 1);
    req.id = 3;
    req.wdata[0] = cb.d;
    req.cnt = n;
    msg = $sformatf("wdata[%0d]=%0x wstrb=%0x", i, req.wdata[i], req.wstrb[i]);
    msgs.push_back(msg);
  endtask
endinterface

class proxy;
  virtual bfm vif;
  int got;
  longint tms[$];
  int ids[$], cnts[$];
  bit [15:0] ds[$];
  task run();
    forever begin
      big_s p;
      vif.sample(p);
      got++;
      tms.push_back($time); ids.push_back(p.id); cnts.push_back(p.cnt); ds.push_back(p.wdata[0][15:0]);
    end
  endtask
endclass

module svbigtemp1;
  bit clk = 0;
  logic v = 0, r = 0;
  logic [511:0] d = 512'h1234;
  int errs = 0;
  string m0;
  always #10 clk = ~clk;
  bfm b(clk, v, r, d);
  proxy px;

  task automatic ck(input bit c, input string m);
    if (!c) begin errs++; $error("%s", m); end
  endtask

  initial begin
    px = new;
    px.vif = b;
    fork px.run(); join_none
    #2005 v = 1; r = 1; d = 512'habcd;
    #20 v = 0; d = 512'h77;
    #600 v = 1; d = 512'h99;
    #20 v = 0;
    #50;
    ck(px.got == 2, "two requests");
    if (px.got == 2) begin
      ck(px.tms[0] == 2010 && px.tms[1] == 2630, "request times");
      ck(px.ids[0] == 3 && px.ids[1] == 3, "ids");
      ck(px.cnts[0] == 101 && px.cnts[1] == 31, "edge counts");
      ck(px.ds[0] == 16'habcd && px.ds[1] == 16'h0099, "data");
    end
    ck(b.msgs.size() == 2, "two messages");
    if (b.msgs.size() == 2) begin
      // wstrb of the freshly created local is zero, wdata[0] holds the sampled data (%0x: no leading zeros)
      ck(b.msgs[0] == "wdata[0]=abcd wstrb=0", "message 0");
      ck(b.msgs[1] == "wdata[0]=99 wstrb=0", "message 1");
      m0 = b.msgs[0];
      ck(m0.len() == 21 && m0.substr(0, 11) == "wdata[0]=abc", "message 0 length/substr");
    end
    if (errs == 0) $display("PASSED");
    $finish;
  end
endmodule
