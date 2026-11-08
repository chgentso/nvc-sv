// Expected values: IEEE 1800-2017 16.8 (local variables), 16.9.2 (repetition,
// ##[m:n], throughout), 16.9.3 ($rose, $stable, $past), 16.12.6 (|->, |=>),
// 16.12.14 (disable iff), 16.14.3 (cover property), 20.9 ($onehot0), 23.11
// (bind), 25 (interfaces).
// Reference model: both interfaces are sampled just before every clock edge
// (edge e at time 5+10e, e = 0..61; rst_n is low only before time 22, i.e. at
// edges 0 and 1, which disables attempts starting there).  Each property is
// evaluated procedurally on the samples:
//   ack     $rose(req) |-> ##[1:4] ack                        (fail at e+4)
//   spur    ack |-> $past(req,1) || $past(req,2)              (fail at e)
//   stable  (valid && !ready) |=> valid && $stable(data)      (fail at e+1)
//   onehot  $onehot0(gnt)                                     (fail at e)
//   burst   ($rose(valid), n=1) ##1 ((valid&&!last), n++)[*1:$] ##1 (valid&&last)
//             |-> n == 4   -> a burst must have exactly 4 beats before last
//   echo    (valid&&ready&&!last, d=data) |=> ##[0:3] (valid&&ready&&data==d+1)
//   covers (one count per succeeding attempt): req ##1 ack; req ##[3:4] ack;
//             (valid&&ready)[*3] ##1 last
// Failure counts and cover counts must match exactly; pass counts are checked
// from below with non-vacuous successes only (vacuous success handling and
// the unspecified order of messages are not relied on).
// SVA protocol checkers bound to interface instances: handshake and burst
// rules with local variables, sampled value functions, sequence repetition,
// cover properties and disable iff
interface hs_if #(parameter int W = 8) (input logic clk, input logic rst_n);
  logic         req, ack;
  logic         valid, ready, last;
  logic [W-1:0] data;
  logic [1:0]   gnt;
endinterface

