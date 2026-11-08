// Self-checking: rand_mode on an object-handle member, on the whole object and on a member variable.
// Expected values: IEEE 1800-2017 18.8 (rand_mode: a disabled variable is treated as a state variable;
// disabling a handle excludes the pointed-to object, and its constraints, from the solve; rand_mode(1) on the
// object re-enables its rand variables), 18.4 (rand handles), 18.5 (constraints, a non-random variable
// takes part in constraints as a state variable).
module svcrvobjoff1;
  class P;
    rand bit a;
    bit b = 0;
    rand bit [3:0] arr[];
    constraint c { b == 1; a == 1; }
    function new(); arr = new[2]; endfunction
  endclass
  class C;
    rand P p;
    rand bit x;
    function new(); p = new; endfunction
  endclass
  initial begin
    automatic C c = new;
    int r;
    automatic bit failed = 0;
    c.p.rand_mode(0);
    r = c.randomize();
    if (r !== 1 || c.p.a !== 0) begin failed = 1; $error("A: r=%0d a=%0d, expected 1 0", r, c.p.a); end
    c.p.rand_mode(1);
    c.p.b = 1;
    r = c.randomize();
    if (r !== 1 || c.p.a !== 1) begin failed = 1; $error("C: r=%0d a=%0d, expected 1 1", r, c.p.a); end
    c.p.b = 0;
    c.rand_mode(0);
    r = c.randomize();
    if (r !== 1) begin failed = 1; $error("D: r=%0d, expected 1", r); end
    c.rand_mode(1);
    c.p.a.rand_mode(0);
    c.p.b = 1;
    r = c.randomize();
    if (r !== 1 || c.p.a !== 1) begin failed = 1; $error("E: r=%0d a=%0d, expected 1 1", r, c.p.a); end
    r = c.p.randomize();
    if (r !== 1) begin failed = 1; $error("F: r=%0d, expected 1", r); end
    if (!failed) $display("PASSED");
  end
endmodule
