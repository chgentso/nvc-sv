// IEEE 1800-2017 21.2.1.7 leaves the %p text of a class handle to the
// implementation.  nvc prints a handle given directly to %p by its
// properties and a handle inside an array, a queue or an object as the
// class name and the object number, Name#N, which is the number that %0d
// of the handle prints.  A null handle prints as null everywhere.
class C;
  int x;
  C nxt;
endclass

module svpfmtref1;
  C h, a[2], q[$];
  int errors = 0;

  task check(string got, string want);
    if (got != want) begin
      $display("got '%s', expected '%s'", got, want);
      errors++;
    end
  endtask

  initial begin
    string n1, n2;
    h = new;
    h.x = 1;
    h.nxt = new;
    a[0] = h;
    q.push_back(h.nxt);
    q.push_back(null);
    n1 = $sformatf("%0d", h);
    n2 = $sformatf("%0d", h.nxt);
    check($sformatf("%p", h), {"'{x:1, nxt:C#", n2, "}"});
    check($sformatf("%0p", h), {"'{x:1,nxt:C#", n2, "}"});
    check($sformatf("%p", h.nxt), "'{x:0, nxt:null}");
    check($sformatf("%p", a), {"'{C#", n1, ", null}"});
    check($sformatf("%p", q), {"'{C#", n2, ", null}"});
    if (errors == 0) $display("PASSED");
  end
endmodule
