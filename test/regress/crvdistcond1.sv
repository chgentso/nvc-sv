// Guarded, foreach, and frozen-operand dist constraints
module crvdistcond1;
  class CCond;
    rand bit mode;
    rand bit [7:0] x;
    constraint c {
      if (mode) x dist { 8'd255 := 3, 8'd0 := 1 };
      else      x dist { 8'd0 := 3, 8'd255 := 1 };
    }
  endclass

  class CAlt;
    rand bit sel;
    rand int unsigned x;
    constraint c {
      if (sel) x dist { 5 := 1, 70 := 1 };
      else     x == 4242;
    }
  endclass

  class CForeach;
    rand bit [7:0] a[4];
    constraint c {
      foreach (a[i]) a[i] dist { 8'd10 := 9, 8'd20 := 1 };
    }
  endclass

  class CAffine;
    rand int unsigned x, z;
    constraint c {
      (x + z) dist { 10 := 9, 1000 := 1 };
    }
  endclass

  class CFrozenIdx;
    rand bit [7:0] a[4];
    rand int unsigned idx;
    constraint ci { idx inside {[0:3]}; }
    constraint c {
      a[idx] dist { 8'd10 := 9, 8'd20 := 1 };
    }
  endclass

  initial begin
    CCond cc;
    CAlt ca;
    CForeach cf;
    CAffine af;
    CFrozenIdx fi;
    int m1_hi, m1_n, m0_lo, m0_n;
    int s0, s1_5, s1_70;
    int f10, f20;
    int a10, a1000;
    int i10, i20;

    cc = new;
    ca = new;
    cf = new;
    af = new;
    fi = new;

    // Conditional dist under free guard
    m1_hi = 0; m1_n = 0; m0_lo = 0; m0_n = 0;
    for (int i = 0; i < 400; i++) begin
      if (!cc.randomize()) $fatal(1, "CCond fail");
      if (cc.x != 0 && cc.x != 255) $fatal(1, "CCond val %0d", cc.x);
      if (cc.mode) begin
        m1_n++;
        if (cc.x == 255) m1_hi++;
      end else begin
        m0_n++;
        if (cc.x == 0) m0_lo++;
      end
    end
    if (m1_n == 0 || m0_n == 0 || m1_hi * 2 <= m1_n || m0_lo * 2 <= m0_n)
      $fatal(1, "CCond bias m1=%0d/%0d m0=%0d/%0d", m1_hi, m1_n, m0_lo, m0_n);

    // Guarded dist with non-dist else branch
    s0 = 0; s1_5 = 0; s1_70 = 0;
    for (int i = 0; i < 200; i++) begin
      if (!ca.randomize()) $fatal(1, "CAlt fail");
      if (ca.sel) begin
        if (ca.x == 5) s1_5++;
        else if (ca.x == 70) s1_70++;
        else $fatal(1, "CAlt sel=1 val=%0d", ca.x);
      end else begin
        if (ca.x != 4242) $fatal(1, "CAlt sel=0 val=%0d", ca.x);
        s0++;
      end
    end
    if (s0 == 0 || s1_5 == 0 || s1_70 == 0)
      $fatal(1, "CAlt counts s0=%0d s1_5=%0d s1_70=%0d", s0, s1_5, s1_70);

    // Foreach dist on fixed array
    f10 = 0; f20 = 0;
    for (int i = 0; i < 100; i++) begin
      if (!cf.randomize()) $fatal(1, "CForeach fail");
      foreach (cf.a[j]) begin
        if (cf.a[j] == 10) f10++;
        else if (cf.a[j] == 20) f20++;
        else $fatal(1, "CForeach val %0d", cf.a[j]);
      end
    end
    if (f10 == 0 || f20 == 0 || f10 <= f20)
      $fatal(1, "CForeach bias f10=%0d f20=%0d", f10, f20);

    // Affine dist with frozen partner
    af.x.rand_mode(0);
    af.x = 600;
    a10 = 0; a1000 = 0;
    for (int i = 0; i < 100; i++) begin
      if (!af.randomize()) $fatal(1, "CAffine fail");
      if (af.x + af.z == 10) a10++;
      else if (af.x + af.z == 1000) a1000++;
      else $fatal(1, "CAffine sum %0d", af.x + af.z);
    end
    if (a10 == 0 || a1000 == 0 || a10 <= a1000)
      $fatal(1, "CAffine bias a10=%0d a1000=%0d", a10, a1000);

    // Array element dist with frozen index
    fi.idx.rand_mode(0);
    fi.idx = 2;
    i10 = 0; i20 = 0;
    for (int i = 0; i < 100; i++) begin
      if (!fi.randomize()) $fatal(1, "CFrozenIdx fail");
      if (fi.a[2] == 10) i10++;
      else if (fi.a[2] == 20) i20++;
      else $fatal(1, "CFrozenIdx val %0d", fi.a[2]);
    end
    if (i10 == 0 || i20 == 0 || i10 <= i20)
      $fatal(1, "CFrozenIdx bias i10=%0d i20=%0d", i10, i20);

    $display("PASSED");
  end
endmodule
