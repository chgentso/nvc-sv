// Arrays of dynamic arrays and queues of queues whose outer level has the length it has when randomize is
// called: each inner array is solved on its own and the sizes of the inner arrays can be constrained.
class A;
  rand int q[$][$];
  rand int dyn[][];
  rand int qd[$][];
  rand int dq[][$];
  constraint c {
    foreach (q[i, j]) q[i][j] == (2 * i) + j;
    dyn[0][0] == 10;
    dyn[1][1] > 50;
    dyn[1][1] < 60;
    foreach (qd[i, j]) qd[i][j] == i + j + 3;
    foreach (dq[i, j]) dq[i][j] == 3 * i + j;
  }
endclass

class B;
  rand int q[$][$];
  constraint c {
    foreach (q[i]) q[i].size() == 2;
    foreach (q[i, j]) q[i][j] == j + 10 * i;
  }
endclass

module svcrvnestdyn1;
  initial begin
    A a = new;
    B b = new;
    int ok;
    a.q = {{0, 0, 0}, {0, 0}};
    a.dyn = new[2];
    a.dyn[0] = new[2];
    a.dyn[1] = new[3];
    a.qd = {};
    a.qd[0] = new[2];
    a.qd[1] = new[2];
    a.dq = new[2];
    a.dq[0] = {1, 2};
    a.dq[1] = {3};
    repeat (5) begin
      ok = a.randomize();
      if (ok != 1) $error("A %0d", ok);
      if (a.q.size() != 2 || a.q[0].size() != 3 || a.q[1].size() != 2) $error("sizes of q");
      if (a.q[0][2] != 2 || a.q[1][1] != 3) $error("q %p", a.q);
      if (a.dyn[0][0] != 10 || a.dyn[1][1] <= 50 || a.dyn[1][1] >= 60) $error("dyn %p", a.dyn);
      if (a.qd[1][1] != 5 || a.dq[1][0] != 3) $error("qd %p dq %p", a.qd, a.dq);
    end

    b.q = {{1}, {2, 3, 4}, {}};
    ok = b.randomize();
    if (ok != 1 || b.q.size() != 3) $error("B %0d", ok);
    if (b.q[0].size() != 2 || b.q[1].size() != 2 || b.q[2].size() != 2) $error("sizes of B");
    if (b.q[2][1] != 21 || b.q[1][0] != 10) $error("B %p", b.q);
    $display("PASSED");
  end
endmodule
