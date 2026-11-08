// UVM-style scoreboard: transactions with constraints, a reference model held
// in associative arrays, covergroups, queues, mailboxes and assertions
// Expected values: IEEE 1800-2017 18.5 (constraints hold in every solution), 18.5.4 (dist),
// 18.5.10 (solve before), 7.8 (associative arrays), 15.4 (mailbox), 19.5/19.6 (bins, cross,
// ignore_bins, binsof), 19.8/19.11 (coverage = bins covered / bins, group = average of the
// item percentages, equal weights). Random values are not predicted: a recording of the
// sampled (kind, region, len, hit) tuples feeds a coverage reference model, and
// constraint-satisfaction and the scoreboard counters are checked directly.
package syn_pkg;
  typedef enum bit [1:0] { RD = 2'd0, WR = 2'd1, RMW = 2'd2, NOP = 2'd3 } kind_e;

  class txn;
    rand kind_e       kind;
    rand bit [15:0]   addr;
    rand bit [31:0]   data;
    rand bit [3:0]    len;
    rand bit [1:0]    prio;
    int               id;
    static int        next_id = 0;

    constraint c_addr { addr[1:0] == 2'b00; addr inside {[16'h0000:16'h00FF], [16'h8000:16'h80FF]}; }
    constraint c_len  { kind == NOP -> len == 0; kind != NOP -> len inside {[1:8]}; }
    constraint c_kind { kind dist { RD := 4, WR := 4, RMW := 1, NOP := 1 }; }
    constraint c_prio { solve kind before prio; (kind == RMW) -> prio == 3; }

    function new(); id = next_id++; endfunction

    function string convert2string();
      return $sformatf("#%0d %s a=%04h d=%08h l=%0d p=%0d", id, kind.name(), addr, data, len, prio);
    endfunction

    function bit compare(txn o);
      return kind == o.kind && addr == o.addr && data == o.data && len == o.len;
    endfunction
  endclass

  class model;
    bit [31:0] mem [bit [15:0]];
    int        reads, writes, hits, misses;

    function bit [31:0] access(txn t);
      bit [31:0] r = 0;
      case (t.kind)
        RD: begin
          reads++;
          if (mem.exists(t.addr)) begin hits++; r = mem[t.addr]; end
          else misses++;
        end
        WR: begin writes++; mem[t.addr] = t.data; end
        RMW: begin
          reads++; writes++;
          r = mem.exists(t.addr) ? mem[t.addr] : 32'hDEAD_BEEF;
          mem[t.addr] = r ^ t.data;
        end
        default: ;
      endcase
      return r;
    endfunction
  endclass

  class scoreboard;
    model      m = new();
    txn        expected_q[$];
    int        n_ok, n_bad;
    int        by_kind[string];
    typedef struct { kind_e k; bit region; bit [3:0] len; bit hit; } smp_t;
    smp_t      smp[$];
    bit [31:0] last_by_region[string];

    covergroup cg with function sample(txn t, bit hit);
      option.per_instance = 1;
      cp_kind: coverpoint t.kind;
      cp_region: coverpoint t.addr[15] { bins low = {0}; bins high = {1}; }
      cp_len: coverpoint t.len { bins one = {1}; bins few[] = {[2:4]}; bins many = {[5:8]}; ignore_bins zero = {0}; }
      cp_hit: coverpoint hit;
      x_kind_region: cross cp_kind, cp_region;
      x_kind_hit: cross cp_kind, cp_hit { ignore_bins nop = binsof(cp_kind) intersect {NOP}; }
    endgroup

    function new(); cg = new(); endfunction

    function void check(txn t, bit [31:0] got);
      bit [31:0] exp;
      bit hit = m.mem.exists(t.addr);
      exp = m.access(t);
      by_kind[t.kind.name()]++;
      last_by_region[t.addr[15] ? "high" : "low"] = t.data;
      cg.sample(t, hit);
      smp.push_back('{t.kind, t.addr[15], t.len, hit});
      if (got === exp) n_ok++; else n_bad++;
    endfunction
  endclass
endpackage

module svsyn1;
  import syn_pkg::*;
  scoreboard sb;
  model      dut_model;
  mailbox #(txn) mbx = new();
  int n_sent, n_bad_constraint;
  int kinds_seen[kind_e];

  initial begin
    txn t;
    sb = new();
    dut_model = new();
    for (int i = 0; i < 400; i++) begin
      t = new();
      if (!t.randomize()) $error("randomize failed");
      if (t.addr[1:0] != 0 || !(t.addr inside {[16'h0000:16'h00FF], [16'h8000:16'h80FF]})) n_bad_constraint++;
      if (t.kind == NOP && t.len != 0) n_bad_constraint++;
      if (t.kind != NOP && !(t.len inside {[1:8]})) n_bad_constraint++;
      if (t.kind == RMW && t.prio != 3) n_bad_constraint++;
      kinds_seen[t.kind]++;
      mbx.put(t);
      n_sent++;
    end
    while (mbx.num() > 0) begin
      bit [31:0] got;
      mbx.get(t);
      got = dut_model.access(t);
      sb.check(t, got);
    end
    begin
      automatic int errs = 0;
      automatic int tot_kind = 0;
      bit seen_k[4], seen_r[2], seen_h[2], seen_kr[4][2], seen_kh[4][2];
      bit seen_len[5];
      int nk, nr, nl, nh, nkr, nkh;
      real e_kind, e_region, e_len, e_hit, e_x1, e_x2, e_cg;
      if (n_sent != 400) begin $error("sent %0d", n_sent); errs++; end
      if (n_bad_constraint != 0) begin $error("constraint violations %0d", n_bad_constraint); errs++; end
      if (sb.n_bad != 0 || sb.n_ok != 400) begin $error("scoreboard n_bad=%0d n_ok=%0d", sb.n_bad, sb.n_ok); errs++; end
      if (kinds_seen.num() != 4) begin $error("not all kinds seen"); errs++; end
      if (!(sb.m.mem.num() == dut_model.mem.num() && sb.m.reads == dut_model.reads && sb.m.writes == dut_model.writes)) begin
        $error("models differ"); errs++;
      end
      if (sb.last_by_region.num() != 2 || !sb.last_by_region.exists("low") || !sb.last_by_region.exists("high")) begin
        $error("regions: %0d", sb.last_by_region.num()); errs++;
      end
      foreach (sb.by_kind[k]) tot_kind += sb.by_kind[k];
      if (tot_kind != 400) begin $error("by_kind total %0d", tot_kind); errs++; end
      // coverage reference model (19.11)
      foreach (sb.smp[i]) begin
        seen_k[sb.smp[i].k] = 1; seen_r[sb.smp[i].region] = 1; seen_h[sb.smp[i].hit] = 1;
        seen_kr[sb.smp[i].k][sb.smp[i].region] = 1;
        if (sb.smp[i].k != NOP) seen_kh[sb.smp[i].k][sb.smp[i].hit] = 1;
        case (sb.smp[i].len)
          1: seen_len[0] = 1;
          2, 3, 4: seen_len[sb.smp[i].len - 1] = 1;
          5, 6, 7, 8: seen_len[4] = 1;
          default: ;
        endcase
      end
      foreach (seen_k[i]) nk += seen_k[i];
      foreach (seen_r[i]) nr += seen_r[i];
      foreach (seen_h[i]) nh += seen_h[i];
      foreach (seen_len[i]) nl += seen_len[i];
      for (int i = 0; i < 4; i++) for (int j = 0; j < 2; j++) begin nkr += seen_kr[i][j]; nkh += seen_kh[i][j]; end
      e_kind = 100.0 * nk / 4; e_region = 100.0 * nr / 2; e_len = 100.0 * nl / 5; e_hit = 100.0 * nh / 2;
      e_x1 = 100.0 * nkr / 8; e_x2 = 100.0 * nkh / 6;
      e_cg = (e_kind + e_region + e_len + e_hit + e_x1 + e_x2) / 6.0;
      cmp2("cov", sb.cg.get_inst_coverage(), e_cg, errs);
      cmp2("cp_kind", sb.cg.cp_kind.get_inst_coverage(), e_kind, errs);
      cmp2("cp_region", sb.cg.cp_region.get_inst_coverage(), e_region, errs);
      cmp2("cp_len", sb.cg.cp_len.get_inst_coverage(), e_len, errs);
      cmp2("cp_hit", sb.cg.cp_hit.get_inst_coverage(), e_hit, errs);
      cmp2("x_kind_region", sb.cg.x_kind_region.get_inst_coverage(), e_x1, errs);
      cmp2("x_kind_hit", sb.cg.x_kind_hit.get_inst_coverage(), e_x2, errs);
      if (errs == 0) $display("PASSED");
    end
    $finish;
  end
  task automatic cmp2(string nm, real got, real exp, ref int errs);
    if ($sformatf("%0.2f", got) != $sformatf("%0.2f", exp)) begin
      $error("%s: got %0.2f expected %0.2f", nm, got, exp); errs++;
    end
  endtask
endmodule
