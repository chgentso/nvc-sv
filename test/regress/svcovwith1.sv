// Self-checking test of `bins x[] = cp with (expr)` where the filter uses `inside` over an array.
// Expected values: IEEE 1800-2017 19.5 (the `with` clause selects those values of the coverpoint's
// domain for which the expression, with `item`, is true; `bins b[]` creates one bin per selected value, a
// scalar bin one bin for all of them), 19.11 (covered bins / bins); one instance each, so the type
// coverage reported by get_coverage equals the instance coverage.
typedef enum bit [3:0] {A0 = 1, A1 = 2, A2 = 4, A3 = 7} ex_t;
module svcovwith1;
  bit [7:0] csr;
  ex_t ex;
  int impl[] = '{3, 5, 9, 200};
  ex_t eimpl[] = '{A1, A3};
  covergroup cg1;
    cp_csr : coverpoint csr {
      bins pcsr[] = cp_csr with (item inside {impl});
    }
  endgroup
  covergroup cg2 with function sample(ex_t e);
    cp_e : coverpoint e {
      bins eb[] = cp_e with (item inside {eimpl});
    }
  endgroup
  covergroup cg3;
    cp_c : coverpoint csr {
      bins low = cp_c with (item < 4);
    }
  endgroup
  cg1 c1; cg2 c2; cg3 c3;
  int fails = 0;
  task automatic chk(string what, real got, real exp);
    if (got < exp - 0.005 || got > exp + 0.005) begin
      fails++; $error("%s: got %0.2f, expected %0.2f", what, got, exp);
    end
  endtask
  initial begin
    c1 = new; c2 = new; c3 = new;
    // cg1: four bins (values 3, 5, 9, 200)
    chk("cg1 initial", c1.get_coverage(), 0.0);
    for (int i = 0; i < impl.size(); i++) begin
      csr = impl[i]; c1.sample();
      chk($sformatf("cg1 after %0d", impl[i]), c1.get_coverage(), 25.0 * (i + 1));
    end
    csr = 7; c1.sample();        // 7 is in no bin
    chk("cg1 value outside", c1.get_coverage(), 100.0);
    // cg2: two bins (values 2 and 7)
    c2.sample(A1);
    chk("cg2 A1", c2.get_coverage(), 50.0);
    c2.sample(A2);               // 4 is not selected
    chk("cg2 A2", c2.get_coverage(), 50.0);
    c2.sample(A3);
    chk("cg2 A3", c2.get_coverage(), 100.0);
    // cg3: one bin holding the values 0..3
    csr = 9; c3.sample();
    chk("cg3 9", c3.get_coverage(), 0.0);
    csr = 2; c3.sample();
    chk("cg3 2", c3.get_coverage(), 100.0);
    if (fails == 0) $display("PASSED");
  end
endmodule
