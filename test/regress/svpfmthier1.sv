// %p of a hierarchical reference to an unpacked struct (IEEE 1800-2017
// 21.2.1.7): the value is printed as an assignment pattern, never an address.
typedef struct { int a; bit [3:0] b; string s; } st_t;
module sub;
  st_t v;
  st_t arr[2];
  initial begin
    v.a = 5; v.b = 4'ha; v.s = "hi";
    arr[1].a = 77;
  end
endmodule
module svpfmthier1;
  sub u();
  int errors = 0;
  function automatic bit has(string txt, string tok);
    for (int k = 0; k + tok.len() <= txt.len(); k++)
      if (txt.substr(k, k + tok.len() - 1) == tok) return 1;
    return 0;
  endfunction
  string t;
  initial begin
    #1;
    t = $sformatf("%p", u.v);
    if (!has(t, "5") || !has(t, "10") || !has(t, "hi")) begin
      $error("u.v printed '%s'", t); errors++;
    end
    t = $sformatf("%p", u.arr[1]);
    if (!has(t, "77")) begin $error("u.arr[1] printed '%s'", t); errors++; end
    t = $sformatf("%p", u.arr);
    if (!has(t, "77")) begin $error("u.arr printed '%s'", t); errors++; end
    if (errors == 0) $display("PASSED");
  end
endmodule
