// IEEE 1800-2017 10.9.1 (array assignment patterns), 7.6 (assignment of
// unpacked arrays) and 6.12 (real): an unpacked array of reals takes the
// value of an assignment pattern element by element, in a declaration,
// in a blocking or non-blocking assignment, and whole arrays of reals are
// copied element by element.
module svrealpat1;
  real m[3] = '{0.25, 1.25, 2.25};
  real m2[2][2] = '{'{1.0, 2.0}, '{3.0, 4.0}};
  shortreal sr[2] = '{0.5, 1.5};
  real f[3], h[3];
  int errs;
  `define CK(G, E) if ((G) != (E)) begin $error(`"G = %f, expected %f`", G, E); errs++; end
  task automatic t();
    real a[2] = '{4.5, 5.5};
    real b[2];
    b = '{6.5, 7};
    `CK(a[0], 4.5) `CK(a[1], 5.5) `CK(b[0], 6.5) `CK(b[1], 7.0)
  endtask
  initial begin
    automatic real e[3] = '{0.5, 5.5, 6.5};
    `CK(e[0], 0.5) `CK(e[1], 5.5) `CK(e[2], 6.5)
    `CK(m[0], 0.25) `CK(m[1], 1.25) `CK(m[2], 2.25)
    `CK(m2[0][1], 2.0) `CK(m2[1][0], 3.0)
    `CK(sr[1], 1.5)
    m2 = '{'{5.0, 6.0}, default: 7.0};
    `CK(m2[0][0], 5.0) `CK(m2[0][1], 6.0) `CK(m2[1][0], 7.0) `CK(m2[1][1], 7.0)
    t();
    f = '{1.5, 2.5, 3.5};
    `CK(f[0], 1.5) `CK(f[1], 2.5) `CK(f[2], 3.5)
    h = f;
    `CK(h[0], 1.5) `CK(h[1], 2.5) `CK(h[2], 3.5)
    f = '{default: 9.5};
    `CK(f[0], 9.5) `CK(f[2], 9.5)
    f <= '{9.0, 8.0, 7.0};
    #1;
    `CK(f[0], 9.0) `CK(f[1], 8.0) `CK(f[2], 7.0)
    if (errs == 0) $display("PASSED");
  end
endmodule
