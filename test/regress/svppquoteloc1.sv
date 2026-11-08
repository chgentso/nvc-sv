// Stringification with `" and macro expansion inside the stringified argument, token pasting of
// a default macro argument, and a dist constraint passed through macros.
// Expected values: IEEE 1800-2017 22.5.1 (`" expands embedded macros, ``, default args), 18.5.4 (dist),
// 18.12 (std::randomize with)
string got = "", fatalmsg = "";
`define S(T_) got = `"T_`";
`define IDX(a_) [0 : a_-1]
`define Y 5
`define DV_CHECK(T_, SEV_=error) \
  begin if (T_) ; else begin `dv_``SEV_($sformatf("Check failed (%s)", `"T_`")) end end
`define dv_fatal(MSG_) fatalmsg = MSG_;
`define DIST(V_, W_ = 2) V_ dist {6 := W_ * 3, [0 : `Y - 1] := 1}
module svppquoteloc1;
  int val;
  int errs = 0;
  initial begin
    `S(x [0 : `IDX(3)-1] := 4)
    if (got != "x [0 : [0 : 3-1]-1] := 4") begin errs++; $error("S1 got '%s'", got); end
    `S(a `Y b)
    if (got != "a 5 b") begin errs++; $error("S2 got '%s'", got); end
    `DV_CHECK(std::randomize(val) with {`DIST(val, 1);}, fatal)
    if (fatalmsg != "") begin errs++; $error("randomize check failed: %s", fatalmsg); end
    `DV_CHECK(val == 99, fatal)
    if (fatalmsg != "Check failed (val == 99)") begin errs++; $error("fatalmsg '%s'", fatalmsg); end
    if (!(val >= 0 && val <= 6)) begin errs++; $error("val %0d outside dist range", val); end
    if (errs == 0) $display("PASSED");
  end
endmodule
