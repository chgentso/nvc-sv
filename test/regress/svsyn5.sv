// Functional coverage of a packet generator: cross coverage, ignore_bins,
// illegal_bins, binsof/intersect, transition bins, option.at_least and
// per-instance coverage queried with get_inst_coverage
// Expected values: IEEE 1800-2017 19.5 (bins, ignore_bins, illegal_bins, transition bins incl.
// [*n] and default sequence), 19.6 (cross; automatically generated cross bins are kept for the
// products that no user-defined cross bin covers; ignore_bins removes products), 19.7/19.8
// (per_instance covergroups of two class objects are independent; get_inst_coverage), 19.11
// (coverage = covered bins / bins, ignored/illegal/default bins excluded, group = average of
// the item percentages with equal weights; type coverage with merge_instances = 0 is the average
// of the instance coverages; $get_coverage averages the covergroup types).
// Random values are not predicted: every sample is recorded and a reference model computes the
// expected coverage numbers (compared with two decimals). The random phase additionally excludes
// addr 8'hff so that no illegal bin is sampled.
package cov_pkg;
  typedef enum logic [1:0] { READ, WRITE, ATOMIC, FLUSH } op_e;
  typedef enum logic [1:0] { SZ1, SZ2, SZ4, SZ8 } size_e;

  class pkt;
    rand op_e   op;
    rand size_e sz;
    rand bit [7:0] addr;
    rand bit [3:0] len;
    constraint c_len { len inside {[1:12]}; }
    constraint c_atomic { (op == ATOMIC) -> (sz inside {SZ4, SZ8}); }
    constraint c_align { (sz == SZ8) -> (addr[2:0] == 0); (sz == SZ4) -> (addr[1:0] == 0); }
    function string str();
      return $sformatf("%s/%s/%02h/%0d", op.name(), sz.name(), addr, len);
    endfunction
  endclass

  class pkt_cov;
    pkt p;
    covergroup cg;
      option.per_instance = 1;
      option.name = "pkt_cg";
      cp_op: coverpoint p.op;
      cp_sz: coverpoint p.sz {
        ignore_bins sz1 = {SZ1};
      }
      cp_addr: coverpoint p.addr {
        bins low  = {[0:63]};
        bins mid  = {[64:191]};
        bins high = {[192:255]};
        illegal_bins badaddr = {8'hff};
      }
      cp_len: coverpoint p.len {
        bins lens[] = {[1:4]};
        bins big     = {[5:12]};
      }
      x_op_sz: cross cp_op, cp_sz {
        ignore_bins flush_any = binsof(cp_op) intersect {FLUSH};
        bins atomic_big = binsof(cp_op) intersect {ATOMIC} && binsof(cp_sz) intersect {SZ8};
      }
      x_op_addr: cross cp_op, cp_addr;
    endgroup

    covergroup trans_cg;
      cp_t: coverpoint p.op {
        bins rw     = (READ => WRITE);
        bins wr     = (WRITE => READ);
        bins seq3   = (READ => READ => WRITE);
        bins rep    = (WRITE [*2]);
        bins any_a = default sequence;
      }
    endgroup

    function new();
      p = new;
      cg = new;
      trans_cg = new;
    endfunction

    typedef struct { op_e op; size_e sz; bit [7:0] addr; bit [3:0] len; } smp_t;
    smp_t hist[$];
    function void sample(pkt q);
      hist.push_back('{q.op, q.sz, q.addr, q.len});
      p = q;
      cg.sample();
      trans_cg.sample();
    endfunction
  endclass
endpackage

module svsyn5;
  import cov_pkg::*;
  pkt q;
  pkt_cov pc, pc2;
  int n, errs;

  function automatic int abin(bit [7:0] a);
    if (a <= 63) return 0;
    if (a <= 191) return 1;
    return 2;
  endfunction
  // expected coverage of one pkt_cov object: [0..5] = cp_op, cp_sz, cp_addr, cp_len, x_op_sz,
  // x_op_addr; [6] = cg group; [7] = trans_cg
  real r[8];
  function automatic void model(pkt_cov o);
    bit s_op[4], s_sz[4], s_ad[3], s_len[5];
    bit s_xs[4][4], s_xa[4][3], atomic_big;
    int c_op, c_sz, c_ad, c_len, c_xs, c_xa, c_tr;
    bit t_rw, t_wr, t_seq3, t_rep;
    foreach (o.hist[i]) begin
      cov_pkg::pkt_cov::smp_t h = o.hist[i];
      s_op[h.op] = 1;
      if (h.sz != SZ1) s_sz[h.sz] = 1;
      if (h.addr != 8'hff) s_ad[abin(h.addr)] = 1;
      if (h.len >= 1 && h.len <= 4) s_len[h.len - 1] = 1;
      else if (h.len >= 5 && h.len <= 12) s_len[4] = 1;
      if (h.op != FLUSH && h.sz != SZ1) begin
        if (h.op == ATOMIC && h.sz == SZ8) atomic_big = 1;
        else s_xs[h.op][h.sz] = 1;
      end
      if (h.addr != 8'hff) s_xa[h.op][abin(h.addr)] = 1;
      if (i > 0) begin
        if (o.hist[i-1].op == READ && h.op == WRITE) t_rw = 1;
        if (o.hist[i-1].op == WRITE && h.op == READ) t_wr = 1;
        if (o.hist[i-1].op == WRITE && h.op == WRITE) t_rep = 1;
      end
      if (i > 1 && o.hist[i-2].op == READ && o.hist[i-1].op == READ && h.op == WRITE) t_seq3 = 1;
    end
    foreach (s_op[i]) c_op += s_op[i];
    foreach (s_sz[i]) c_sz += s_sz[i];
    foreach (s_ad[i]) c_ad += s_ad[i];
    foreach (s_len[i]) c_len += s_len[i];
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) c_xs += s_xs[i][j];
    c_xs += atomic_big;
    for (int i = 0; i < 4; i++) for (int j = 0; j < 3; j++) c_xa += s_xa[i][j];
    c_tr = t_rw + t_wr + t_seq3 + t_rep;
    r[0] = 100.0 * c_op / 4;
    r[1] = 100.0 * c_sz / 3;
    r[2] = 100.0 * c_ad / 3;
    r[3] = 100.0 * c_len / 5;
    r[4] = 100.0 * c_xs / 9;     // 8 automatic bins (READ/WRITE x 3 sizes, ATOMIC x SZ2/SZ4) + atomic_big
    r[5] = 100.0 * c_xa / 12;
    r[6] = (r[0] + r[1] + r[2] + r[3] + r[4] + r[5]) / 6.0;
    r[7] = 100.0 * c_tr / 4;
  endfunction
  task automatic cmp(string nm, real got, real exp);
    if ($sformatf("%0.2f", got) != $sformatf("%0.2f", exp)) begin
      $error("%s: got %0.2f expected %0.2f", nm, got, exp); errs++;
    end
  endtask
  real e1[8], e2[8];
  initial begin
    q = new;
    pc = new;
    for (int i = 0; i < 400; i++) begin
      if (!q.randomize() with { op != FLUSH; addr != 8'hff; }) begin $error("randomize failed"); errs++; end
      pc.sample(q);
    end
    model(pc); e1 = r;
    cmp("cg inst", pc.cg.get_inst_coverage(), e1[6]);
    cmp("cp_op", pc.cg.cp_op.get_inst_coverage(), e1[0]);
    cmp("cp_sz", pc.cg.cp_sz.get_inst_coverage(), e1[1]);
    cmp("cp_addr", pc.cg.cp_addr.get_inst_coverage(), e1[2]);
    cmp("cp_len", pc.cg.cp_len.get_inst_coverage(), e1[3]);
    cmp("x_op_sz", pc.cg.x_op_sz.get_inst_coverage(), e1[4]);
    cmp("x_op_addr", pc.cg.x_op_addr.get_inst_coverage(), e1[5]);
    cmp("trans", pc.trans_cg.get_inst_coverage(), e1[7]);
    // only one instance of each covergroup type exists so far
    cmp("type cov", pc.cg.get_coverage(), e1[6]);
    cmp("total 1", $get_coverage(), (e1[6] + e1[7]) / 2.0);

    // Directed samples into a second object: the covergroups of the objects are independent
    pc2 = new;
    q.op = READ;  q.sz = SZ2; q.addr = 8'h10; q.len = 3;  pc2.sample(q);
    q.op = WRITE; q.sz = SZ4; q.addr = 8'hc0; q.len = 12; pc2.sample(q);
    q.op = ATOMIC; q.sz = SZ8; q.addr = 8'h40; q.len = 5; pc2.sample(q);
    model(pc2); e2 = r;
    cmp("pc2 cg", pc2.cg.get_inst_coverage(), e2[6]);
    cmp("pc2 x", pc2.cg.x_op_sz.get_inst_coverage(), e2[4]);
    cmp("pc2 tr", pc2.trans_cg.get_inst_coverage(), e2[7]);
    cmp("pc cg", pc.cg.get_inst_coverage(), e1[6]);
    cmp("pc tr", pc.trans_cg.get_inst_coverage(), e1[7]);
    cmp("total 2", $get_coverage(), ((e1[6] + e2[6]) / 2.0 + (e1[7] + e2[7]) / 2.0) / 2.0);
    // hand-derived values for the directed object: 3 of 4 ops, 3 of 3 sizes, 3 of 3 address
    // ranges, 2 of 5 length bins, 3 of 9 and 3 of 12 cross bins, 1 of 4 transitions
    cmp("pc2 cp_op", pc2.cg.cp_op.get_inst_coverage(), 75.0);
    cmp("pc2 cp_len", pc2.cg.cp_len.get_inst_coverage(), 40.0);
    cmp("pc2 x_op_addr", pc2.cg.x_op_addr.get_inst_coverage(), 25.0);
    cmp("pc2 cg fixed", pc2.cg.get_inst_coverage(), (75.0 + 100.0 + 100.0 + 40.0 + 100.0 * 3 / 9 + 25.0) / 6.0);
    cmp("pc2 tr fixed", pc2.trans_cg.get_inst_coverage(), 25.0);
    if (errs == 0) $display("PASSED");
  end
endmodule
