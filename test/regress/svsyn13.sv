// Expected values: IEEE 1800-2017 16.9.3 (sampled values), 16.12.6, 16.12 (SVA
// with sequences/properties/cover), 7.8 / 7.10 / 7.12 (associative arrays of
// queues, queue methods, array locator and reduction methods, sort with),
// 6.19 (enum name()), 12.7.3 (foreach).
// Self-checking, with no expected output file: all 24 driven requests are
// accepted exactly once and answered (sent == done == 24), every response
// matches the reference memory model (mismatched == unexpected == 0), the
// three opcodes occur 8 times each, exactly one error response (address ff,
// request 13), every latency is at least the per-request DUT delay 2+id%3,
// sort/find/sum agree with a manual loop, and the assertion failures and
// cover hits equal a procedural evaluation of the same properties on values
// sampled just before every clock edge.
// The testbench samples the interface at the end of the previous time step
// (preponed semantics) instead of reading it at the edge, because the DUT
// updates req_r in the active region of the edge (a race otherwise).
// Protocol scoreboard: queues of structs, associative arrays of queues,
// concurrent assertions with sequences/properties, cover directives
package sb_pkg;
  typedef enum logic [1:0] {OP_RD, OP_WR, OP_RMW} op_e;
  typedef struct packed { op_e op; logic [3:0] id; logic [7:0] addr; logic [15:0] data; } req_t;
  typedef struct { logic [3:0] id; logic [15:0] data; bit err; int latency; } rsp_t;
  typedef struct { req_t req; time t_issue; } pending_t;
endpackage

interface chan_if (input logic clk, input logic rst);
  logic        req_v, req_r;
  sb_pkg::req_t req;
  logic        rsp_v;
  logic [3:0]  rsp_id;
  logic [15:0] rsp_data;
  logic        rsp_err;
endinterface

