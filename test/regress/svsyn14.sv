// Coverage driven randomisation: constraints, solve ordering, covergroups with
// cross and transition bins, loops until the goal is reached
// Expected values: IEEE 1800-2017 18.5 (constraints: implication, dist, solve before, inside,
// !=), 18.6.2 (post_randomize), 19.5 (bins, transition bins), 19.6 (cross), 19.11 (coverage =
// covered bins / bins; group = average of the item percentages, equal weights). Random values
// are not predicted: every constraint is checked on every solution, and all coverage numbers
// are computed by a reference model from the recorded samples.
class pkt;
  typedef enum {SMALL, MEDIUM, LARGE} size_e;
  rand size_e  size;
  rand bit [7:0] len;
  rand bit [3:0] port;
  rand bit       urgent;
  rand bit [1:0] mode;
  rand int unsigned gap;
  bit [3:0] last_port;

  constraint c_len {
    (size == SMALL)  -> len inside {[1:15]};
    (size == MEDIUM) -> len inside {[16:100]};
    (size == LARGE)  -> len inside {[101:200]};
  }
  constraint c_port { port < 12; port != last_port; }
  constraint c_gap { gap inside {[0:20]}; urgent -> gap < 3; }
  constraint c_mode { solve size before len; mode dist {0 := 4, 1 := 3, [2:3] :/ 3}; }

  function void post_randomize();
    last_port = port;
  endfunction
endclass

