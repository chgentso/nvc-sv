// Streaming into a string drops the zero bytes
// IEEE 1800-2017 6.16 (a string holds no null characters: they are ignored
// when an integral value is assigned to a string), 11.4.14.3 (streaming
// into a string)
module svstreamnul1;
  string u, v;
  bit [31:0] w = 32'h41004243;
  byte b[] = '{8'h41, 8'h00, 8'h42};
  int errs;
  initial begin
    {>>{u}} = w;
    {>>{v}} = b;
    if (u != "ABC" || u.len() != 3) begin $display("FAILED u '%s' %0d", u, u.len()); errs++; end
    if (v != "AB" || v.len() != 2) begin $display("FAILED v '%s' %0d", v, v.len()); errs++; end
    {<<8{u}} = 32'h00414200;
    if (u != "BA") begin $display("FAILED reversed '%s'", u); errs++; end
    if (errs == 0) $display("PASSED");
  end
endmodule
