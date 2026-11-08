// Writes to queue and dynamic array elements which do not exist have no
// effect: an index with x or z bits, a negative index, or an index past the
// end (except one past the end of a queue, which appends)
// IEEE 1800-2017 7.4.6 (indexing with x/z or out of range: a write has no
// effect, a read gives the default value), 7.10.1 (queue operators: Q[$+1]
// appends), 7.5 (dynamic arrays never grow by indexing)
module svqueuexidx1;
  typedef int iq_t[$];
  iq_t q;
  int d[];
  logic [3:0] lq[$];
  integer ix;
  int errs;
  task automatic ck(bit ok, string m);
    if (!ok) begin $display("FAILED %s", m); errs++; end
  endtask
  initial begin
    q = '{1, 2, 3};
    q[ix] = 9;
    ck(q == iq_t'{1, 2, 3}, "x index write");
    ck(q[ix] == 0, "x index read");
    q[5] = 7;
    ck(q == iq_t'{1, 2, 3}, "write beyond the end");
    q[-1] = 7;
    ck(q == iq_t'{1, 2, 3}, "negative index write");
    q[3] = 4;
    ck(q == iq_t'{1, 2, 3, 4}, "write one past the end appends");
    q[$+1] = 5;
    ck(q == iq_t'{1, 2, 3, 4, 5}, "Q[$+1] appends");
    ix = 1;
    q[ix] = 8;
    ck(q == iq_t'{1, 8, 3, 4, 5}, "known index write");
    ix = 'z;
    q[ix] += 1;
    ck(q == iq_t'{1, 8, 3, 4, 5}, "z index operator assignment");
    lq = '{4'h1};
    lq[ix] = 4'h5;
    ck(lq.size() == 1 && lq[0] === 4'h1 && lq[ix] === 4'hx, "4-state queue");
    d = new[2];
    d[ix] = 3;
    d[2] = 4;
    ck(d.size() == 2 && d[0] == 0 && d[1] == 0, "dynamic array");
    if (errs == 0) $display("PASSED");
  end
endmodule
