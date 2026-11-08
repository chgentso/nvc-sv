`timescale 1ns/1ns
interface bus_if #(parameter int W = 8) (input logic clk);
  logic [W-1:0] data;
  logic valid, ready;
  modport master (import send, output data, valid, input ready, clk);
  modport slave (input data, valid, clk, output ready);
  task automatic send(input logic [W-1:0] d);
    @(posedge clk); data <= d; valid <= 1;
    @(posedge clk); valid <= 0;
  endtask
  function automatic logic [W-1:0] peek(); return data; endfunction
  clocking cb @(posedge clk);
    input data, valid;
    output ready;
  endclocking
endinterface
module producer (bus_if.master b);
  initial begin
    b.send(8'h11); b.send(8'h22);
  end
endmodule
module consumer (bus_if.slave b);
  logic [15:0] rd[$];
  time rt[$];
  always @(posedge b.clk) begin
    if (b.valid) begin rd.push_back(b.data); rt.push_back($time); end
    b.ready <= 1;
  end
endmodule
module svlrm_c25a;
  // Self-checking test. Expected values: IEEE 1800-2017 25.3 (interfaces, parameterised
  // interface instances), 25.4 (modports, ports of interface type with modport selection, import of
  // an interface task), 25.5 (interface tasks and functions called through a port), 25.7 (clocking
  // block in an interface), 25.9 (virtual interface: assignment from an instance, access to
  // members and calling interface functions through it), 20.6.2 ($bits), 4.7/9.4.2 (nonblocking
  // assignments are applied after the active events of the time step, so the consumer sees at a
  // clock edge the values of the previous edge).
  // Timeline (clk rises at 5, 15, 25, 35): producer sets data=11, valid=1 at 5 (NBA); valid=0 at 15;
  // data=22, valid=1 at 25; valid=0 at 35. The consumer samples valid/data before the NBAs of the
  // same edge, so it receives 11 at 15 and 22 at 35, and nothing at 5 (valid is x) or 25 (valid is 0).
  logic clk = 0;
  always #5 clk = ~clk;
  bus_if #(8) bi (clk);
  bus_if #(16) bi16 (clk);
  producer p (bi);
  consumer c (bi);
  virtual bus_if #(8) vb;
  int errs;
  function automatic void ck(bit ok, string m);
    if (!ok) begin errs++; $error("%s", m); end
  endfunction
  initial begin
    vb = bi;
    #1 ck($bits(bi.data) == 8, "8-bit interface");
    ck($bits(bi16.data) == 16, "16-bit interface");
    #40;
    ck(c.rd.size() == 2 && c.rt.size() == 2, "number of received items");
    ck(c.rd[0] === 16'h0011 && c.rt[0] == 15, "first item at 15");
    ck(c.rd[1] === 16'h0022 && c.rt[1] == 35, "second item at 35");
    ck(vb.peek() === 8'h22, "peek through virtual interface");
    ck(bi.peek() === 8'h22, "peek through instance");
    vb.data = 8'h99; ck(bi.data === 8'h99, "write through virtual interface");
    if (errs == 0) $display("PASSED");
    $finish;
  end
endmodule
