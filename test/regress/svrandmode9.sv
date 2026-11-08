// rand_mode(0) on a member from inside a method; a local static associative array that stays empty.
// Expected values: IEEE 1800-2017 18.8 (rand_mode: disabled variable is not randomized and keeps
// its value; rand_mode() returns the state; other variables stay active), 7.9.8 (exists on an
// empty associative array returns 0), 18.6.3 (randomize returns 1)
class A;
  rand bit [63:0] value;
  rand bit [7:0] other;
  local static bit m[string];
  static int noex;
  function void f(string access, bit is_rand);
    if (!m.exists(access)) noex++;
    case (access)
      "RO", "RC", "WC": is_rand = 0;
    endcase
    if (!is_rand) value.rand_mode(0);
  endfunction
endclass

module svrandmode9;
  int errs = 0, r;
  initial begin
    automatic A a = new;
    a.value = 5;
    a.f("RO", 1);
    r = a.randomize();
    if (r !== 1) begin errs++; $error("randomize returned %0d", r); end
    if (a.value !== 5 || a.value.rand_mode() !== 0) begin
      errs++; $error("value=%0d rand_mode=%0d", a.value, a.value.rand_mode());
    end
    a.f("RW", 0);
    r = a.randomize();
    if (r !== 1) begin errs++; $error("randomize returned %0d", r); end
    if (a.value !== 5 || a.other.rand_mode() !== 1) begin
      errs++; $error("value=%0d other.rand_mode=%0d", a.value, a.other.rand_mode());
    end
    if (A::noex !== 2) begin errs++; $error("noex=%0d", A::noex); end
    if (errs == 0) $display("PASSED");
  end
endmodule
