// APB master/monitor/slave written with clocking blocks (default input
// #1step, output #1), virtual interfaces with modports, classes with tasks
// waiting on clocking events.
// Expected values: IEEE 1800-2017 14.3 / 14.4 / 14.5 (clocking block
// events, input sampling in the preponed region with #1step, outputs driven
// 1 time unit after the clocking event), 14.16 (synchronous drives),
// 25.5 (modports), clause 8 classes.  The slave registers pready one edge after it
// sees psel & penable, so each transfer takes setup edge, access edge, and a
// wait edge: with edges at 5, 15, 25, ... (clock starts low, period 10) the
// sampling monitor sees psel & penable & pready at 35, 75, 115, 155 and 195
// (one transfer every 4 edges), carrying the data of each transfer.
interface apb_if(input logic pclk);
  logic psel, penable, pwrite;
  logic [15:0] paddr;
  logic [31:0] pwdata, prdata;
  logic pready;
  clocking drv_cb @(posedge pclk);
    default input #1step output #1;
    output psel, penable, pwrite, paddr, pwdata;
    input prdata, pready;
  endclocking
  clocking mon_cb @(posedge pclk);
    default input #1step;
    input psel, penable, pwrite, paddr, pwdata, prdata, pready;
  endclocking
  modport DRV(clocking drv_cb);
  modport MON(clocking mon_cb);
endinterface

module slave(apb_if bus);
  logic [31:0] mem [4];
  always @(posedge bus.pclk) begin
    bus.pready <= 0;
    if (bus.psel && bus.penable) begin
      bus.pready <= 1;
      if (bus.pwrite) mem[bus.paddr[3:2]] <= bus.pwdata;
      else bus.prdata <= mem[bus.paddr[3:2]];
    end
  end
  initial begin
    foreach (mem[i]) mem[i] = 32'h100 + i;
    bus.pready = 0;
  end
endmodule

class apb_driver;
  virtual apb_if.DRV vif;
  task write(input [15:0] a, input [31:0] d);
    @(vif.drv_cb);
    vif.drv_cb.psel <= 1; vif.drv_cb.penable <= 0;
    vif.drv_cb.pwrite <= 1; vif.drv_cb.paddr <= a; vif.drv_cb.pwdata <= d;
    @(vif.drv_cb);
    vif.drv_cb.penable <= 1;
    do @(vif.drv_cb); while (!vif.drv_cb.pready);
    vif.drv_cb.psel <= 0; vif.drv_cb.penable <= 0;
  endtask
  task read(input [15:0] a, output [31:0] d);
    @(vif.drv_cb);
    vif.drv_cb.psel <= 1; vif.drv_cb.penable <= 0;
    vif.drv_cb.pwrite <= 0; vif.drv_cb.paddr <= a;
    @(vif.drv_cb);
    vif.drv_cb.penable <= 1;
    do @(vif.drv_cb); while (!vif.drv_cb.pready);
    d = vif.drv_cb.prdata;
    vif.drv_cb.psel <= 0; vif.drv_cb.penable <= 0;
  endtask
endclass

class apb_monitor;
  virtual apb_if.MON vif;
  int n;
  longint t_log[$];
  string kind_log[$];
  logic [15:0] addr_log[$];
  logic [31:0] data_log[$];
  task run;
    forever begin
      @(vif.mon_cb);
      if (vif.mon_cb.psel && vif.mon_cb.penable && vif.mon_cb.pready) begin
        n++;
        t_log.push_back($time);
        kind_log.push_back(vif.mon_cb.pwrite ? "WR" : "RD");
        addr_log.push_back(vif.mon_cb.paddr);
        data_log.push_back(vif.mon_cb.pwrite ? vif.mon_cb.pwdata : vif.mon_cb.prdata);
      end
    end
  endtask
endclass

module svclk2;
  logic pclk = 0;
  always #5 pclk = ~pclk;
  apb_if bus(pclk);
  slave s(bus);
  int errors = 0;
  initial begin
    automatic apb_driver d = new;
    automatic apb_monitor m = new;
    logic [31:0] r0, r1, r2;
    d.vif = bus; m.vif = bus;
    fork m.run(); join_none
    d.write(16'h4, 32'hDEADBEEF);
    d.write(16'h8, 32'h12345678);
    d.read(16'h4, r0);
    d.read(16'h0, r1);
    d.read(16'h8, r2);
    #1;   // the monitor wakes on the same clocking event as the driver
    if (r0 !== 32'hDEADBEEF) begin $error("read 4: %h", r0); errors++; end
    if (r1 !== 32'h100) begin $error("read 0: %h (initial memory content 32'h100)", r1); errors++; end
    if (r2 !== 32'h12345678) begin $error("read 8: %h", r2); errors++; end
    if (m.n != 5) begin $error("monitor saw %0d transfers, expected 5", m.n); errors++; end
    else begin
      automatic longint exp_t[5] = '{35, 75, 115, 155, 195};
      automatic string exp_k[5] = '{"WR", "WR", "RD", "RD", "RD"};
      automatic logic [15:0] exp_a[5] = '{16'h4, 16'h8, 16'h4, 16'h0, 16'h8};
      automatic logic [31:0] exp_d[5] = '{32'hDEADBEEF, 32'h12345678, 32'hDEADBEEF, 32'h100, 32'h12345678};
      for (int k = 0; k < 5; k++)
        if (m.t_log[k] != exp_t[k] || m.kind_log[k] != exp_k[k] || m.addr_log[k] !== exp_a[k]
            || m.data_log[k] !== exp_d[k]) begin
          $error("transfer %0d: t=%0d %s addr=%h data=%h expected t=%0d %s addr=%h data=%h", k,
                 m.t_log[k], m.kind_log[k], m.addr_log[k], m.data_log[k],
                 exp_t[k], exp_k[k], exp_a[k], exp_d[k]);
          errors++;
        end
    end
    if (errors == 0) $display("PASSED");
    $finish;
  end
endmodule