interface hs_checker #(parameter int W = 8, parameter string NAME = "hs")
  (input logic clk, input logic rst_n, input logic req, input logic ack,
   input logic valid, input logic ready, input logic last,
   input logic [W-1:0] data, input logic [1:0] gnt);

  int nfail[string], npass[string], ncover[string];
  initial begin
    automatic string nm[] = '{"ack", "spur", "stable", "onehot", "burst", "echo"};
    foreach (nm[i]) begin nfail[nm[i]] = 0; npass[nm[i]] = 0; end
    ncover["fast"] = 0; ncover["slow"] = 0; ncover["burst3"] = 0;
  end

  sequence s_req_then_ack(int lo, int hi);
    req ##[lo:hi] ack;
  endsequence

  property p_ack_after_req;
    @(posedge clk) disable iff (!rst_n) $rose(req) |-> ##[1:4] ack;
  endproperty
  property p_no_spurious_ack;
    @(posedge clk) disable iff (!rst_n) ack |-> $past(req, 1) || $past(req, 2);
  endproperty
  property p_data_stable;
    @(posedge clk) disable iff (!rst_n) (valid && !ready) |=> valid && $stable(data);
  endproperty
  property p_gnt_onehot;
    @(posedge clk) disable iff (!rst_n) $onehot0(gnt);
  endproperty
  property p_burst_len;
    int n;
    @(posedge clk) disable iff (!rst_n)
      ($rose(valid), n = 1) ##1 ((valid && !last), n++)[*1:$] ##1 (valid && last) |-> (n == 4);
  endproperty
  property p_echo;
    logic [W-1:0] d;
    @(posedge clk) disable iff (!rst_n)
      (valid && ready && !last, d = data) |=> ##[0:3] (valid && ready && data == d + 1'b1);
  endproperty
  property p_req_hold;
    @(posedge clk) disable iff (!rst_n) (req && !ack) throughout (##[1:3] ack) |-> 1;
  endproperty

  a_ack:    assert property (p_ack_after_req)
              npass["ack"]++; else nfail["ack"]++;
  a_spur:   assert property (p_no_spurious_ack)
              npass["spur"]++; else nfail["spur"]++;
  a_stable: assert property (p_data_stable)
              npass["stable"]++; else nfail["stable"]++;
  a_onehot: assert property (p_gnt_onehot)
              npass["onehot"]++; else nfail["onehot"]++;
  a_burst:  assert property (p_burst_len)
              npass["burst"]++; else nfail["burst"]++;
  a_echo:   assert property (p_echo)
              npass["echo"]++; else nfail["echo"]++;

  c_fast: cover property (@(posedge clk) disable iff (!rst_n) s_req_then_ack(1, 1)) ncover["fast"]++;
  c_slow: cover property (@(posedge clk) disable iff (!rst_n) s_req_then_ack(3, 4)) ncover["slow"]++;
  c_bst:  cover property (@(posedge clk) disable iff (!rst_n) valid && ready [*3] ##1 last) ncover["burst3"]++;

endinterface

module dut_gen(hs_if h, input int seed, input bit inject);
  int cyc;
  logic [7:0] cnt;
  always @(posedge h.clk) begin
    if (!h.rst_n) begin
      h.req <= 0; h.ack <= 0; h.valid <= 0; h.ready <= 1; h.last <= 0; h.data <= 0; h.gnt <= 0; cyc <= 0; cnt <= seed;
    end
    else begin
      cyc <= cyc + 1;
      // request/ack with a latency that varies with the cycle number
      h.req <= (cyc % 9) < 3;
      h.ack <= ((cyc % 9) == 2 + (cyc / 9) % 3) || (inject && cyc == 20);
      // a burst of five beats every 12 cycles
      h.valid <= (cyc % 12) < 5;
      h.last  <= (cyc % 12) == 4 || (inject && cyc == 30);
      h.ready <= !((cyc % 12) == 2 && inject);
      if ((cyc % 12) < 5 && h.ready) cnt <= cnt + 1;
      h.data <= (inject && cyc == 14) ? 8'hee : cnt;
      h.gnt <= (inject && cyc == 40) ? 2'b11 : (cyc % 3 == 0 ? 2'b01 : cyc % 3 == 1 ? 2'b10 : 2'b00);
    end
  end
endmodule

module svsyn6;
  logic clk = 0, rst_n = 0;
  always #5 clk = ~clk;
  hs_if #(.W(8)) good(clk, rst_n);
  hs_if #(.W(8)) bad(clk, rst_n);
  dut_gen g0(good, 1, 1'b0);
  dut_gen g1(bad, 5, 1'b1);
  bind hs_if hs_checker #(.W(W), .NAME("chk")) u_chk(.clk(clk), .rst_n(rst_n), .req(req), .ack(ack), .valid(valid), .ready(ready), .last(last), .data(data), .gnt(gnt));
  localparam int NE = 62;
  logic H_req[2][NE], H_ack[2][NE], H_valid[2][NE], H_ready[2][NE], H_last[2][NE];
  logic [7:0] H_data[2][NE]; logic [1:0] H_gnt[2][NE];
  int he;
  initial begin
    #4;
    for (int e = 0; e < NE; e++) begin
      #0;
      H_req[0][e] = good.req; H_ack[0][e] = good.ack; H_valid[0][e] = good.valid; H_ready[0][e] = good.ready;
      H_last[0][e] = good.last; H_data[0][e] = good.data; H_gnt[0][e] = good.gnt;
      H_req[1][e] = bad.req; H_ack[1][e] = bad.ack; H_valid[1][e] = bad.valid; H_ready[1][e] = bad.ready;
      H_last[1][e] = bad.last; H_data[1][e] = bad.data; H_gnt[1][e] = bad.gnt;
      he++;
      #10;
    end
  end
  initial begin
    #22 rst_n = 1;
    #600;
    check();
    $finish;
  end
  function automatic bit T(logic x); return x === 1'b1; endfunction
  function automatic bit dis(int s); return s <= 1; endfunction
  function automatic int gf(int w, string k);
    if (w == 0) return good.u_chk.nfail.exists(k) ? good.u_chk.nfail[k] : 0;
    else return bad.u_chk.nfail.exists(k) ? bad.u_chk.nfail[k] : 0;
  endfunction
  function automatic int gp(int w, string k);
    if (w == 0) return good.u_chk.npass.exists(k) ? good.u_chk.npass[k] : 0;
    else return bad.u_chk.npass.exists(k) ? bad.u_chk.npass[k] : 0;
  endfunction
  function automatic int gc(int w, string k);
    if (w == 0) return good.u_chk.ncover.exists(k) ? good.u_chk.ncover[k] : 0;
    else return bad.u_chk.ncover.exists(k) ? bad.u_chk.ncover[k] : 0;
  endfunction
  int errs;
  int tot_fail[2];
  task automatic ck(int w, string what, int got, int exp);
    if (got != exp) begin errs++; $error("%s %s: got %0d expected %0d", w == 0 ? "good" : "bad", what, got, exp); end
  endtask
  task automatic check();
    if (he != NE) begin errs++; $error("recorded %0d edges, expected %0d", he, NE); end
    for (int w = 0; w < 2; w++) begin
      int f_ack, f_spur, f_stable, f_onehot, f_burst, f_echo;
      int p_ack, p_spur, p_stable, p_onehot, p_burst, p_echo;
      int c_fast, c_slow, c_bst;
      for (int e = 0; e < NE; e++) if (!dis(e)) begin
        // ack
        if (e >= 1 && T(H_req[w][e]) && H_req[w][e-1] === 1'b0 && e + 4 < NE) begin
          if (T(H_ack[w][e+1]) || T(H_ack[w][e+2]) || T(H_ack[w][e+3]) || T(H_ack[w][e+4])) p_ack++; else f_ack++;
        end
        // spur
        if (e >= 2 && T(H_ack[w][e])) begin
          if (T(H_req[w][e-1]) || T(H_req[w][e-2])) p_spur++; else f_spur++;
        end
        // stable
        if (e + 1 < NE && T(H_valid[w][e]) && !T(H_ready[w][e])) begin
          if (T(H_valid[w][e+1]) && H_data[w][e+1] === H_data[w][e]) p_stable++; else f_stable++;
        end
        // onehot0
        if ($countones(H_gnt[w][e]) <= 1) p_onehot++; else f_onehot++;
        // burst
        if (e >= 1 && T(H_valid[w][e]) && H_valid[w][e-1] === 1'b0) begin
          int k = 0, j = e + 1;
          while (j < NE && T(H_valid[w][j]) && !T(H_last[w][j])) begin k++; j++; end
          if (k >= 1 && j < NE && T(H_valid[w][j]) && T(H_last[w][j])) begin
            if (1 + k == 4) p_burst++; else f_burst++;
          end
        end
        // echo
        if (e + 4 < NE && T(H_valid[w][e]) && T(H_ready[w][e]) && !T(H_last[w][e])) begin
          logic [7:0] dn = H_data[w][e] + 8'd1;
          bit m = 0;
          for (int j = 1; j <= 4; j++) if (T(H_valid[w][e+j]) && T(H_ready[w][e+j]) && H_data[w][e+j] == dn) m = 1;
          if (m) p_echo++; else f_echo++;
        end
        // covers
        if (e + 1 < NE && T(H_req[w][e]) && T(H_ack[w][e+1])) c_fast++;
        if (e + 4 < NE && T(H_req[w][e]) && (T(H_ack[w][e+3]) || T(H_ack[w][e+4]))) c_slow++;
        if (e + 3 < NE && T(H_valid[w][e] && H_ready[w][e]) && T(H_valid[w][e+1] && H_ready[w][e+1])
            && T(H_valid[w][e+2] && H_ready[w][e+2]) && T(H_last[w][e+3])) c_bst++;
      end
      ck(w, "fail ack", gf(w, "ack"), f_ack);       ck(w, "fail spur", gf(w, "spur"), f_spur);
      ck(w, "fail stable", gf(w, "stable"), f_stable); ck(w, "fail onehot", gf(w, "onehot"), f_onehot);
      ck(w, "fail burst", gf(w, "burst"), f_burst); ck(w, "fail echo", gf(w, "echo"), f_echo);
      ck(w, "cover fast", gc(w, "fast"), c_fast); ck(w, "cover slow", gc(w, "slow"), c_slow);
      ck(w, "cover burst3", gc(w, "burst3"), c_bst);
      ck(w, "pass onehot", gp(w, "onehot"), p_onehot);
      if (gp(w, "ack") < p_ack) begin errs++; $error("w%0d pass ack %0d < %0d", w, gp(w, "ack"), p_ack); end
      if (gp(w, "spur") < p_spur) begin errs++; $error("w%0d pass spur %0d < %0d", w, gp(w, "spur"), p_spur); end
      if (gp(w, "stable") < p_stable) begin errs++; $error("w%0d pass stable %0d < %0d", w, gp(w, "stable"), p_stable); end
      if (gp(w, "burst") < p_burst) begin errs++; $error("w%0d pass burst %0d < %0d", w, gp(w, "burst"), p_burst); end
      if (gp(w, "echo") < p_echo) begin errs++; $error("w%0d pass echo %0d < %0d", w, gp(w, "echo"), p_echo); end
      tot_fail[w] = f_ack + f_spur + f_stable + f_onehot + f_burst + f_echo;
      if (p_ack == 0 || p_burst == 0 || p_echo == 0 || c_fast + c_slow + c_bst == 0) begin
        errs++; $error("model sanity (w%0d): ack %0d burst %0d echo %0d covers %0d", w, p_ack, p_burst, p_echo, c_fast + c_slow + c_bst);
      end
    end
    // the clean interface must not violate any rule?  Only what the model says; the
    // injected faults on the second interface must show up in the model as failures.
    if (tot_fail[1] == 0) begin errs++; $error("model sanity: injected faults produced no failure"); end
    if (errs == 0) $display("PASSED");
  endtask
endmodule
