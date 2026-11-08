// foreach over a string argument/return variable and a local string inside functions
// Expected values: IEEE 1800-2017 12.7.3 (foreach over a string: indices 0..len-1), 6.16 (string indexing
// and assignment of a byte s[i]), 13.4.1 (the function name variable is a local variable of the function).
module svforeachfn1;
  int errs = 0;
  function string up(string s);
    up = s;
    foreach (up[i]) if (up[i] == "a") up[i] = "A";
    return up;
  endfunction
  function string up2(string s);
    string t; t = s;
    foreach (t[i]) if (t[i] == "a") t[i] = "A";
    return t;
  endfunction
  initial begin
    automatic string s = "banana";
    automatic string r = "", u1, u2;
    foreach (s[i]) r = {r, $sformatf("%s.", s[i])};
    $display("%s", r);
    if (r != "b.a.n.a.n.a.") begin $error("foreach over s: got '%s'", r); errs++; end
    u1 = up("banana"); u2 = up2("banana");
    $display("%s %s", u1, u2);
    if (u1 != "bAnAnA") begin $error("up: got '%s'", u1); errs++; end
    if (u2 != "bAnAnA") begin $error("up2: got '%s'", u2); errs++; end
    if (s != "banana") begin $error("s must be unchanged: '%s'", s); errs++; end
    if (errs == 0) $display("PASSED");
  end
endmodule
