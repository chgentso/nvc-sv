// A class object created with new and assigned to a hierarchical name whose
// class is only known once the name is bound (IEEE 1800-2017 8.7, 23.6)
class Cls;
  int v;
  int q[$];
  function new(int v = 7);
    this.v = v;
    q.push_back(v);
  endfunction
endclass

module sub;
  Cls c;
endmodule

module svnewhier1;
  sub u();
  if (1) begin : named
    Cls c;
  end

  initial begin
    named.c = new;
    $display("%0d %0d", named.c.v, named.c.q[0]);
    named.c = new(3);
    $display("%0d %0d", named.c.v, named.c.q[0]);
    u.c = new(5);
    $display("%0d %0d", u.c.v, u.c.q[0]);
    u.c = new;
    $display("%0d", u.c.v);
    foreach (named.c.q[i]) $display("q %0d %0d", i, named.c.q[i]);
  end
endmodule
