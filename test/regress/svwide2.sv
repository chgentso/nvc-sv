// 100-bit elements in queues, dynamic arrays, associative arrays and fixed arrays.
// Expected values: IEEE 1800-2017 7.10 (queues: push, insert, pop), 7.5 (dynamic arrays), 7.8
// (associative arrays), 7.4 (fixed arrays; a 4-state element starts at x, 6.8), 11.5.1 (selects),
// 11.3.3 (arithmetic modulo 2**100), 11.4.10 (shift), 11.4.8 (~).
module svwide2;
  typedef logic [99:0] w_t;
  w_t q[$];
  w_t d[];
  w_t a[string];
  w_t f[3];
  int errors = 0;
  w_t b0, b1;
  initial begin
    q.push_back(100'h1); q.push_back(100'h2); q.push_back(100'h3);
    q[1] = 100'hABCDEF0123456789ABCDEF012;
    q[2] = q[1] + 1;
    q[0][99] = 1'b1;
    q[0][3:0] = 4'hA;
    if (q[0] !== 100'h800000000000000000000000a) begin $error("q0 %h", q[0]); errors++; end
    if (q[1] !== 100'hABCDEF0123456789ABCDEF012) begin $error("q1 %h", q[1]); errors++; end
    if (q[2] !== 100'hABCDEF0123456789ABCDEF013) begin $error("q2 %h", q[2]); errors++; end
    d = new[3];
    d[0] = 100'h7;
    d[1] = 100'hFFFFFFFFFFFFFFFFFFFFFFFFF;
    d[2] = d[1] + d[0];
    d[2][70] = 1'b0;
    if (d[0] !== 100'h7) begin $error("d0 %h", d[0]); errors++; end
    if (d[1] !== 100'hFFFFFFFFFFFFFFFFFFFFFFFFF) begin $error("d1 %h", d[1]); errors++; end
    if (d[2] !== 100'h6) begin $error("d2 %h", d[2]); errors++; end
    a["x"] = 100'h55555555555555555555555;
    a["y"] = a["x"] << 4;
    if (a["x"] !== 100'h0055555555555555555555555) begin $error("ax %h", a["x"]); errors++; end
    if (a["y"] !== 100'h0555555555555555555555550) begin $error("ay %h", a["y"]); errors++; end
    if (a.num() != 2) begin $error("a.num %0d", a.num()); errors++; end
    f[0] = 100'h9; f[2] = ~f[0];
    f[1][99:90] = 10'h3FF;
    if (f[0] !== 100'h9) begin $error("f0 %h", f[0]); errors++; end
    // f[1] starts at x; only bits 99:90 were written
    if (f[1] !== {10'h3FF, {90{1'bx}}}) begin $error("f1 %b", f[1]); errors++; end
    if (f[2] !== 100'hffffffffffffffffffffffff6) begin $error("f2 %h", f[2]); errors++; end
    q[1]++;
    q[1] += 100'h10;
    if (q[1] !== 100'habcdef0123456789abcdef023) begin $error("q1b %h", q[1]); errors++; end
    q.insert(1, 100'hDEAD);
    q[0] = q[3] ^ q[1];
    if (q.size() != 4) begin $error("q.size %0d", q.size()); errors++; end
    if (q[0] !== 100'habcdef0123456789abcde2ebe) begin $error("q0b %h", q[0]); errors++; end
    if (q[1] !== 100'hdead) begin $error("q1c %h", q[1]); errors++; end
    if (q[2] !== 100'habcdef0123456789abcdef023) begin $error("q2b %h", q[2]); errors++; end
    if (q[3] !== 100'habcdef0123456789abcdef013) begin $error("q3b %h", q[3]); errors++; end
    b0 = q.pop_front(); b1 = q.pop_back();
    if (b0 !== 100'habcdef0123456789abcde2ebe || b1 !== 100'habcdef0123456789abcdef013) begin
      $error("pops %h %h", b0, b1); errors++;
    end
    if (q.size() != 2) begin $error("q.size after pops %0d", q.size()); errors++; end
    if (errors == 0) $display("PASSED");
  end
endmodule
