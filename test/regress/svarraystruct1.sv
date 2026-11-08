// Self-checking. Expected values: IEEE 1800-2017 7.2 (structures), 7.4 (unpacked arrays of structs and of
// strings in classes), 7.6 (array copy), 10.9.2 (struct assignment patterns, positional and named), 13.5 (by-value
// and ref array arguments), 20.7 ($size), 6.8 (an uninitialized 2-state member is 0).
`define CHK(tag, got, exp) if ((got) !== (exp)) begin errs++; $error("%s: got %0d expected %0d", tag, got, exp); end
`define CHKS(tag, got, exp) if ((got) != (exp)) begin errs++; $error("%s: got %s expected %s", tag, got, exp); end
typedef struct { int x; logic [7:0] y; } d_t;
class C;
  d_t arr[3];
  string tags[2];
  function void set(int i, int v); arr[i].x = v; tags[i%2] = $sformatf("t%0d", v); endfunction
endclass
module svarraystruct1;
  int errs = 0;
  d_t a[3], b[3];
  d_t pat[2] = '{'{1, 8'h2}, '{3, 8'h4}};
  d_t pat2[2];
  d_t pat3[2][2];
  function automatic int sum(d_t v[3]);
    int s = 0;
    foreach (v[i]) s += v[i].x;
    return s;
  endfunction
  task automatic bump(ref d_t v[3]);
    foreach (v[i]) v[i].x++;
  endtask
  initial begin
    automatic C c = new;
    c.set(0, 4); c.set(1, 5);
    `CHK("c.arr[0].x", c.arr[0].x, 4) `CHK("c.arr[1].x", c.arr[1].x, 5)
    `CHKS("tags[0]", c.tags[0], "t4") `CHKS("tags[1]", c.tags[1], "t5")
    a[0].x = 1; a[1].x = 2; a[2].x = 3;
    `CHK("sum", sum(a), 6)
    b = a; b[0].x = 100;
    `CHK("a[0] after copy", a[0].x, 1) `CHK("b[0]", b[0].x, 100)
    bump(a);
    `CHK("bump0", a[0].x, 2) `CHK("bump1", a[1].x, 3) `CHK("bump2", a[2].x, 4)
    `CHK("pat[0].x", pat[0].x, 1) `CHK("pat[0].y", pat[0].y, 8'h02)
    `CHK("pat[1].x", pat[1].x, 3) `CHK("pat[1].y", pat[1].y, 8'h04)
    pat2 = '{'{x: 5, y: 8'h6}, '{x: 7, y: 8'h8}};
    `CHK("pat2[0].x", pat2[0].x, 5) `CHK("pat2[1].y", pat2[1].y, 8'h08)
    `CHK("pat2[0].y", pat2[0].y, 8'h06) `CHK("pat2[1].x", pat2[1].x, 7)
    pat3[1][0].x = 9;
    `CHK("pat3[1][0].x", pat3[1][0].x, 9) `CHK("pat3[0][1].x", pat3[0][1].x, 0)
    `CHK("$size", $size(a), 3)
    if (errs == 0) $display("PASSED");
  end
endmodule
