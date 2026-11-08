// Self-checking: unique over a dynamic array of enums, "inside" with arrays of another object,
// and overriding a named constraint in a derived class.
// Expected values: IEEE 1800-2017 18.5.5 (unique), 18.5.3 (inside with array), 18.5.2 (constraint blocks
// with the same name in a derived class override the base one), 18.4 (rand dynamic arrays).
typedef enum bit [4:0] {ZERO, RA, SP, GP, TP, T0, T1, T2, S0, S1, A0, A1, A2, A3, A4, A5, A6, A7, S2, S3, S4, S5, S6, S7, S8, S9, S10, S11, T3, T4, T5, T6} reg_t;
class cfg_c;
  reg_t reserved_regs[];
endclass
class mp;
  cfg_c cfg;
  rand int unsigned num;
  rand int unsigned dp[];
  rand reg_t rs1[];
  int max_dp = 2;
  constraint default_c {
    foreach (dp[i]) dp[i] < max_dp;
    dp.size() == num;
    rs1.size() == num;
    unique {rs1};
    foreach (rs1[i]) { !(rs1[i] inside {cfg.reserved_regs, ZERO}); }
  }
  constraint page_c {
    solve num before dp;
    num inside {[1:max_dp]};
    unique {dp};
  }
  constraint reasonable_c { num inside {[2:8]}; }
endclass
class mp2 extends mp;
  constraint page_c {
    num inside {[2:5]};
    foreach (dp[i]) { if (i > 0) dp[i] == dp[i-1]; }
  }
endclass
module svcrvuniqdyn1;
  initial begin
    automatic mp2 o = new;
    automatic int bad = 0, fail = 0;
    o.cfg = new;
    o.cfg.reserved_regs = new[3];
    o.cfg.reserved_regs[0] = S0; o.cfg.reserved_regs[1] = A6; o.cfg.reserved_regs[2] = T6;
    repeat (300) begin
      if (!o.randomize()) fail++;
      else begin
        foreach (o.rs1[i]) foreach (o.rs1[j]) if (i != j && o.rs1[i] == o.rs1[j]) bad++;
        foreach (o.rs1[i]) if (o.rs1[i] inside {S0, A6, T6, ZERO}) bad++;
        if (o.rs1.size() != o.num || o.dp.size() != o.num || o.num < 2 || o.num > 5) bad++;
        foreach (o.dp[i]) if (o.dp[i] >= o.max_dp || o.dp[i] != o.dp[0]) bad++;
      end
    end
    if (bad != 0 || fail != 0) $error("bad %0d fail %0d", bad, fail);
    else $display("PASSED");
  end
endmodule
