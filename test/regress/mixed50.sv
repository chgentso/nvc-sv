// SystemVerilog hierarchical references into VHDL record/array signals,
// ports and generics: field selects, bit/part selects of record fields,
// array-of-record element selects with constant and variable indices, change
// events on fields, edge events on a bit of a field, wires driven from record
// fields and always @*.
// Expected values are derived from the VHDL declarations and the assignment
// schedule in mixed50.vhd (IEEE 1076 signal semantics, indices as declared
// in VHDL, vectors read left to right) and from IEEE 1800-2017 9.4 (event
// control, @*), 11.5.1 (bit/part selects), 23.6 (hierarchical references).
// Cross-language mapping is not part of the 1800 LRM; times are in ns
// (the VHDL delays are `wait for ... ns`), set explicitly below.
// Change events at time zero are not checked.
timeunit 1ns; timeprecision 100ps;
module mixed50;
  mixed50_e u();
  integer i, j;
  integer errors = 0;
  wire w_valid = u.o.valid;
  wire [3:0] w_word = u.o.words[1];

  task automatic chk(string what, string got, string exp);
    if (got !== exp) begin $error("%s: got '%s' expected '%s'", what, got, exp); errors++; end
  endtask

  // event log: "<time*10> <value>" strings, checked at the end
  string ev_valid[$], ev_h[$], ev_vec[$], ev_a[$], ev_cnt[$], ev_edge[$], ev_star[$];
  always @(u.o.valid) if ($realtime > 0) ev_valid.push_back($sformatf("%0d %b", $realtime, u.o.valid));
  always @(u.o.f.h) if ($realtime > 0) ev_h.push_back($sformatf("%0d %b", $realtime, u.o.f.h));
  always @(u.o.vec) if ($realtime > 0) ev_vec.push_back($sformatf("%0d %b", $realtime, u.o.vec));
  always @(u.o.arr[2].a) if ($realtime > 0) ev_a.push_back($sformatf("%0d %0d", $realtime, u.o.arr[2].a));
  always @(u.o.cnt) if ($realtime > 0) ev_cnt.push_back($sformatf("%0d %0d", $realtime, u.o.cnt));
  always @(posedge u.oa[3].f.h[0]) ev_edge.push_back($sformatf("%0d", $realtime));
  always @* if (u.o.words[1] == 4'hF) ev_star.push_back($sformatf("%0d %b", $realtime, u.o.words[1]));

  function automatic void chkq(string what, string q[$], string exp[$]);
    if (q.size() != exp.size()) begin
      $error("%s: %0d events expected %0d", what, q.size(), exp.size()); errors++;
      foreach (q[k]) $display("  %s", q[k]);
    end else
      foreach (q[k]) if (q[k] != exp[k]) begin
        $error("%s event %0d: got '%s' expected '%s'", what, k, q[k], exp[k]); errors++;
      end
  endfunction

  initial begin
    j = 3;
    #1;
    chk("a", $sformatf("%b %b %b %b %0d", u.o.valid, u.o.f.g, u.o.f.h, u.o.vec, u.o.cnt),
        "0 1 101 10100101 42");
    chk("b", $sformatf("%b %b %b", u.o.f.h[0], u.o.f.h[1:2], u.o.vec[7:4]), "1 01 1010");
    chk("c", $sformatf("%b %b", u.o.vec[3], u.o.vec[2 +: 3]), "0 001");
    // arr is (3 downto 1): arr(1) = (3, x"3"), arr(2) = (-2, x"2"), arr(3) = (1, x"1")
    for (i = 1; i <= 3; i = i + 1)
      chk($sformatf("d%0d", i), $sformatf("%0d %b", u.o.arr[i].a, u.o.arr[i].b),
          i == 1 ? "3 0011" : i == 2 ? "-2 0010" : "1 0001");
    for (i = 2; i <= 4; i = i + 1)
      chk($sformatf("e%0d", i), $sformatf("%b %b %b %0d %b", u.oa[i].valid, u.oa[i].f.h,
                                          u.oa[i].vec, u.oa[i].cnt, u.oa[i].arr[1].b),
          "1 010 00001111 7 0000");
    chk("f", $sformatf("%b %b", u.s.pr.valid, u.s.pr.f.h), "0 101");
    chk("g", $sformatf("%b %b", u.o.arr[2].b[5], u.o.arr[3].b[6:5]), "1 00");
    chk("h", $sformatf("%b %b %b", u.o.words[1], u.o.words[0], u.o.words[1][3:2]), "0101 0110 01");
    chk("i", $sformatf("%0d %0d", u.s.width, u.s.width + 1), "9 10");
    chk("q", $sformatf("%b %b %b", u.ww[1], u.ww[0], u.ww[1][3:2]), "1010 0011 10");
    chk("j", $sformatf("%b", w_valid), "0");
    #4.5;     // 5.5: valid and g updated at 5
    chk("k", $sformatf("%b %b %b %b %0d", u.o.valid, u.o.f.g, u.o.f.h, u.o.vec, u.o.cnt),
        "1 z 101 10100101 42");
    chk("k wire", $sformatf("%b", w_valid), "1");
    #12;      // 17.5: all updates done
    chk("l", $sformatf("%0d %b %b", u.o.arr[2].a, u.o.arr[2].b, w_word), "4 1010 1111");
    chk("m", $sformatf("%b %b %b", u.oa[3].f.h, u.oa[3].arr[1].b, u.oa[4].vec), "111 1111 10000001");
    j = 3;
    chk("o", $sformatf("%b %0d", u.oa[j].vec, u.oa[j].cnt + u.o.cnt), "00001111 -2");
    chkq("valid", ev_valid, '{"5 1"});
    chkq("h", ev_h, '{"6 110"});
    chkq("vec", ev_vec, '{"7 00111100", "12 0011110x"});
    chkq("a", ev_a, '{"9 4"});
    chkq("cnt", ev_cnt, '{"10 -9"});
    chkq("edge", ev_edge, '{"11"});
    chkq("star", ev_star, '{"8 1111"});
    if (errors == 0) $display("PASSED");
  end
endmodule
