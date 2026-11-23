// A member of another object as the argument of randomize is solved, the
// other properties of the objects are left alone
class Baz;
  int four;
  int five;
endclass
class Qux;
  Baz baz;
  rand int q;
  function new; baz = new; endfunction
endclass
module svcrvargmember1;
  Qux qux;
  int changed, bad;
  initial begin
    qux = new;
    qux.q = 5; qux.baz.four = 1; qux.baz.five = 2;
    for (int n = 0; n < 10; n++) begin
      void'(qux.randomize(baz.four));
      if (qux.baz.four != 1) changed++;
      if (qux.q != 5 || qux.baz.five != 2) bad++;
      qux.baz.four = 1;
    end
    if (bad == 0 && changed > 5) $display("PASSED");
    else $display("FAILED %0d %0d", bad, changed);
  end
endmodule
