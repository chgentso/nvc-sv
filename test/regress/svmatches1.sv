// Self-checking: tagged unions with case/if ... matches, guards (&&&), struct patterns, wildcards.
// Expected values: IEEE 1800-2017 12.6 (pattern matching: tagged, constant, variable-binding and wildcard
// patterns, assignment-pattern patterns for structs, && guards, first matching case item wins), 7.3.2
// (tagged unions), 21.3.3 ($sformatf)
`define CHK(g, e) if ((g) !== (e)) begin errs++; $error("check failed at line %0d", `__LINE__); end
module svmatches1;
  typedef struct { bit [3:0] lo, hi; } pair_t;
  typedef union tagged {
    void Invalid;
    int Valid;
    pair_t Pair;
    struct { bit [7:0] a; bit [7:0] b; } Wide;
  } u_t;

  typedef union tagged packed {
    bit [3:0] Small;
    struct packed { bit [3:0] x; bit [3:0] y; } Big;
  } p_t;

  u_t u;
  p_t p;
  int r; int errs;

  function automatic string show(u_t v);
    case (v) matches
      tagged Invalid : return "invalid";
      tagged Valid .n &&& (n < 0) : return $sformatf("negative %0d", n);
      tagged Valid .n : return $sformatf("valid %0d", n);
      tagged Pair '{.l, 0} : return $sformatf("pair %0d,0", l);
      tagged Pair '{.l, .h} : return $sformatf("pair %0d,%0d", l, h);
      tagged Wide '{a: 8'h55, b: .b} : return $sformatf("wide 55,%0h", b);
      default : return "other";
    endcase
  endfunction

  initial begin
    string s;
    u = tagged Invalid;
    `CHK(show(u), "invalid")
    u = tagged Valid (-5);
    `CHK(show(u), "negative -5")
    u = tagged Valid (12);
    `CHK(show(u), "valid 12")
    u = tagged Pair '{3, 0};
    `CHK(show(u), "pair 3,0")
    u = tagged Pair '{3, 9};
    `CHK(show(u), "pair 3,9")
    u = tagged Wide '{8'h55, 8'hab};
    `CHK(show(u), "wide 55,ab")
    u = tagged Wide '{8'h56, 8'hab};
    `CHK(show(u), "other")

    u = tagged Valid (7);
    s = "";
    if (u matches tagged Valid .v) s = $sformatf("if valid %0d", v); else s = "if not valid";
    `CHK(s, "if valid 7")
    if (u matches tagged Valid .v &&& v > 100) s = "big";
    else if (u matches tagged Invalid) s = "invalid";
    else s = "small";
    `CHK(s, "small")
    s = ""; if (u matches tagged Valid 7) s = "seven"; `CHK(s, "seven")
    s = ""; if (u matches tagged Valid 8) s = "eight"; `CHK(s, "")
    s = ""; if (u matches tagged Valid .*) s = "any valid"; `CHK(s, "any valid")
    if (u matches tagged Pair .*) s = "pair"; else s = "not pair";
    `CHK(s, "not pair")

    p = tagged Small (4'h9);
    s = "";
    if (p matches tagged Small .s2) s = $sformatf("small %0h", s2);
    `CHK(s, "small 9")
    p = tagged Big '{4'h3, 4'hc};
    s = "";
    case (p) matches
      tagged Small .s2 : s = $sformatf("small %0h", s2);
      tagged Big '{.x, .y} : s = $sformatf("big %0h %0h", x, y);
    endcase
    `CHK(s, "big 3 c")
    case (p) matches
      tagged Big '{4'h3, 4'hc} : s = "exact";
      default : s = "none";
    endcase
    `CHK(s, "exact")
    if (errs == 0) $display("PASSED");
  end
endmodule
