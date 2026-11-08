// Unpacked array arguments of virtual methods
// Self-checking. Expected values: IEEE 1800-2017 8.20 (virtual methods dispatch on the object's class),
// 13.5 (inout array argument copied in and out), 10.9.1 (array assignment pattern as an actual argument).
`define CHK(tag, got, exp) if ((got) !== (exp)) begin errs++; $error("%s: got %0d expected %0d", tag, got, exp); end
class base;
  virtual function int total(int a [3]);
    return a[0] + a[1] + a[2];
  endfunction
  virtual task bump(inout int a [3]);
    a[0] = a[0] + 1;
  endtask
endclass
class derived extends base;
  virtual function int total(int a [3]);
    return a[0] * a[1] * a[2];
  endfunction
endclass
module svarrarg2;
  int errs = 0;
  int arr [3] = '{2, 3, 4};
  initial begin
    automatic base b = new;
    automatic derived d = new;
    automatic base p = d;
    `CHK("b.total", b.total(arr), 9)
    `CHK("d.total", d.total(arr), 24)
    `CHK("p.total", p.total(arr), 24)
    b.bump(arr);
    `CHK("bump0", arr[0], 3) `CHK("bump1", arr[1], 3) `CHK("bump2", arr[2], 4)
    `CHK("total(pattern)", b.total('{1, 1, 1}), 3)
    if (errs == 0) $display("PASSED");
  end
endmodule
