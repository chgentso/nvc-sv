module svqbound1;
  integer qi[$:1];
  real qr[$:1];
  string qs[$:1];
  integer calls = 0;

  function automatic integer touch_i;
    calls = calls + 1;
    return 99;
  endfunction

  function automatic real touch_r;
    calls = calls + 1;
    return 9.5;
  endfunction

  function automatic string touch_s;
    calls = calls + 1;
    return "extra";
  endfunction

  initial begin
    qi = '{11, 22, touch_i(), touch_i()};
    if (calls !== 2 || qi.size() !== 2 || qi[0] !== 11 || qi[1] !== 22)
      $fatal(1, "qi pattern mismatch: calls=%0d size=%0d", calls, qi.size());

    qi = {33, 44, touch_i()};
    if (calls !== 3 || qi.size() !== 2 || qi[0] !== 33 || qi[1] !== 44)
      $fatal(1, "qi concat mismatch: calls=%0d size=%0d", calls, qi.size());

    qi.push_back(55);
    if (qi.size() !== 2 || qi[0] !== 33 || qi[1] !== 44)
      $fatal(1, "qi push_back mismatch");

    qi.push_front(55);
    if (qi.size() !== 2 || qi[0] !== 55 || qi[1] !== 33)
      $fatal(1, "qi push_front mismatch");

    qr = '{1.5, 2.5, touch_r(), touch_r()};
    if (calls !== 5 || qr.size() !== 2 || qr[0] != 1.5 || qr[1] != 2.5)
      $fatal(1, "qr pattern mismatch");

    qr = {3.5, 4.5, touch_r()};
    if (calls !== 6 || qr.size() !== 2 || qr[0] != 3.5 || qr[1] != 4.5)
      $fatal(1, "qr concat mismatch");

    qs = '{"a", "b", touch_s(), touch_s()};
    if (calls !== 8 || qs.size() !== 2 || qs[0] != "a" || qs[1] != "b")
      $fatal(1, "qs pattern mismatch");

    qs = {"c", "d", touch_s()};
    if (calls !== 9 || qs.size() !== 2 || qs[0] != "c" || qs[1] != "d")
      $fatal(1, "qs concat mismatch");

    $display("PASSED");
  end
endmodule
