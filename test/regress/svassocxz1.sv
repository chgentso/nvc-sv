// Associative arrays indexed with an index expression that contains x or z.
// Expected values: IEEE 1800-2017 7.8.1 / 7.4.6 (an index with x or z bits is
// invalid: a read yields the default value of the element type, a write has
// no effect, the element does not exist), 7.9.1-7.9.6 (num, delete, exists).
// Applied strictly to the array with a 4-state index type (ab); for the 2-state
// index type (aa) the conversion of an x/z index is not checked.
module svassocxz1;
  int aa[int];
  int ab[logic [3:0]];
  int as[string];
  logic [3:0] k;
  int errors = 0;
  task automatic chk(string what, int got, int exp);
    if (got != exp) begin $error("%s: got %0d expected %0d", what, got, exp); errors++; end
  endtask
  initial begin
    k = 4'bx01x;
    aa[1] = 10;
    chk("num after aa[1]", aa.num(), 1);
    aa[k] = 5;
    // aa has a 2-state int index: how a 4-state expression with x/z is
    // converted to that index type is not pinned down here, only that the
    // element aa[1] is unaffected by the accesses below
    ab[4'b0001] = 1;
    ab[k] = 2;
    chk("ab num", ab.num(), 1);
    chk("ab exists(x)", ab.exists(k), 0);
    k = 4'bzzzz;
    ab[k] = 3;
    chk("ab num after z write", ab.num(), 1);
    chk("ab read z", ab[k], 0);
    ab[k] += 1;
    chk("ab num after z +=", ab.num(), 1);
    ab.delete(k);
    chk("ab num after delete(z)", ab.num(), 1);
    aa[k] += 1;
    aa.delete(k);
    chk("aa exists(1)", aa.exists(1), 1);
    chk("aa[1] untouched", aa[1], 10);
    chk("ab[1] untouched", ab[4'b0001], 1);
    chk("as unused", as.num(), 0);
    if (errors == 0) $display("PASSED");
  end
endmodule
