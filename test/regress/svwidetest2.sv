// Two-state vectors wider than 64 bits in reductions, comparisons, logical
// operators, case, inside, loops, wait, event controls and assertions
module svwidetest2;
  typedef bit [127:0] w128;
  int errs = 0;
  `define CHK(c, msg) if (!(c)) begin errs++; $display("FAIL: %s", msg); end
  function automatic bit retbool(bit [127:0] x); return x; endfunction
  function automatic bit retbool384(bit [383:0] x); return x; endfunction
  function automatic bit retneg(bit [127:0] x); return !x; endfunction
  event ev;
  bit [127:0] z, one, hi, ones, w2;
  bit [383:0] z3, o3;
  int hits;
  initial begin
    z = '0; one = 1; hi = 0; hi[127] = 1; ones = '1; z3 = '0; o3 = '1;
    // reductions
    `CHK((&z) === 1'b0, "&z"); `CHK((&ones) === 1'b1, "&ones"); `CHK((&hi) === 1'b0, "&hi");
    `CHK((|z) === 1'b0, "|z"); `CHK((|hi) === 1'b1, "|hi"); `CHK((|one) === 1'b1, "|one");
    `CHK((^z) === 1'b0, "^z"); `CHK((^one) === 1'b1, "^one"); `CHK((^ones) === 1'b0, "^ones");
    `CHK((~&z) === 1'b1, "~&z"); `CHK((~&ones) === 1'b0, "~&ones");
    `CHK((~|z) === 1'b1, "~|z"); `CHK((~|hi) === 1'b0, "~|hi");
    `CHK((~^z) === 1'b1, "~^z"); `CHK((~^one) === 1'b0, "~^one");
    `CHK((|z3) === 1'b0, "|z3"); `CHK((&o3) === 1'b1, "&o3");
    // equality with constants
    `CHK((z == 0) === 1'b1, "z==0"); `CHK((z != 0) === 1'b0, "z!=0");
    `CHK((hi == 0) === 1'b0, "hi==0"); `CHK((hi != 0) === 1'b1, "hi!=0");
    `CHK((z3 == 384'd0) === 1'b1, "z3==0"); `CHK((o3 != 384'd0) === 1'b1, "o3!=0");
    `CHK((z === 128'd0), "z===0"); `CHK((hi !== 128'd0), "hi!==0");
    `CHK((z == '0) === 1'b1, "z=='0"); `CHK((z == '1) === 1'b0, "z=='1");
    // logical ops
    `CHK((z && 1) === 1'b0, "z&&1"); `CHK((hi && 1) === 1'b1, "hi&&1");
    `CHK((z || 0) === 1'b0, "z||0"); `CHK((z || hi) === 1'b1, "z||hi");
    `CHK((!z) === 1'b1, "!z"); `CHK((!hi) === 1'b0, "!hi");
    `CHK((z -> 0) === 1'b1, "z->0"); `CHK((hi -> 0) === 1'b0, "hi->0");
    `CHK((z <-> 0) === 1'b1, "z<->0");
    // ternary
    `CHK((z ? 5 : 6) == 6, "z?:"); `CHK((hi ? 5 : 6) == 5, "hi?:");
    // case
    case (z) 128'd0: hits += 1; default: begin errs++; $display("FAIL case z"); end endcase
    case (hi) 128'd0: begin errs++; $display("FAIL case hi 0"); end default: hits += 1; endcase
    case (hi) z: begin errs++; $display("FAIL case hi z"); end hi: hits += 1; default: begin errs++; $display("FAIL case hi none"); end endcase
    casez (z) 128'd0: hits += 1; default: begin errs++; $display("FAIL casez"); end endcase
    unique0 case (z3) 384'd0: hits += 1; endcase
    // inside
    `CHK((z inside {0, 5}) === 1'b1, "z inside"); `CHK((hi inside {0, 5}) === 1'b0, "hi inside");
    // returns
    `CHK(retbool(z) === 1'b0, "ret z"); `CHK(retneg(hi) === 1'b0, "retneg hi");
    `CHK(retbool384(z3) === 1'b0, "ret z3"); `CHK(retneg(z) === 1'b1, "retneg");
    // loops / conditions
    hits = 0; for (int i = 0; z; i++) hits++; `CHK(hits == 0, "for z");
    hits = 0; do hits++; while (z); `CHK(hits == 1, "do-while z");
    hits = 0; if (z) hits = 1; else hits = 2; `CHK(hits == 2, "if z");
    // assert
    assert (!z) else begin errs++; $display("FAIL assert !z"); end
    assert (hi) else begin errs++; $display("FAIL assert hi"); end
    assert (z == 0) else begin errs++; $display("FAIL assert z==0"); end
    // wait and event controls
    fork begin wait (z); errs++; $display("FAIL wait(z) passed"); end join_none
    fork begin wait (!z); hits = 77; end join_none
    #1; `CHK(hits == 77, "wait !z");
    w2 = 0;
    fork begin @(w2); hits = 88; end join_none
    #1 w2 = 1; #1; `CHK(hits == 88, "@w2 change");
    w2 = 0; hits = 0;
    fork begin @(posedge w2[0]); hits = 99; end join_none
    #1 w2 = 128'd1; #1; `CHK(hits == 99, "posedge wide[0]");
    // wide bit selects / part selects as conditions
    `CHK(!hi[0] && hi[127], "bitsel");
    if (hi[126:0]) begin errs++; $display("FAIL part select"); end
    // enum/struct packed wide
    begin
      typedef struct packed { bit [127:0] a; bit [127:0] b; } s_t;
      s_t s = '0;
      `CHK(!s, "struct !s"); `CHK(!(s ? 1 : 0), "struct ?:"); s.b[100] = 1; `CHK(s ? 1 : 0, "struct nz");
    end
    if (errs == 0) $display("PASSED"); else $fatal(1, "%0d errors", errs);
  end
endmodule
