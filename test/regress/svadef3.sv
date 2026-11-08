// IEEE 1800-2017 16.4: the actual arguments of the subroutine called by
// the action block of a deferred assertion are evaluated when the
// assertion is evaluated; an output argument is still written when the
// action block runs.  Arguments given by name are captured too.
class box; int o; endclass

module svadef3;
  int g = 1;
  box b = new;
  int errs;
  int got[$];
  function void f(int v, int w); got.push_back(v); got.push_back(w); endfunction
  task t(output int x, input int v); x = v; endtask
  initial begin
    assert #0 (0) else t(b.o, g);
    assert #0 (0) else f(.w(g + 10), .v(g));
    g = 7;
    #1;
    if (b.o != 1) begin $error("output argument got %0d, expected 1", b.o); errs++; end
    if (got.size() != 2 || got[0] != 1 || got[1] != 11) begin
      $error("named arguments got %p, expected '{1, 11}", got); errs++;
    end
    if (errs == 0) $display("PASSED");
  end
endmodule
