// always_comb state machine over an enum, checked with name()
// Expected values: IEEE 1800-2017 9.2.2.2 (always_comb executes once at time zero and
// again when a variable read in it changes), 6.19.5.6 (name()), 12.5 (case), 11.4.13 (inside)
`timescale 1ns/1ns
module svcombenum1;
  typedef enum logic [1:0] {IdleSt, PingSt, WaitSt} st_e;
  st_e state_q = IdleSt, state_d;
  logic go = 0, done = 0;
  int errs = 0;
  int bt[$];
  st_e bs[$];
  always_comb begin
    state_d = state_q;
    case (state_q)
      IdleSt: if (go) state_d = PingSt;
      PingSt: state_d = WaitSt;
      WaitSt: if (done) state_d = IdleSt;
      default: state_d = IdleSt;
    endcase
  end
  // the block reads only state_q, so it runs at time 0 and whenever state_q changes
  always_comb if (state_q == WaitSt || (state_q inside {PingSt})) begin
    bt.push_back(int'($time));
    bs.push_back(state_q);
  end
  function automatic void chks(string m, string got, string exp);
    if (got != exp) begin $error("%s: got '%s' expected '%s'", m, got, exp); errs++; end
  endfunction
  initial begin
    #1 go = 1; #1 chks("t2", state_d.name(), "PingSt");
    state_q = state_d; #1 chks("t3", state_d.name(), "WaitSt");
    state_q = state_d; #1 done = 1; #1 chks("t5", state_d.name(), "IdleSt");
    #1;
    if (bt.size() != 2) begin $error("expected 2 busy events, got %0d", bt.size()); errs++; end
    else begin
      if (bt[0] != 2 || bs[0] != PingSt) begin $error("busy event 0 wrong"); errs++; end
      if (bt[1] != 3 || bs[1] != WaitSt) begin $error("busy event 1 wrong"); errs++; end
    end
    if (errs == 0) $display("PASSED");
  end
endmodule
