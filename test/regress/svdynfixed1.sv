// Fixed size unpacked arrays copied to dynamic arrays by new[] and by
// passing them to a dynamic array formal.
// Expected values: IEEE 1800-2017 7.5.1 (new[n](src): the first elements are copied from
// src, the remaining ones get the default value), 7.6, 13.5 (argument passing),
// 8.21 / 8.26 (virtual class with pure virtual method), 13.5.2 (ref arguments).
`define CHK(c, m) if (!(c)) begin err++; $error(m); end
class C;
  byte fx[4] = '{1,2,3,4};
  task automatic go(output byte r[]);
    r = new[6](fx);
  endtask
endclass
typedef longint unsigned uint64_t;

virtual class Acc;
  pure virtual task automatic readm(uint64_t addr[], ref uint64_t data[]);
  task automatic read(uint64_t addr, ref uint64_t data);
    uint64_t aa[1], da[];
    da = new[1];
    aa[0] = addr;
    readm(aa, da);
    data = da[0];
  endtask
endclass

class Impl extends Acc;
  virtual task automatic readm(uint64_t addr[], ref uint64_t data[]);
    foreach (addr[i]) data[i] = addr[i] + 1;
  endtask
endclass

module svdynfixed1;
  Impl acc = new();
  uint64_t d;
  byte tmp[8];
  real rr[3] = '{1.5, 2.5, 3.5};
  bit [3:0] nib[3] = '{1,2,3};
  byte p[], q[], r[];
  real rd[];
  bit [3:0] nd[];
  int i;
  int err = 0;
  int sh_size, sh_first, sh_last;
  task automatic show(byte a[]);
    sh_size = a.size(); sh_first = a[0]; sh_last = a[a.size()-1];
  endtask
  initial begin
    automatic C c = new();
    acc.read(41, d);
    `CHK(d == 42, "d")
    for (i = 0; i < 8; i++) tmp[i] = i + 10;
    p = new[3](tmp);
    q = new[12](tmp);
    `CHK(p.size() == 3 && p[2] == 12, "p")
    `CHK(q.size() == 12 && q[7] == 17, "q")
    `CHK(q[8] == 0 && q[11] == 0, "q tail default")
    show(tmp);
    `CHK(sh_size == 8 && sh_first == 10 && sh_last == 17, "show(tmp)")
    c.go(r);
    `CHK(r.size() == 6 && r[3] == 4 && r[5] == 0 && r[4] == 0, "r")
    nd = new[3](nib);
    `CHK(nd.size() == 3 && nd[2] == 3, "nd")
    if (err == 0) $display("PASSED");
  end
endmodule