module dut_model (chan_if c);
  import sb_pkg::*;
  logic [15:0] mem [256];
  req_t q[$];
  int delay[$];
  int cyc = 0;
  assign c.req_r = (q.size() < 3);
  initial for (int i = 0; i < 256; i++) mem[i] = 16'h1000 ^ (i * 37);
  always @(posedge c.clk) begin
    c.rsp_v <= 0;
    cyc++;
    if (c.rst) begin q.delete(); delay.delete(); end
    else begin
      if (c.req_v && c.req_r) begin q.push_back(c.req); delay.push_back(2 + c.req.id % 3); end
      foreach (delay[i]) if (delay[i] > 0) delay[i]--;
      if (q.size() > 0 && delay[0] == 0) begin
        automatic req_t r = q.pop_front(); void'(delay.pop_front());
        c.rsp_v <= 1; c.rsp_id <= r.id; c.rsp_err <= (r.addr == 8'hff);
        case (r.op)
          OP_RD: c.rsp_data <= mem[r.addr];
          OP_WR: begin mem[r.addr] <= r.data; c.rsp_data <= 16'h0; end
          OP_RMW: begin c.rsp_data <= mem[r.addr]; mem[r.addr] <= mem[r.addr] + r.data; end
        endcase
      end
    end
  end
endmodule

module svsyn13;
  import sb_pkg::*;
  logic clk = 0, rst = 1;
  always #5 clk = ~clk;
  chan_if c (clk, rst);
  dut_model d (c);

  pending_t outstanding[logic [3:0]][$];
  rsp_t     done[$];
  req_t     sent[$];
  logic [15:0] ref_mem [256];
  int matched, mismatched, unexpected;
  int op_count[op_e];
  int max_out;
  int n_out;
  int f_stable, f_rsp, f_idle, cv_b2b, cv_err;
  // sampled copies (value at the end of the time step before each clock edge)
  logic S_rst, S_req_v, S_req_r, S_rsp_v, S_rsp_err;
  req_t S_req; logic [3:0] S_rsp_id; logic [15:0] S_rsp_data;
  int S_nout;
  localparam int NE = 80;
  logic H_hs[NE], H_rv[NE], H_rerr[NE], H_rst[NE], H_vr[NE];
  req_t H_req[NE];
  int H_nout[NE];
  int he;
  initial begin
    #4;
    forever begin
      #0;
      S_rst = rst; S_req_v = c.req_v; S_req_r = c.req_r; S_req = c.req; S_rsp_v = c.rsp_v;
      S_rsp_id = c.rsp_id; S_rsp_data = c.rsp_data; S_rsp_err = c.rsp_err; S_nout = n_out;
      if (he < NE) begin
        H_hs[he] = S_req_v && S_req_r; H_vr[he] = S_req_v && !S_req_r; H_rv[he] = S_rsp_v;
        H_rerr[he] = S_rsp_v && S_rsp_err; H_rst[he] = rst; H_req[he] = S_req; H_nout[he] = S_nout; he++;
      end
      #10;
    end
  end

  initial for (int i = 0; i < 256; i++) ref_mem[i] = 16'h1000 ^ (i * 37);

  // driver: a fixed pattern of requests
  function automatic req_t make(int n);
    req_t r;
    r.op = op_e'(n % 3);
    r.id = n[3:0];
    r.addr = (n * 29 + 7) % 251;
    if (n == 13) r.addr = 8'hff;
    r.data = 16'h0100 * (n % 5) + n;
    return r;
  endfunction

  initial begin
    repeat (3) @(posedge clk);
    rst <= 0;
    for (int n = 0; n < 24; n++) begin
      @(posedge clk);
      c.req_v <= 1; c.req <= make(n);
      @(posedge clk iff S_req_r);
      c.req_v <= 0;
      if (n % 4 == 3) repeat (3) @(posedge clk);
    end
    repeat (12) @(posedge clk);
    begin
      automatic int errs = 0;
      automatic int e_stable, e_rsp, e_idle, e_b2b, e_err;
      automatic int sumerr = 0, mn = 1 << 30, mx = -1, nlate = 0, lat_sum = 0;
      automatic real avg = 0;
      if (sent.size() != 24 || done.size() != 24) begin errs++; $error("sent=%0d done=%0d, expected 24/24", sent.size(), done.size()); end
      if (matched != 24 || mismatched != 0 || unexpected != 0) begin errs++; $error("matched=%0d mismatched=%0d unexpected=%0d", matched, mismatched, unexpected); end
      if (n_out != 0) begin errs++; $error("n_out=%0d at end", n_out); end
      if (max_out < 1 || max_out > 4) begin errs++; $error("max_out=%0d outside 1..4", max_out); end
      foreach (op_count[o]) if (op_count[o] != 8) begin errs++; $error("op %s: %0d, expected 8", o.name(), op_count[o]); end
      if (op_count.num() != 3) begin errs++; $error("%0d distinct ops", op_count.num()); end
      foreach (done[i]) begin
        sumerr += done[i].err; lat_sum += done[i].latency;
        if (done[i].latency < 2 + done[i].id % 3) begin errs++; $error("response %0d id %0d latency %0d below DUT delay", i, done[i].id, done[i].latency); end
        if (done[i].latency < mn) mn = done[i].latency;
        if (done[i].latency > mx) mx = done[i].latency;
        if (done[i].latency > 3) nlate++;
      end
      avg = real'(lat_sum) / done.size();
      if (sumerr != 1 || done.sum() with (int'(item.err)) != 1) begin errs++; $error("error responses %0d, expected 1", sumerr); end
      done.sort(x) with (x.latency);
      if (done[0].latency != mn || done[$].latency != mx) begin errs++; $error("sort: min/max %0d/%0d, manual %0d/%0d", done[0].latency, done[$].latency, mn, mx); end
      for (int i = 1; i < done.size(); i++) if (done[i-1].latency > done[i].latency) begin errs++; $error("not sorted at %0d", i); break; end
      begin
        automatic rsp_t late[$] = done.find(x) with (x.latency > 3);
        if (late.size() != nlate) begin errs++; $error("find: %0d late, manual %0d", late.size(), nlate); end
      end
      if (avg < mn || avg > mx) begin errs++; $error("avg latency %f outside %0d..%0d", avg, mn, mx); end
      // reference evaluation of the assertions on sampled values
      if (he != NE) begin errs++; $error("recorded %0d samples", he); end
      for (int e = 0; e < NE; e++) begin
        if (e + 1 < NE && H_hs[e] && H_hs[e+1] && !H_rst[e] && !H_rst[e+1]) e_b2b++;
        if (H_rerr[e]) e_err++;
        if (!H_rst[e] && H_rv[e] && !(H_nout[e] > 0)) e_idle++;
        // a_req_stable: (req_v && !req_r) |=> req_v && $stable(req)
        if (e + 1 < NE && !H_rst[e] && H_vr[e]) begin
          if (!(H_hs[e+1] || H_vr[e+1]) || H_req[e+1] !== H_req[e]) e_stable++;
        end
        // a_rsp: hs |-> ##[2:8] rsp_v
        if (e + 8 < NE && !H_rst[e] && H_hs[e]) begin
          automatic bit any = 0;
          for (int j = 2; j <= 8; j++) any |= H_rv[e+j];
          if (!any) e_rsp++;
        end
      end
      if (f_stable != e_stable) begin errs++; $error("a_req_stable: %0d failures, model %0d", f_stable, e_stable); end
      if (f_rsp != e_rsp) begin errs++; $error("a_rsp: %0d failures, model %0d", f_rsp, e_rsp); end
      if (f_idle != e_idle) begin errs++; $error("a_idle: %0d failures, model %0d", f_idle, e_idle); end
      if (cv_b2b != e_b2b) begin errs++; $error("back-to-back covers %0d, model %0d", cv_b2b, e_b2b); end
      if (cv_err != e_err) begin errs++; $error("error-response covers %0d, model %0d", cv_err, e_err); end
      if (e_err != 1) begin errs++; $error("model sanity: err covers %0d", e_err); end
      if (errs == 0) $display("PASSED");
    end
    $finish;
  end

  // monitor requests
  always @(posedge clk) if (!S_rst && S_req_v && S_req_r) begin
    automatic pending_t p = '{req: S_req, t_issue: $time};
    outstanding[S_req.id].push_back(p);
    sent.push_back(S_req);
    n_out++;
    if (!op_count.exists(S_req.op)) op_count[S_req.op] = 0;
    op_count[S_req.op]++;
    begin
      automatic int total = 0;
      foreach (outstanding[k]) total += outstanding[k].size();
      if (total > max_out) max_out = total;
    end
  end

  // monitor responses and check against the reference
  always @(posedge clk) if (!S_rst && S_rsp_v) begin
    if (!outstanding.exists(S_rsp_id) || outstanding[S_rsp_id].size() == 0) unexpected++;
    else begin
      automatic pending_t p = outstanding[S_rsp_id].pop_front();
      automatic rsp_t r = '{id: S_rsp_id, data: S_rsp_data, err: S_rsp_err,
                  latency: int'(($time - p.t_issue) / 10)};
      logic [15:0] exp;
      n_out--;
      case (p.req.op)
        OP_RD: exp = ref_mem[p.req.addr];
        OP_WR: begin ref_mem[p.req.addr] = p.req.data; exp = 0; end
        default: begin exp = ref_mem[p.req.addr]; ref_mem[p.req.addr] += p.req.data; end
      endcase
      if (r.data === exp && r.err == (p.req.addr == 8'hff)) matched++;
      else begin
        mismatched++;
        $error("MISMATCH id=%0d op=%s exp=%h got=%h", r.id, p.req.op.name(), exp, r.data);
      end
      done.push_back(r);
      if (outstanding[S_rsp_id].size() == 0) outstanding.delete(S_rsp_id);
    end
  end

  // assertions
  sequence s_req_hs; c.req_v && c.req_r; endsequence
  property p_req_stable;
    @(posedge clk) disable iff (rst) (c.req_v && !c.req_r) |=> c.req_v && $stable(c.req);
  endproperty
  a_req_stable: assert property (p_req_stable) else f_stable++;

  property p_rsp_after_req;
    @(posedge clk) disable iff (rst) s_req_hs |-> ##[2:8] c.rsp_v;
  endproperty
  a_rsp: assert property (p_rsp_after_req) else f_rsp++;

  property p_no_rsp_when_idle;
    @(posedge clk) disable iff (rst) c.rsp_v |-> (n_out > 0);
  endproperty
  a_idle: assert property (p_no_rsp_when_idle) else f_idle++;

  c_back2back: cover property (@(posedge clk) disable iff (rst) s_req_hs ##1 s_req_hs) cv_b2b++;
  c_rsp_err:   cover property (@(posedge clk) c.rsp_v && c.rsp_err) cv_err++;
endmodule
