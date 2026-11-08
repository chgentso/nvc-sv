// Self-checking test of cross bins defined with `binsof(...) with (...)`, `intersect` and `||`.
// Expected values: IEEE 1800-2017 19.6.1 (user-defined cross bins: a bin selects the cross products
// (tuples of coverpoint bins) it names; products selected by no user bin remain as automatic bins),
// 19.6.1.1 (binsof with `with`: a coverpoint bin is selected when some value of it satisfies the
// expression, and `intersect` selects the bins that share a value with the set), 19.8 (covered/total),
// 19.11.  The reference model below computes, for each cross, the selected tuples, the total number of
// bins (1 user bin + unselected automatic tuples) and the covered count for every (a, b) sample.
module svcovxwith1;
  bit [1:0] a, b; int k = 1;
  covergroup g;
    ca: coverpoint a { bins lo = {0,1}; bins hi = {2,3}; bins m = {1,2}; }
    cb: coverpoint b { bins x = {0}; bins y = {1,2}; bins z = {3}; }
    c1: cross ca, cb { bins w1 = binsof(ca) with (ca == 1); }
    c2: cross ca, cb { bins w2 = binsof(ca) with (ca > 1); }
    c3: cross ca, cb { bins w3 = binsof(ca) with (ca + cb == 3); }
    c4: cross ca, cb { bins w4 = binsof(cb) with (cb == k); }
    c5: cross ca, cb { bins w5 = (binsof(ca.lo) || binsof(cb.z)) with (cb == 0); }
    c6: cross ca, cb { bins w6 = binsof(ca) intersect {1} with (cb > 0); }
  endgroup
  g gi = new;
  int fails = 0;
  // coverpoint bins as value masks: ca: lo={0,1} hi={2,3} m={1,2}; cb: x={0} y={1,2} z={3}
  bit [3:0] am[3] = '{4'b0011, 4'b1100, 4'b0110};
  bit [3:0] bm[3] = '{4'b0001, 4'b0110, 4'b1000};
  function automatic bit sel(int cr, int i, int j);
    bit r = 0;
    for (int va = 0; va < 4; va++) for (int vb = 0; vb < 4; vb++) if (am[i][va] && bm[j][vb]) begin
      case (cr)
        1: if (va == 1) r = 1;
        2: if (va > 1) r = 1;
        3: if (va + vb == 3) r = 1;
        4: if (vb == k) r = 1;
        5: if ((i == 0 || j == 2) && vb == 0) r = 1;
        6: if (am[i][1] && vb > 0) r = 1;
      endcase
    end
    return r;
  endfunction
  // number of bins of cross cr: one user bin (if it selects anything) plus the unselected tuples
  function automatic int total(int cr);
    int n = 0, any = 0;
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) if (sel(cr, i, j)) any = 1; else n++;
    return n + any;
  endfunction
  // covered bins after a single sample (va, vb)
  function automatic int covered(int cr, int va, int vb);
    int n = 0, usr = 0;
    for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) if (am[i][va] && bm[j][vb]) begin
      if (sel(cr, i, j)) usr = 1; else n++;
    end
    return n + usr;
  endfunction
  function automatic int got(int cr, g h);
    int c, t;
    case (cr)
      1: void'(h.c1.get_inst_coverage(c, t));
      2: void'(h.c2.get_inst_coverage(c, t));
      3: void'(h.c3.get_inst_coverage(c, t));
      4: void'(h.c4.get_inst_coverage(c, t));
      5: void'(h.c5.get_inst_coverage(c, t));
      default: void'(h.c6.get_inst_coverage(c, t));
    endcase
    return c;
  endfunction
  function automatic int gott(int cr, g h);
    int c, t;
    case (cr)
      1: void'(h.c1.get_inst_coverage(c, t));
      2: void'(h.c2.get_inst_coverage(c, t));
      3: void'(h.c3.get_inst_coverage(c, t));
      4: void'(h.c4.get_inst_coverage(c, t));
      5: void'(h.c5.get_inst_coverage(c, t));
      default: void'(h.c6.get_inst_coverage(c, t));
    endcase
    return t;
  endfunction
  initial begin
    for (int cr = 1; cr <= 6; cr++) begin
      if (got(cr, gi) != 0 || gott(cr, gi) != total(cr)) begin
        fails++; $error("c%0d before sampling: covered/total %0d/%0d, expected 0/%0d", cr, got(cr, gi), gott(cr, gi), total(cr));
      end
    end
    // the bin counts derived by hand from the selection rules above: 4 4 5 7 9 6
    if (total(1) != 4 || total(2) != 4 || total(3) != 5 || total(4) != 7 || total(5) != 9 || total(6) != 6) begin
      fails++; $error("reference model bin counts %0d %0d %0d %0d %0d %0d", total(1), total(2), total(3), total(4), total(5), total(6));
    end
    for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) begin
      g h; h = new;
      a = i; b = j; h.sample();
      for (int cr = 1; cr <= 6; cr++) begin
        if (got(cr, h) != covered(cr, i, j)) begin
          fails++;
          $error("a=%0d b=%0d c%0d: covered %0d, expected %0d", i, j, cr, got(cr, h), covered(cr, i, j));
        end
      end
    end
    if (fails == 0) $display("PASSED");
  end
endmodule
