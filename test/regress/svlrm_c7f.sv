// Self-checking: packed unions, tagged unions, pattern matching.
// Expected values: IEEE 1800-2017 7.3 (unions), 7.3.2 (tagged unions), 12.6 (pattern matching),
// 21.2.1.7 (%p: only the presence of the tag and the member value is checked, not the layout)
`define CHK(g, e) if ((g) !== (e)) begin errs++; $error("check failed at line %0d", `__LINE__); end
module svlrm_c7f;
  typedef union { int i; real r; } u_t;
  typedef union packed { logic [7:0] b; struct packed { logic [3:0] h, l; } n; } pu_t;
  typedef union tagged { void Invalid; int Valid; real R; } vu_t;
  typedef union tagged { struct { int a; int b; } Pair; bit [3:0] Nib; } tu2_t;
  u_t u; pu_t pu; vu_t vu; tu2_t t2;
  int errs; int which; int got;
  function automatic bit has(string s, string sub);
    for (int i = 0; i + sub.len() <= s.len(); i++)
      if (s.substr(i, i + sub.len() - 1) == sub) return 1;
    return 0;
  endfunction
  initial begin
    string str;
    pu.b = 8'hab; `CHK(pu.n.h, 4'ha) `CHK(pu.n.l, 4'hb)
    pu.n.l = 4'h0; `CHK(pu.b, 8'ha0)
    pu = 8'h5a; `CHK(pu.n.h, 4'h5)
    vu = tagged Valid 5;
    which = 0; got = 0;
    case (vu) matches
      tagged Valid .x: begin which = 1; got = x; end
      tagged Invalid: which = 2;
      tagged R .r: which = 3;
    endcase
    `CHK(which, 1) `CHK(got, 5)
    vu = tagged Invalid;
    which = 0;
    case (vu) matches
      tagged Valid .x: which = 1;
      tagged Invalid: which = 2;
      default: which = 9;
    endcase
    `CHK(which, 2)
    vu = tagged Valid 3;
    which = 0;
    case (vu) matches
      tagged Invalid: which = 2;
      default: which = 9;
    endcase
    `CHK(which, 9)
    vu = tagged R 2.5;
    which = 0;
    if (vu matches tagged R .r) begin which = 1; if (r != 2.5) begin errs++; $error("r value"); end end
    `CHK(which, 1)
    t2 = tagged Pair '{3, 4};
    t2 = tagged Nib 4'h9;
    which = 0;
    if (t2 matches tagged Nib .n &&& n > 4) begin which = 1; `CHK(n, 4'h9) end
    `CHK(which, 1)
    which = 0;
    if (t2 matches tagged Nib .n &&& n > 10) which = 1;
    `CHK(which, 0)
    str = $sformatf("%p", vu);
    `CHK(has(str, "R"), 1) `CHK(has(str, "2.5"), 1)
    str = $sformatf("%p", t2);
    `CHK(has(str, "Nib"), 1) `CHK(has(str, "9"), 1)
    vu = tagged Valid 7; `CHK(vu.Valid, 7)
    if (errs == 0) $display("PASSED");
  end
endmodule
