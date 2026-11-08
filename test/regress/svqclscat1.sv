// Queue-of-class-handle assignment from concatenations, including empty and derived-class handles.
// Expected values: IEEE 1800-2017 7.10.4 (queue assignment from unpacked array concatenation,
// {} is the empty queue), 8.15/8.22 (handle assignment compatibility of derived to base)
class I; int v; function new(int v); this.v = v; endfunction endclass
class J extends I; function new(int v); super.new(v); endfunction endclass
module svqclscat1;
  I q[$];
  I a, b;
  J j;
  int errs = 0;
  task automatic chk(string n, int got, int exp);
    if (got != exp) begin errs++; $error("%s: got %0d expected %0d", n, got, exp); end
  endtask
  initial begin
    a = new(1); b = new(2); j = new(3);
    q = {a};
    chk("size1", q.size(), 1);
    q = {a, b};
    chk("size2", q.size(), 2);
    q = {q, a};
    chk("size3", q.size(), 3);
    q = {};
    chk("size0", q.size(), 0);
    q = {a, j, b};
    chk("size3b", q.size(), 3); chk("q[1].v", q[1].v, 3);
    if (1) q = {b};
    chk("size1b", q.size(), 1); chk("q[0].v", q[0].v, 2);
    if (errs == 0) $display("PASSED");
  end
endmodule
