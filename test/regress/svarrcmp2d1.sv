// Test equality and inequality comparison of multi-dimensional unpacked arrays

module svarrcmp2d1;
  typedef int arr2d_t[2][2];

  initial begin
    automatic arr2d_t a = '{'{30, 31}, '{10, 11}};
    automatic arr2d_t b = '{'{30, 31}, '{10, 5}};
    automatic arr2d_t c = '{'{30, 31}, '{10, 11}};

    if (!(a == c)) $fatal(1, "expected a == c");
    if (a != c)    $fatal(1, "expected !(a != c)");
    if (a == b)    $fatal(1, "expected !(a == b)");
    if (!(a != b)) $fatal(1, "expected a != b");

    $display("PASSED");
    $finish;
  end
endmodule
