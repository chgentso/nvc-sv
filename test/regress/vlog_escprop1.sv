// A class property with an escaped identifier that contains brackets, used and printed with %p
class C;
  int \array_w[ith_es]cape [2];
  int plain = 3;
  function void init;
    \array_w[ith_es]cape [0] = 6;
    \array_w[ith_es]cape [1] = 7;
  endfunction
endclass
module vlog_escprop1;
  C c;
  string s;
  initial begin
    c = new;
    c.init();
    $sformat(s, "%p", c);
    if (c.\array_w[ith_es]cape [0] != 6) $error("value");
    else if (s != "'{array_w[ith_es]cape:'{6, 7}, plain:3}") $error("printed %s", s);
    else $display("PASSED");
  end
endmodule
