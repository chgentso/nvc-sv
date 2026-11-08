`timescale 1ns/1ns
// Nonblocking assignments to wide vectors, part selects, structure members and a task formal
// Expected values: IEEE 1800-2017 10.4.2 (the nonblocking updates of a time step happen after the active events),
// 11.5.1 (part selects as assignment targets), 7.2 (struct members), 13.5 (copy-out of an inout struct); the
// expected vectors are built with blocking assignments of the same slices (reference model)
module svnbawide1;
  typedef struct { logic [3:0][31:0] d; bit [127:0] q; int n; } s_t;
  logic [127:0] w;
  s_t s, u;
  int fails = 0;
  task t(inout s_t p);
    p.d[1] <= 32'hdeadbeef;
    p.q[100:70] <= 31'h7fffffff;
    p.n <= 5;
    #1;
  endtask
  logic [127:0] mw;
  logic [3:0][31:0] md;
  bit [127:0] mq;
  initial begin
    w = 0; s.n = 0; s.d = '0; s.q = '0;
    w <= 128'h1234_5678_9abc_def0_1111_2222_3333_4444;
    #1 if (w !== 128'h1234_5678_9abc_def0_1111_2222_3333_4444) begin $error("t=1: w=%h", w); fails++; end
    w[71:40] <= 32'hffffffff;
    w[3:0] <= 4'h9;
    mw = 128'h1234_5678_9abc_def0_1111_2222_3333_4444;
    mw[71:40] = 32'hffffffff;
    mw[3:0] = 4'h9;
    #1 if (w !== mw) begin $error("t=2: w=%h expected %h", w, mw); fails++; end
    // the task changes s through the inout formal; the copy-out at its return carries the updates
    t(s);
    md = '0; md[1] = 32'hdeadbeef;
    mq = '0; mq[100:70] = 31'h7fffffff;
    if (s.d !== md || s.q !== mq || s.n !== 5) begin $error("t=3: d=%h q=%h n=%0d", s.d, s.q, s.n); fails++; end
    s.d[2] <= 32'hcafebabe;
    s.d[0][7:4] <= 4'hx;
    s.q[0] <= 1'b1;
    s.q[127:120] <= 8'ha5;
    md[2] = 32'hcafebabe;
    md[0][7:4] = 4'hx;
    mq[0] = 1'b1;
    mq[127:120] = 8'ha5;
    #1 if (s.d !== md || s.q !== mq) begin $error("t=4: d=%h q=%h", s.d, s.q); fails++; end
    if (s.d[0] !== 32'h000000x0) begin $error("t=4: d[0]=%b", s.d[0]); fails++; end
    u.d = '1;
    u.d[3][3:0] <= 4'b01zx;
    u.d[2] <= 32'h1;
    #1;
    if (u.d !== {{28'hfffffff, 4'b01zx}, 32'h1, 32'hffffffff, 32'hffffffff}) begin
      $error("t=5: d=%b", u.d); fails++;
    end
    if (fails == 0) $display("PASSED");
  end
endmodule
