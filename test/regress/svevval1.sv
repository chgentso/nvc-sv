// Value-change event controls on operands without a vector representation:
// class handles, strings, queues, dynamic arrays, unpacked structures (with
// real, string and unpacked array members) and arrays of them; always_comb
// and @* reading members of unpacked structures.  An event occurs when the
// value changes (an assignment of the same value is not an event); for
// always_comb only the members read count (longest static prefix).
// IEEE 1800-2017 9.4.2 (event control), 9.2.2.2.1 (always_comb
// sensitivity), 11.2.
module svevval1;
  class C; int x; endclass
  typedef struct { int a; real r; string s; logic [3:0] b[2]; } st_t;
  C h; string s; int q[$]; int d[]; st_t st; st_t sa[2];
  int nh, ns, nq, nd, nst, nsa, ncomb, nstar;
  int comb_out; int star_out;
  always @(h) nh++;
  always @(s) ns++;
  always @(q) nq++;
  always @(d) nd++;
  always @(st) nst++;
  always @(sa) nsa++;
  always_comb begin comb_out = st.a + st.b[1]; ncomb++; end
  always @* begin star_out = sa[1].a; nstar++; end
  initial begin
    #1 h = new; #1 h = h; #1 h = new;
    #1 s = "a"; #1 s = "a"; #1 s = "b";
    #1 q.push_back(1); #1 q[0] = 2; #1 q[0] = 2;
    #1 d = new[2]; #1 d[1] = 5;
    #1 st.a = 3; #1 st.r = 1.5; #1 st.s = "x"; #1 st.b[1] = 4'h2; #1 st.b[1] = 4'h2;
    #1 sa[1].a = 7; #1 sa[0].s = "q";
    #1;
    $display("%0d %0d %0d %0d %0d %0d %0d %0d %0d %0d", nh, ns, nq, nd, nst, nsa, ncomb, nstar, comb_out, star_out);
    if (nh != 2 || ns != 2 || nq != 2 || nd != 2 || nst != 4 || nsa != 2 || comb_out != 5 || star_out != 7) $fatal(1, "FAILED");
    else $display("PASSED");
  end
endmodule
