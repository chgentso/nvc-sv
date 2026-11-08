// Constraint prototypes defined out of the class body
// Expected values: IEEE 1800-2017 18.5.2 (constraint prototypes, external constraint
// blocks; every prototype shall have a definition), 18.6.1 (randomize returns 1 on success)
class a;
  rand int b;
  constraint c;
endclass

class d;
  rand int e;
  constraint k;
endclass

constraint a::c { b inside {5, 6}; }
constraint d::k { e inside {[3:4]}; }

module svconstrimpl1;
  a o = new;
  d p = new;
  int errs = 0;
  initial begin
    for (int i = 0; i < 10; i++) begin
      if (!o.randomize() || (o.b != 5 && o.b != 6)) begin $error("a: randomize failed or b=%0d", o.b); errs++; end
    end
    for (int i = 0; i < 10; i++) begin
      if (!p.randomize() || p.e < 3 || p.e > 4) begin $error("d: randomize failed or e=%0d", p.e); errs++; end
    end
    if (errs == 0) $display("PASSED");
  end
endmodule
