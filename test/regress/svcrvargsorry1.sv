// A member of another object as the argument of randomize is reported, not ignored
class Baz;
  int four;
endclass
class Qux;
  Baz baz;
  rand int q;
  function new; baz = new; endfunction
endclass
module svcrvargsorry1;
  Qux qux;
  initial begin
    qux = new;
    void'(qux.randomize(baz.four));
    $display("not reached");
  end
endmodule
