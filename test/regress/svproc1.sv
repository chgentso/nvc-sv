// IEEE 1800-2017 18.13 to 18.15: the random stream of a process.  Values are not
// specified so the checks compare streams with each other.
class item;
  rand bit [31:0] a;
  rand bit [7:0]  b;
endclass

module svproc1;
  int errs = 0;
  task automatic check(bit ok, string what);
    if (!ok) begin $error("%s", what); errs++; end
  endtask

  initial begin
    process p;
    string st;
    int a, b, c;
    int seq1[4], seq2[4];
    item x, y;
    p = process::self();

    // Restoring a state repeats $random, $urandom and $urandom_range
    st = p.get_randstate();
    a = $random; p.set_randstate(st); b = $random;
    check(a == b, "random after set_randstate");
    p.set_randstate(st);
    a = $urandom; p.set_randstate(st); b = $urandom;
    check(a == b, "urandom after set_randstate");
    p.set_randstate(st);
    a = $urandom_range(1000, 0); p.set_randstate(st); b = $urandom_range(1000, 0);
    check(a == b, "urandom_range after set_randstate");

    // The state after ten draws is the same again
    st = p.get_randstate();
    for (int i = 0; i < 10; i++) void'($urandom);
    a = $urandom;
    p.set_randstate(st);
    for (int i = 0; i < 10; i++) void'($urandom);
    b = $urandom;
    check(a == b, "eleventh draw");

    // The same seed gives the same sequence
    p.srandom(11);
    foreach (seq1[i]) seq1[i] = $urandom;
    p.srandom(11);
    foreach (seq2[i]) seq2[i] = $urandom;
    check(seq1 == seq2, "srandom repeats the sequence");
    p.srandom(12);
    foreach (seq2[i]) seq2[i] = $urandom;
    check(seq1 != seq2, "another seed differs");

    // Objects are seeded by their creator and randomize uses the stream of
    // the process
    p.srandom(5);
    x = new; void'(x.randomize());
    p.srandom(5);
    y = new; void'(y.randomize());
    check(x.a == y.a && x.b == y.b, "object created after the same seed");
    p.srandom(5);
    x = new; y = new;
    void'(x.randomize()); void'(y.randomize());
    check(x.a != y.a, "two objects differ");

    // Saving the state around creating and randomizing an object
    p.srandom(9);
    st = p.get_randstate();
    x = new; void'(x.randomize()); a = x.a;
    void'(x.randomize()); b = x.a;
    p.set_randstate(st);
    y = new; void'(y.randomize());
    check(y.a == a, "restored state repeats the object");
    check(a != b, "successive randomize calls differ");

    // Object state
    x = new;
    st = x.get_randstate();
    void'(x.randomize()); a = x.a;
    x.set_randstate(st);
    void'(x.randomize()); b = x.a;
    check(a == b, "object randstate");

    // A malformed state changes nothing
    p.srandom(3);
    st = p.get_randstate();
    a = $urandom;
    p.set_randstate(st);
    p.set_randstate("garbage");
    b = $urandom;
    check(a == b, "malformed state ignored");

    if (errs == 0) $display("PASSED");
  end
endmodule
