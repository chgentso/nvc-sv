// A class specialised in the body of a method defined outside of its class keeps all of its
// methods, which the constraint block of randomize names.
class item; rand int unsigned d; endclass

class gen #(type T = item);
  rand T cmd;
  function new(); cmd = new; endfunction
  function int unsigned limit(); return 100; endfunction
endclass

class outer;
  extern function void body();
endclass

function void outer::body();
  gen #(item) seq;
  int ok;
  seq = new;
  repeat (10) begin
    ok = seq.randomize() with { cmd.d <= limit(); };
    if (ok != 1 || seq.cmd.d > 100) $error("ok %0d d %0d", ok, seq.cmd.d);
  end
endfunction

module svcrvspecextern1;
  initial begin
    automatic outer o = new;
    o.body();
    $display("PASSED");
  end
endmodule
