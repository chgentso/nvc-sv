// Expected values: IEEE 1800-2017 6.16 (string methods: compare, len, toupper, tolower,
// substr), 8.5/8.7 (class methods on a returned handle), 7.10 (queue size), 13.4.
// String methods and class methods are called directly on function call results.
class C;
  int v;
  function new(int x); v = x; endfunction
  function int get(); return v; endfunction
  function C self(); return this; endfunction
endclass
module svcallmethod1;
  int errors = 0;
  function string get_id(); return "Abc"; endfunction
  function automatic C mk(int x); C c = new(x); return c; endfunction
  typedef int q_t[$];
  function q_t mkq(); q_t q; q.push_back(1); q.push_back(2); return q; endfunction
  initial begin
    automatic string id = "abc";
    // compare is a character-by-character comparison: "Abc" < "abc" since 'A' (65) < 'a' (97)
    if (!(get_id().compare(id) < 0)) begin $error("compare not negative"); errors++; end
    if (get_id().len() !== 3) begin $error("len = %0d", get_id().len()); errors++; end
    if (get_id().toupper() != "ABC") begin $error("toupper = %s", get_id().toupper()); errors++; end
    if (mk(5).get() !== 5) begin $error("mk(5).get() = %0d", mk(5).get()); errors++; end
    if (mk(6).self().get() !== 6) begin $error("mk(6).self().get() = %0d", mk(6).self().get()); errors++; end
    if (mkq().size() !== 2) begin $error("mkq().size() = %0d", mkq().size()); errors++; end
    if (get_id().tolower() != id) begin $error("tolower mismatch"); errors++; end
    if (get_id().substr(1, 2) != "bc") begin $error("substr = %s", get_id().substr(1, 2)); errors++; end
    if (errors == 0) $display("PASSED");
  end
endmodule
