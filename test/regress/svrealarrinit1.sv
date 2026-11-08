// Expected values: IEEE 1800-2017 7.9/10.9.1 (array assignment patterns, default:), 7.4.6 (oob write ignored, oob read
// gives the default value of the element type), 8.12 (shallow copy: unpacked array members are copied by value),
// 12.7.3 (foreach), 11.4.3 (arithmetic)
module svrealarrinit1;
  int err = 0;
  function automatic void chk(bit ok, string m);
    if (!ok) begin $error("%s", m); err++; end
  endfunction
  class K;
    real r2[2] = '{1.5, 2.5};
    real r3[3] = '{default: 0.5};
    string s2[2] = '{"a", "bc"};
    int q[3] = '{1, 2, 3};
  endclass
  function automatic void f();
    real l[2] = '{3.25, 4.5};
    string t[2] = '{"x", "yz"};
    chk(l[0] == 3.25 && l[1] == 4.5 && t[0] == "x" && t[1] == "yz", "local init");
  endfunction
  function automatic real sumr();
    real a[4]; real t = 0;
    for (int i = 0; i < 4; i++) a[i] = i * 1.5;
    foreach (a[i]) t += a[i];
    a[2] += 10; a[3]++;
    return t + a[2] + a[3];
  endfunction
  function automatic real grid();
    real g[2][3]; real t = 0;
    for (int i = 0; i < 2; i++) for (int j = 0; j < 3; j++) g[i][j] = i * 10 + j;
    return g[1][2] + g[0][1] * 100;
  endfunction
  function automatic real oob(int i);
    real a[3]; real t;
    a[0] = 1.0; a[1] = 2.0; a[2] = 3.0;
    a[i] = 99.0;
    t = a[i] + a[0] + a[1] + a[2];
    return t;
  endfunction
  K k;
  initial begin
    k = new;
    chk(k.r2[0] == 1.5 && k.r2[1] == 2.5 && k.r3[2] == 0.5 && k.r3[0] == 0.5 && k.r3[1] == 0.5, "real members");
    chk(k.s2[0] == "a" && k.s2[1] == "bc" && k.q[2] == 3 && k.q[0] == 1, "string/int members");
    f();
    chk(oob(1) == 202.0, "oob(1)");
    chk(oob(5) == 6.0, "oob(5): write ignored, read gives 0.0");
    chk(oob(-1) == 6.0, "oob(-1)");
    chk(sumr() == 27.5, "sumr");
    chk(grid() == 112.0, "grid");
    begin K k2; k2 = new k; k2.r2[1] = 9.5; k2.s2[0] = "q";
      chk(k.r2[1] == 2.5 && k.s2[0] == "a", "shallow copy shares array storage");
      chk(k2.r2[0] == 1.5 && k2.r2[1] == 9.5 && k2.s2[0] == "q" && k2.s2[1] == "bc" && k2.q[2] == 3, "copy contents");
    end
    if (err == 0) $display("PASSED");
  end
endmodule
