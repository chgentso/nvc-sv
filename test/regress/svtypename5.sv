// IEEE 1800-2017 20.6.1: $typename of an expression returns the name of its
// type, and two types are equivalent iff their type names compare equal
// once whitespace is ignored (6.22.1 / 6.22.2 matching types).
// 11.4.3 / 11.8.1: the result of i ** j has the type of i (j is
// self-determined), so int ** int is int, a 2-state type.
// 11.4.12.1: a replication of 2-state operands is 2-state, like the
// equivalent concatenation.
// 18.13.1 / 18.13.2: $urandom and $urandom_range return int unsigned.
// 20.3.1 / 6.11: $time returns time, a 4-state type.
module svtypename5;
  int i, j;
  bit [7:0] b8;
  logic [7:0] l8;
  int errs;

  function automatic string strip(string s);
    string r = "";
    foreach (s[k]) if (s[k] != " ") r = {r, s[k]};
    return r;
  endfunction

  task automatic chk(string got, string exp);
    if (strip(got) != strip(exp)) begin
      $error("got '%s', expected '%s'", got, exp);
      errs++;
    end
  endtask

  initial begin
    chk($typename(i ** j), $typename(i));
    chk($typename(i ** j), "int");
    chk($typename({2{b8}}), $typename({b8, b8}));
    chk($typename({2{b8}}), "bit[15:0]");
    chk($typename({2{l8}}), "logic[15:0]");
    chk($typename($urandom), "int unsigned");
    chk($typename($urandom_range(3)), "int unsigned");
    chk($typename($time), "time");
    chk($typename($time + 64'd1), "time");
    if (errs == 0) $display("PASSED");
  end
endmodule
