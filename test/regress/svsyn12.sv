// Expected values: IEEE 1800-2017 14 (clocking blocks: 14.4 #1step input
// sampling, 14.11 ##N cycle delay, 14.16 synchronous drives with output skew),
// 3.5 / 24 (program blocks, final in a program), 9.3.2 (fork-join).
// Hand-derived timeline (posedge clk at 5+10n; DUT latency = 2 clock edges
// after it sees req; drives at a clock event are applied at event+skew #2):
//   ##3 -> 25;  write 10 drives at 27, DUT acks (ack high after edge 45), the
//   tb samples ack=1 at edge 55;  ##2 -> 75;  read 10 completes at edge 105
//   (data 1234);  read 20 completes at 135 (data A020, the initial memory);
//   fork: write 30 completes at 165, the ##1 branch ends at 145;  read 30
//   completes at 195 (data BEEF);  ##2 -> $finish at 215.
// The monitor (sampling at the same edges) must log five transfers, at 55,
// 105, 135, 165 and 195.  Values of rdata on writes are not checked.
// Clocking blocks, modports, a program block and cycle delays
interface bus_if (input logic clk);
  logic        req, ack, we;
  logic [7:0]  addr;
  logic [15:0] wdata, rdata;
  clocking drv_cb @(posedge clk);
    default input #1step output #2;
    output req, we, addr, wdata;
    input  ack, rdata;
  endclocking
  clocking mon_cb @(posedge clk);
    default input #1step;
    input req, ack, we, addr, wdata, rdata;
  endclocking
  modport dut (input clk, req, we, addr, wdata, output ack, rdata);
  modport tb  (clocking drv_cb, input clk);
  modport mon (clocking mon_cb);
endinterface

module mem_dut (bus_if.dut b);
  logic [15:0] mem [0:255];
  int lat = 0;
  always @(posedge b.clk) begin
    b.ack <= 1'b0;
    if (b.req && !b.ack) begin
      if (lat < 1) lat++;
      else begin
        lat = 0;
        if (b.we) mem[b.addr] <= b.wdata;
        else b.rdata <= mem[b.addr];
        b.ack <= 1'b1;
      end
    end
  end
  initial for (int i = 0; i < 256; i++) mem[i] = 16'hA000 + i;
endmodule

program automatic tb_prog (bus_if.tb t, bus_if.mon m);
  int errors, reads, writes;
  longint t_fd = -1, t_end;
  longint tr[$]; logic [15:0] dr[$];
  task automatic ck(longint got, longint exp, string what);
    if (got !== exp) begin $error("%s: got %0d expected %0d", what, got, exp); errors++; end
  endtask
  default clocking pcb @(posedge t.clk); endclocking
  task automatic write(input [7:0] a, input [15:0] d);
    t.drv_cb.addr <= a; t.drv_cb.wdata <= d; t.drv_cb.we <= 1; t.drv_cb.req <= 1;
    do @(t.drv_cb); while (!t.drv_cb.ack);
    t.drv_cb.req <= 0;
    writes++;
  endtask
  task automatic read(input [7:0] a, output [15:0] d);
    t.drv_cb.addr <= a; t.drv_cb.we <= 0; t.drv_cb.req <= 1;
    do @(t.drv_cb); while (!t.drv_cb.ack);
    d = t.drv_cb.rdata;
    t.drv_cb.req <= 0;
    reads++;
  endtask
  initial begin
    logic [15:0] d;
    t.drv_cb.req <= 0;
    ##3;
    write(8'h10, 16'h1234);
    ##2;
    read(8'h10, d);
    tr.push_back($time); dr.push_back(d);
    if (d !== 16'h1234) errors++;
    read(8'h20, d);
    tr.push_back($time); dr.push_back(d);
    if (d !== 16'hA020) errors++;
    fork
      begin write(8'h30, 16'hbeef); end
      begin ##1; t_fd = $time; end
    join
    read(8'h30, d);
    tr.push_back($time); dr.push_back(d);
    ck(reads, 3, "reads"); ck(writes, 2, "writes");
    ##2;
    t_end = $time;
    $finish;
  end
  int nmon;
  longint mt[$]; bit mwe[$]; logic [7:0] ma[$]; logic [15:0] mw[$], mr[$];
  initial forever begin
    @(m.mon_cb);
    if (m.mon_cb.req && m.mon_cb.ack) begin
      nmon++;
      mt.push_back($time); mwe.push_back(m.mon_cb.we); ma.push_back(m.mon_cb.addr);
      mw.push_back(m.mon_cb.wdata); mr.push_back(m.mon_cb.rdata);
    end
  end
  final begin
    automatic longint et[5] = '{55, 105, 135, 165, 195};
    automatic bit ewe[5] = '{1, 0, 0, 1, 0};
    automatic logic [7:0] ea[5] = '{8'h10, 8'h10, 8'h20, 8'h30, 8'h30};
    ck(nmon, 5, "monitor transfers");
    if (nmon == 5) for (int i = 0; i < 5; i++) begin
      ck(mt[i], et[i], $sformatf("monitor time #%0d", i));
      ck(mwe[i], ewe[i], $sformatf("monitor we #%0d", i));
      ck(ma[i], ea[i], $sformatf("monitor addr #%0d", i));
    end
    ck(mw[0], 16'h1234, "monitor wdata of write 10"); ck(mw[3], 16'hbeef, "monitor wdata of write 30");
    ck(mr[1], 16'h1234, "monitor rdata of read 10"); ck(mr[2], 16'hA020, "monitor rdata of read 20");
    ck(mr[4], 16'hbeef, "monitor rdata of read 30");
    ck(tr.size(), 3, "program reads logged");
    if (tr.size() == 3) begin
      ck(tr[0], 105, "read 10 time"); ck(dr[0], 16'h1234, "read 10 data");
      ck(tr[1], 135, "read 20 time"); ck(dr[1], 16'hA020, "read 20 data");
      ck(tr[2], 195, "read 30 time"); ck(dr[2], 16'hbeef, "read 30 data");
    end
    ck(t_fd, 145, "forked ##1 branch time");
    ck(t_end, 215, "end time");
    ck(errors, 0, "data mismatches");
    if (errors == 0) $display("PASSED");
  end
endprogram

module svsyn12;
  logic clk = 0;
  always #5 clk = ~clk;
  bus_if bus (clk);
  mem_dut dut (bus);
  tb_prog prog (bus, bus);
endmodule
