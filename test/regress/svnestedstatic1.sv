// Nested classes with the same name in different outer classes each have their own static members
// Expected values: IEEE 1800-2017 8.23 and 8.24 (a nested class is declared in the scope of its outer class, so pk::A::N,
// pk::B::N, UA::N and UB::N are four different classes), 8.9 (one static property per class).
// Calls are made in separate statements: the order of evaluation inside one argument list is not specified.
package pk;
  class A;
    class N; static int cnt; endclass
    function int bump(); N::cnt++; return N::cnt; endfunction
  endclass
  class B;
    class N; static int cnt; endclass
    function int bump(); N::cnt++; return N::cnt; endfunction
  endclass
endpackage

class UA;
  class N;
    static int cnt;
    function int bump(); cnt++; return cnt; endfunction
  endclass
  N n = new;
endclass

class UB;
  class N;
    static int cnt;
    function int bump(); cnt++; return cnt; endfunction
  endclass
  N n = new;
endclass

module svnestedstatic1;
  pk::A a = new;
  pk::B b = new;
  UA ua = new;
  UB ub = new;
  int fails = 0;
  int r1, r2, r3;
  initial begin
    r1 = a.bump(); r2 = a.bump(); r3 = b.bump();
    if (r1 != 1 || r2 != 2 || r3 != 1) begin $error("pk: %0d %0d %0d", r1, r2, r3); fails++; end
    r1 = ua.n.bump(); r2 = ua.n.bump(); r3 = ub.n.bump();
    if (r1 != 1 || r2 != 2 || r3 != 1) begin $error("U: %0d %0d %0d", r1, r2, r3); fails++; end
    if (pk::A::N::cnt != 2 || pk::B::N::cnt != 1 || UA::N::cnt != 2 || UB::N::cnt != 1) begin
      $error("final counts"); fails++;
    end
    if (fails == 0) $display("PASSED");
  end
endmodule
