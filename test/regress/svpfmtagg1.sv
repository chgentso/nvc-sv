// %p / %0p applied to unpacked and packed structs, nested structs, arrays and
// strings.
// Expected values: IEEE 1800-2017 21.2.1.7 (%p prints the value in the form of
// an assignment pattern: the members/elements in declaration order, integral
// members as decimal numbers, enumeration members by name, strings and reals
// by value); %0p is the same with no padding.
// The member-name prefixes, quoting, the separators and the form chosen for a
// packed struct (one number or its fields) are implementation-defined and
// not checked; only the presence and the order of the member values is.
typedef enum {A, B} e_t;
typedef struct { int a; bit [3:0] b; string s; } st_t;
typedef struct { int a; int arr[2]; st_t in; } st2_t;
typedef struct packed { bit [3:0] a; bit [3:0] b; } pk_t;
typedef struct { e_t e; real r; pk_t p; } st3_t;
module svpfmtagg1;
  st_t x; st2_t y; st3_t z;
  string s = "hi";
  int errors = 0;
  // the tokens must occur in the text in the given order
  function automatic bit has_ordered(string txt, string toks[$]);
    int pos = 0, found;
    foreach (toks[i]) begin
      found = -1;
      for (int k = pos; k + toks[i].len() <= txt.len(); k++)
        if (txt.substr(k, k + toks[i].len() - 1) == toks[i]) begin found = k; break; end
      if (found < 0) return 0;
      pos = found + toks[i].len();
    end
    return 1;
  endfunction
  function automatic void ordered(string what, string txt, string toks[$]);
    if (!has_ordered(txt, toks)) begin
      $error("%s: tokens not found in order in '%s'", what, txt);
      errors++;
    end
  endfunction
  string t;
  initial begin
    x.a = 5; x.b = 4'ha; x.s = "hi";
    y.a = 1; y.arr[0] = 2; y.arr[1] = 3; y.in = x;
    z.e = B; z.r = 2.5; z.p = 8'h5a;
    t = $sformatf("%p|", x);
    ordered("%p x", t, '{"5", "10", "hi", "|"});
    t = $sformatf("%0p|", x);
    ordered("%0p x", t, '{"5", "10", "hi", "|"});
    t = $sformatf("%0p|%0p|", x, 7);
    ordered("%0p x,7", t, '{"5", "10", "hi", "|", "7", "|"});
    t = $sformatf("%0p|", y);
    ordered("%0p y", t, '{"1", "2", "3", "5", "10", "hi", "|"});
    t = $sformatf("%0p|", z);
    ordered("%0p z", t, '{"B", "2.5", "|"});
    // packed struct: 8'h5a is 90, or its fields 5 and 10
    begin
      string u1, u2;
      u1 = $sformatf("%0p", z.p);
      u2 = $sformatf("%p", z.p);
      if (!has_ordered(u1, '{"90"}) && !has_ordered(u1, '{"5", "10"})) begin
        $error("%%0p of packed struct: '%s'", u1); errors++;
      end
      if (!has_ordered(u2, '{"90"}) && !has_ordered(u2, '{"5", "10"})) begin
        $error("%%p of packed struct: '%s'", u2); errors++;
      end
    end
    t = $sformatf("%0p|%p|", s, s);
    ordered("%0p string", t, '{"hi", "|", "hi", "|"});
    t = $sformatf("%0p", x);
    ordered("sformatf %0p", t, '{"5", "10", "hi"});
    if (errors == 0) $display("PASSED");
  end
endmodule
