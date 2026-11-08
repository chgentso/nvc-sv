// Expected values: IEEE 1800-2017 11.4.14 (streaming: >> keeps order, << reverses slices; a queue or dynamic
// array target is sized to the stream and its element width cuts the stream), 7.10 (queues),
// 6.8 (initializers; here constants so that no order between initializers is assumed)
module svstreamq1;
  localparam bit [31:0] D0 = 32'h11223344;
  localparam bit [15:0] E0 = 16'habcd;
  bit [31:0] d = D0;
  bit [15:0] e = E0;
  int errs = 0;
  task automatic ck(string w, int got, int exp);
    if (got != exp) begin $error("%s: got %h expected %h", w, got, exp); errs++; end
  endtask

  initial begin
    automatic bit [7:0] q[$] = {<< 8{D0}};
    automatic bit [7:0] r[$] = {>> 8{D0}};
    bit [7:0] s[$];
    bit [15:0] w[];
    automatic bit [3:0] n[$] = {<< 4{E0}};
    byte b[$];
    int unsigned dyn[];

    ck("size", q.size(), 4);
    ck("q0", q[0], 8'h44); ck("q1", q[1], 8'h33); ck("q2", q[2], 8'h22); ck("q3", q[3], 8'h11);
    ck("r size", r.size(), 4);
    ck("r0", r[0], 8'h11); ck("r1", r[1], 8'h22); ck("r2", r[2], 8'h33); ck("r3", r[3], 8'h44);
    foreach (q[i]) s.push_back(q[i]);
    ck("s size", s.size(), 4);
    foreach (s[i]) ck("s", s[i], q[i]);
    q = {<< 8{e, e}};   // abcdabcd: bytes ab cd ab cd, reversed cd ab cd ab
    ck("q2 size", q.size(), 4);
    ck("q2[0]", q[0], 8'hcd); ck("q2[1]", q[1], 8'hab); ck("q2[2]", q[2], 8'hcd); ck("q2[3]", q[3], 8'hab);
    w = {>> 16{d}};
    ck("w size", w.size(), 2);
    ck("w0", w[0], 16'h1122); ck("w1", w[1], 16'h3344);
    ck("n size", n.size(), 4);   // nibbles a b c d reversed
    ck("n0", n[0], 4'hd); ck("n1", n[1], 4'hc); ck("n2", n[2], 4'hb); ck("n3", n[3], 4'ha);
    b = {>> 8{e, d}};
    ck("b size", b.size(), 6);
    ck("b0", b[0] & 8'hff, 8'hab); ck("b1", b[1] & 8'hff, 8'hcd); ck("b2", b[2] & 8'hff, 8'h11);
    ck("b3", b[3] & 8'hff, 8'h22); ck("b4", b[4] & 8'hff, 8'h33); ck("b5", b[5] & 8'hff, 8'h44);
    dyn = {>> 32{d, d}};
    ck("dyn size", dyn.size(), 2);
    ck("dyn0", dyn[0], 32'h11223344); ck("dyn1", dyn[1], 32'h11223344);
    if (errs == 0) $display("PASSED");
  end
endmodule
