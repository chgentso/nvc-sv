module svlrm_c18c;
  // Self-checking test. Expected values: IEEE 1800-2017 18.5.2 (a derived-class constraint with the
  // same name overrides the base one; other base constraints still apply), 18.6.2 (pre_randomize and
  // post_randomize are called once per randomize() call), 18.13.3 / 18.13.4 (srandom: re-seeding with
  // the same seed reproduces the same value), 18.13.2 ($urandom_range(max,min); arguments swapped if
  // max < min; single argument gives 0..max), 18.16 (randcase: weight 0 never chosen, selection
  // proportional to weights, checked statistically), 18.17 (randsequence: all productions of an
  // alternation are reachable), 18.13.1 ($urandom is a 32-bit random number). No exact random value
  // is compared.
  class Base;
    rand int x;
    int pre_cnt, post_cnt;
    constraint cb { x inside {[0:100]}; }
    function void pre_randomize(); pre_cnt++; endfunction
    function void post_randomize(); post_cnt++; endfunction
  endclass
  class Derived extends Base;
    rand int y;
    constraint cb { x inside {[50:60]}; }
    constraint cd { y == x + 1; }
  endclass
  Base b; Derived d; int bad;
  int s1, s2;
  int errs;
  function automatic void ck(bit ok, string m);
    if (!ok) begin errs++; $error("%s", m); end
  endfunction
  initial begin
    b = new; d = new;
    repeat (50) begin void'(b.randomize()); if (b.x < 0 || b.x > 100) bad++; end
    repeat (50) begin void'(d.randomize()); if (d.x < 50 || d.x > 60 || d.y != d.x + 1) bad++; end
    ck(bad == 0, "base and overriding constraints");
    ck(b.pre_cnt == 50 && b.post_cnt == 50 && d.pre_cnt == 50 && d.post_cnt == 50, "pre/post_randomize call counts");
    b = new; d = new;
    b.srandom(42); d.srandom(42);
    void'(b.randomize()); s1 = b.x; b.srandom(42); void'(b.randomize()); s2 = b.x;
    ck(s1 == s2, "srandom reproducibility");
    s1 = $urandom_range(10, 20); ck(s1 >= 10 && s1 <= 20, "urandom_range(10,20)");
    s1 = $urandom_range(5); ck(s1 >= 0 && s1 <= 5, "urandom_range(5)");
    s1 = $urandom_range(3, 3); ck(s1 == 3, "urandom_range(3,3)");
    s1 = $urandom_range(20, 10); ck(s1 >= 10 && s1 <= 20, "urandom_range(20,10)");
    begin int cnt[3]; repeat (300) begin randcase 1: cnt[0]++; 2: cnt[1]++; 3: cnt[2]++; endcase end
      ck(cnt[0] + cnt[1] + cnt[2] == 300, "randcase picks exactly one item per execution");
      ck(cnt[0] > 0 && cnt[1] > 0 && cnt[2] > 0 && cnt[2] > cnt[0], "randcase weights 1:2:3"); end
    begin int x; randcase 0: x = 1; 5: x = 2; endcase ck(x == 2, "randcase weight 0 never chosen"); end
    begin int seen[4]; repeat (200) randsequence (main) main: a | b | c; a: { seen[0]++; }; b: { seen[1]++; }; c: { seen[2]++; }; endsequence
      ck(seen[0] > 0 && seen[1] > 0 && seen[2] > 0, "randsequence alternatives"); end
    begin int r[16]; automatic bit differ = 0; foreach (r[k]) r[k] = $urandom; foreach (r[k]) if (r[k] != r[0]) differ = 1;
      ck(differ, "$urandom is not constant"); end
    begin process p; end
    if (errs == 0) $display("PASSED");
  end
endmodule
