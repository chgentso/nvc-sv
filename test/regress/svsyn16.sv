// Expected values: IEEE 1800-2017 25 (interfaces, modports, interface arrays,
// parameterised interfaces, virtual interfaces and virtual interface arrays),
// 27 (generate loops / if, hierarchical names), 20.6.2 ($bits).
// Derived from the producer/consumer code: producer ID sends the data values
// ID*16+k+1 (k = 0..N-1) exactly once each, so with N = 3,4,5 the router must
// deliver 12 items whose data sum is (1+2+3)+(17+..+20)+(33+..+37) = 255; each
// producer's sent counter equals N and the monitor sees exactly N transfers on
// that producer's interface; the 16-bit fast pair sends 81,82 and the slow pair
// 97,98 (2 items, sum 163 and 195).  The identity of the last delivered item
// depends on arbitration timing and is only required to be the final item of
// some producer (3, 20 or 37).  Widths: 8, 16, 8.
// Interface arrays, modports, generate loops/conditionals, parameterised
// interfaces, virtual interface arrays and hierarchical references
interface stream_if #(parameter int W = 8) (input logic clk);
  logic [W-1:0] data;
  logic         valid, ready;
  modport src (output data, valid, input ready, clk);
  modport snk (input data, valid, clk, output ready);
  modport mon (input data, valid, ready, clk);
  function automatic bit fire(); return valid && ready; endfunction
endinterface

module producer #(parameter int W = 8, parameter int ID = 0, parameter int N = 4)
                 (stream_if.src s);
  int sent = 0;
  initial begin
    s.valid = 0; s.data = '0;
    repeat (2 + ID) @(posedge s.clk);
    while (sent < N) begin
      s.data  <= W'(ID * 16 + sent + 1);
      s.valid <= 1;
      @(posedge s.clk);
      if (s.ready) sent++;
      if (sent == N) s.valid <= 0;
    end
  end
endmodule

module consumer #(parameter int W = 8, parameter int STALL = 1) (stream_if.snk s);
  int cnt = 0;
  int sum = 0;
  int tick = 0;
  logic [W-1:0] last;
  always @(posedge s.clk) begin
    tick++;
    s.ready <= (tick % (STALL + 1)) == 0;
    if (s.valid && s.ready) begin
      cnt++; sum += s.data; last = s.data;
    end
  end
  initial s.ready = 0;
endmodule

module router #(parameter int NIN = 3, W = 8) (stream_if.snk in[NIN], stream_if.src out);
  // fixed priority arbitration, input 0 first
  logic [NIN-1:0] req;
  for (genvar k = 0; k < NIN; k++) begin : rq
    assign req[k] = in[k].valid;
  end
  int pick;
  always_comb begin
    pick = -1;
    for (int k = NIN - 1; k >= 0; k--) if (req[k]) pick = k;
  end
  wire can_take;
  assign can_take = !out.valid;
  for (genvar k = 0; k < NIN; k++) begin : ch
    assign in[k].ready = (pick == k) && can_take;
    always @(posedge out.clk) begin
      if (pick == k && can_take) begin
        out.data <= in[k].data;
        out.valid <= 1;
      end
    end
  end
  always @(posedge out.clk) if (out.valid && out.ready) out.valid <= 0;
  initial out.valid = 0;
endmodule

module svsyn16;
  localparam int NP = 3;
  logic clk = 0;
  always #5 clk = ~clk;

  stream_if #(8)  pin[NP] (clk);
  stream_if #(8)  pout (clk);
  stream_if #(16) wide[2] (clk);

  for (genvar i = 0; i < NP; i++) begin : prod
    producer #(.W(8), .ID(i), .N(3 + i)) p (pin[i]);
  end
  router #(.NIN(NP)) r (pin, pout);
  consumer #(.W(8), .STALL(1)) c (pout);

  // wide path: a different parameterisation connected through a generate if
  for (genvar j = 0; j < 2; j++) begin : wp
    if (j == 0) begin : fast
      producer #(.W(16), .ID(5), .N(2)) p (wide[j]);
      consumer #(.W(16), .STALL(0)) c (wide[j]);
    end else begin : slow
      producer #(.W(16), .ID(6), .N(2)) p (wide[j]);
      consumer #(.W(16), .STALL(3)) c (wide[j]);
    end
  end

  // virtual interface array used by a monitor class
  class mon_c;
    virtual stream_if #(8).mon vifs[NP];
    int seen[NP];
    task run();
      forever begin
        @(posedge clk);
        foreach (vifs[i]) if (vifs[i].valid && vifs[i].ready) seen[i]++;
      end
    endtask
  endclass

  mon_c mon = new;
  initial begin
    mon.vifs[0] = pin[0]; mon.vifs[1] = pin[1]; mon.vifs[2] = pin[2];
    fork mon.run(); join_none
    #400;
    begin
      automatic int errs = 0;
      if (c.cnt != 12 || c.sum != 255) begin errs++; $error("router consumed %0d sum %0d, expected 12 / 255", c.cnt, c.sum); end
      if (c.last != 3 && c.last != 20 && c.last != 37) begin errs++; $error("last item %0d is not the final item of a producer", c.last); end
      if (prod[0].p.sent != 3 || mon.seen[0] != 3) begin errs++; $error("producer 0 sent %0d seen %0d, expected 3/3", prod[0].p.sent, mon.seen[0]); end
      if (prod[1].p.sent != 4 || mon.seen[1] != 4) begin errs++; $error("producer 1 sent %0d seen %0d, expected 4/4", prod[1].p.sent, mon.seen[1]); end
      if (prod[2].p.sent != 5 || mon.seen[2] != 5) begin errs++; $error("producer 2 sent %0d seen %0d, expected 5/5", prod[2].p.sent, mon.seen[2]); end
      if (wp[0].fast.c.cnt != 2 || wp[0].fast.c.sum != 163) begin errs++; $error("fast pair %0d/%0d, expected 2/163", wp[0].fast.c.cnt, wp[0].fast.c.sum); end
      if (wp[1].slow.c.cnt != 2 || wp[1].slow.c.sum != 195) begin errs++; $error("slow pair %0d/%0d, expected 2/195", wp[1].slow.c.cnt, wp[1].slow.c.sum); end
      if ($bits(pin[0].data) != 8 || $bits(wide[1].data) != 16 || $bits(pout.data) != 8) begin errs++; $error("widths %0d %0d %0d", $bits(pin[0].data), $bits(wide[1].data), $bits(pout.data)); end
      if (errs == 0) $display("PASSED");
    end
    $finish;
  end
endmodule
