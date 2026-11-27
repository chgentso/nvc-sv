module sv_sformat_vec1;
  reg [255:0] wide;
  reg [31:0] narrow;
  reg [7:0] one_byte;
  string s;

  initial begin
    $sformat(wide, "hello_%0d", 42);
    if (wide !== "hello_42") $fatal(1, "wide=%0s", wide);

    $sformat(narrow, "ABCDEF");
    if (narrow !== "CDEF") $fatal(1, "narrow=%0s", narrow);

    s = "XYZ";
    one_byte = s;
    if (one_byte !== "Z") $fatal(1, "one_byte=%0s", one_byte);

    FOR: for (int i = 0; i < 1; i++) begin : INNER
      $sformat(wide, "%m");
    end
    if (wide !== "sv_sformat_vec1.FOR.INNER") $fatal(1, "hier=%0s", wide);

    $display("PASSED");
  end
endmodule