module svsyn14;
  pkt p = new;
  int iterations;

  covergroup cg with function sample(pkt x);
    option.per_instance = 1;
    cp_size: coverpoint x.size;
    cp_len: coverpoint x.len { bins lo = {[1:15]}; bins mid = {[16:100]}; bins hi = {[101:200]}; }
    cp_port: coverpoint x.port { bins ports[] = {[0:11]}; }
    cp_mode: coverpoint x.mode;
    cp_urg: coverpoint x.urgent;
    x_size_mode: cross cp_size, cp_mode;
    x_size_urg: cross cp_size, cp_urg;
    cp_trans: coverpoint x.size { bins up = (pkt::SMALL => pkt::MEDIUM => pkt::LARGE);
                                  bins down = (pkt::LARGE => pkt::MEDIUM => pkt::SMALL);
                                  bins same = (pkt::SMALL => pkt::SMALL); }
  endgroup

  cg c = new;

  // recorded samples for the reference model
  typedef struct { pkt::size_e size; bit [7:0] len; bit [3:0] port; bit [1:0] mode; bit urgent; } smp_t;
  smp_t hist[$];
  int errs;
  function void srec();
    hist.push_back('{p.size, p.len, p.port, p.mode, p.urgent});
    c.sample(p);
  endfunction

  // directed values to close what random does not reach quickly
  task automatic directed(pkt::size_e s, bit [1:0] m);
    int tries = 0;
    while (tries++ < 400) begin
      if (!p.randomize() with { size == s; mode == m; }) $fatal(1, "randomize failed");
      srec();
      iterations++;
      if (p.size == s && p.mode == m) return;
    end
  endtask

  initial begin
    real cov, last;
    bit [3:0] prev_port;
    int stagnant;
    // phase 1: unconstrained random until stagnation
    repeat (3000) begin
      prev_port = p.last_port;
      if (!p.randomize()) $fatal(1, "randomize failed");
      srec();
      iterations++;
      if (p.port >= 12 || p.len == 0 || p.len > 200) begin $error("constraint violated"); errs++; end
      if (p.port == prev_port) begin $error("port repeated"); errs++; end
      if (p.size == pkt::SMALL && !(p.len inside {[1:15]})) begin $error("len/SMALL"); errs++; end
      if (p.size == pkt::MEDIUM && !(p.len inside {[16:100]})) begin $error("len/MEDIUM"); errs++; end
      if (p.size == pkt::LARGE && !(p.len inside {[101:200]})) begin $error("len/LARGE"); errs++; end
      if (p.gap > 20 || (p.urgent && p.gap >= 3)) begin $error("gap"); errs++; end
      if (iterations % 100 == 0) begin
        cov = c.get_inst_coverage();
        if (cov == last) stagnant++; else stagnant = 0;
        last = cov;
        if (stagnant > 5) break;
      end
    end
    // phase 2: directed closure for the cross
    for (int s = 0; s < 3; s++)
      for (int m = 0; m < 4; m++)
        directed(pkt::size_e'(s), m[1:0]);
    // transitions
    begin
      automatic pkt::size_e seq[] = '{pkt::SMALL, pkt::MEDIUM, pkt::LARGE, pkt::MEDIUM, pkt::SMALL, pkt::SMALL};
      foreach (seq[i]) begin
        if (!p.randomize() with { size == seq[i]; }) $fatal(1, "randomize failed");
        srec();
      end
    end
    // urgent with large
    repeat (50) begin
      if (!p.randomize() with { urgent == 1; size == pkt::LARGE; }) $fatal(1, "randomize failed");
      srec();
      if (p.gap >= 3) begin $error("urgent gap violated"); errs++; end
    end
    check();
    if (errs == 0) $display("PASSED");
  end

  task automatic cmp(string nm, real got, real exp);
    if ($sformatf("%0.2f", got) != $sformatf("%0.2f", exp)) begin
      $error("%s: got %0.2f expected %0.2f", nm, got, exp); errs++;
    end
  endtask
  task automatic check();
    bit s_size[3], s_len[3], s_port[12], s_mode[4], s_urg[2], s_xm[3][4], s_xu[3][2];
    bit t_up, t_down, t_same;
    int c_size, c_len, c_port, c_mode, c_urg, c_xm, c_xu, c_tr;
    real e_size, e_len, e_port, e_mode, e_urg, e_xm, e_xu, e_tr, e_all;
    foreach (hist[i]) begin
      s_size[hist[i].size] = 1;
      if (hist[i].len >= 1 && hist[i].len <= 15) s_len[0] = 1;
      else if (hist[i].len >= 16 && hist[i].len <= 100) s_len[1] = 1;
      else if (hist[i].len >= 101 && hist[i].len <= 200) s_len[2] = 1;
      if (hist[i].port <= 11) s_port[hist[i].port] = 1;
      s_mode[hist[i].mode] = 1;
      s_urg[hist[i].urgent] = 1;
      s_xm[hist[i].size][hist[i].mode] = 1;
      s_xu[hist[i].size][hist[i].urgent] = 1;
      if (i > 0 && hist[i-1].size == pkt::SMALL && hist[i].size == pkt::SMALL) t_same = 1;
      if (i > 1) begin
        if (hist[i-2].size == pkt::SMALL && hist[i-1].size == pkt::MEDIUM && hist[i].size == pkt::LARGE) t_up = 1;
        if (hist[i-2].size == pkt::LARGE && hist[i-1].size == pkt::MEDIUM && hist[i].size == pkt::SMALL) t_down = 1;
      end
    end
    foreach (s_size[i]) c_size += s_size[i];
    foreach (s_len[i]) c_len += s_len[i];
    foreach (s_port[i]) c_port += s_port[i];
    foreach (s_mode[i]) c_mode += s_mode[i];
    foreach (s_urg[i]) c_urg += s_urg[i];
    for (int i = 0; i < 3; i++) begin
      for (int j = 0; j < 4; j++) c_xm += s_xm[i][j];
      for (int j = 0; j < 2; j++) c_xu += s_xu[i][j];
    end
    c_tr = t_up + t_down + t_same;
    e_size = 100.0 * c_size / 3; e_len = 100.0 * c_len / 3; e_port = 100.0 * c_port / 12;
    e_mode = 100.0 * c_mode / 4; e_urg = 100.0 * c_urg / 2;
    e_xm = 100.0 * c_xm / 12; e_xu = 100.0 * c_xu / 6; e_tr = 100.0 * c_tr / 3;
    e_all = (e_size + e_len + e_port + e_mode + e_urg + e_xm + e_xu + e_tr) / 8.0;
    cmp("cp_size", c.cp_size.get_inst_coverage(), e_size);
    cmp("cp_len", c.cp_len.get_inst_coverage(), e_len);
    cmp("cp_port", c.cp_port.get_inst_coverage(), e_port);
    cmp("cp_mode", c.cp_mode.get_inst_coverage(), e_mode);
    cmp("cp_urg", c.cp_urg.get_inst_coverage(), e_urg);
    cmp("x_size_mode", c.x_size_mode.get_inst_coverage(), e_xm);
    cmp("x_size_urg", c.x_size_urg.get_inst_coverage(), e_xu);
    cmp("cp_trans", c.cp_trans.get_inst_coverage(), e_tr);
    cmp("total", c.get_inst_coverage(), e_all);
    // the directed phase samples every (size, mode) pair, the transition sequence and urgent LARGE
    if (c_xm != 12 || c_tr != 3 || c_urg != 2) begin $error("directed phase left holes: xm=%0d tr=%0d urg=%0d", c_xm, c_tr, c_urg); errs++; end
  endtask
endmodule
