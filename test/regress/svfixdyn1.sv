// A fixed size array assigned or passed to a dynamic array
// Expected values: IEEE 1800-2017 7.6 (array assignment: unpacked arrays of equal element type, fixed to dynamic
// copies the elements and sizes the dynamic array), 13.5 (argument passing: by value copies, ref aliases), 7.5 (new[]).
module svfixdyn1;
  int errs = 0;
  task automatic t(int a[], ref int d[]);
    foreach (a[i]) d[i] = a[i] + 1;
  endtask
  task automatic chk(bit ok, string m);
    if (!ok) begin $error("%s", m); errs++; end
  endtask
  initial begin
    automatic int aa[3] = '{1,2,3};
    int da[];
    automatic longint la[2] = '{7,8};
    longint ld[];
    da = new[3];
    t(aa, da);
    chk(da.size() == 3 && da[0] == 2 && da[1] == 3 && da[2] == 4, "da after t(aa,da) must be 2 3 4");
    chk(aa[0] == 1 && aa[1] == 2 && aa[2] == 3, "aa must be unchanged");
    ld = la;
    chk(ld.size() == 2 && ld[0] == 7 && ld[1] == 8, "ld = la must copy 7 8");
    t(da, da);
    chk(da.size() == 3 && da[0] == 3 && da[1] == 4 && da[2] == 5, "da after t(da,da) must be 3 4 5");
    if (errs == 0) $display("PASSED");
  end
endmodule
