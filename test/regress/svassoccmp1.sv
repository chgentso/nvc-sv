module svassoccmp1;
  class C;
    int v;
  endclass

  typedef struct {
    int a;
    int b;
  } pair_t;

  initial begin
    automatic int m1[int], m2[int];
    if (!(m1 == m2) || (m1 != m2)) $fatal(1, "empty");

    m1[10] = 1;
    m2[20] = 2;
    if ((m1 == m2) || !(m1 != m2)) $fatal(1, "diff");

    m1[20] = 2;
    m2[10] = 1;
    if (!(m1 == m2) || (m1 != m2)) $fatal(1, "same");

    m1[30] = 0;
    if ((m1 == m2) || !(m1 != m2)) $fatal(1, "len");

    begin
      automatic int s1[string], s2[string];
      s1["foo"] = 7;
      s2["foo"] = 7;
      if (!(s1 == s2)) $fatal(1, "str same");
      s2["foo"] = 8;
      if (s1 == s2) $fatal(1, "str diff");
    end

    begin
      automatic C c1 = new, c2 = new;
      automatic int ck1[C], ck2[C];
      automatic C cv1[int], cv2[int];
      ck1[c1] = 1;
      ck2[c1] = 1;
      if (!(ck1 == ck2)) $fatal(1, "cls key same");
      ck2.delete(c1);
      ck2[c2] = 1;
      if (ck1 == ck2) $fatal(1, "cls key diff");

      cv1[1] = c1;
      cv2[1] = c2;
      if (cv1 == cv2) $fatal(1, "cls val diff");
      cv2[1] = c1;
      if (!(cv1 == cv2)) $fatal(1, "cls val same");
    end

    begin
      automatic pair_t p1[int], p2[int];
      p1[1] = '{1, 2};
      p2[1] = '{1, 2};
      if (p1 !== p2) $fatal(1, "struct same");
      p2[1] = '{1, 3};
      if (p1 === p2) $fatal(1, "struct diff");
    end

    $display("PASSED");
    $finish;
  end
endmodule
