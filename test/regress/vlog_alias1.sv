module vlog_alias_sub (
  input logic [7:0] din
);
  wire [7:0] a, b;
  assign a = din;
  alias a = b;

  wire [1:0] p, q;
  alias p = q;
endmodule

module vlog_alias1;
  wire [15:0] a, b, c;
  wire [15:0] x, y;
  wire [15:0] f1, f2;
  logic [15:0] val = 16'h1234;

  alias a = b = c;
  assign a = val;

  alias x = y;
  assign x[7:0] = val[7:0];
  assign y[15:8] = val[15:8];

  alias f1 = f2;

  vlog_alias_sub u_sub (.din(val[7:0]));
  assign u_sub.p[0] = 1'b0;
  assign u_sub.q[1] = 1'b1;

  initial begin
    #1;
    if (a !== 16'h1234 || b !== 16'h1234 || c !== 16'h1234)
      $fatal(1, "transitive alias mismatch: %h %h %h", a, b, c);
    if (x !== 16'h1234 || y !== 16'h1234)
      $fatal(1, "split-driver alias mismatch: %h %h", x, y);
    if (u_sub.a !== 8'h34 || u_sub.b !== 8'h34)
      $fatal(1, "sub alias mismatch: %h %h", u_sub.a, u_sub.b);
    if (u_sub.p !== 2'b10 || u_sub.q !== 2'b10)
      $fatal(1, "sub select alias mismatch: %b %b", u_sub.p, u_sub.q);

    val = 16'hbeef;
    #1;
    if (b !== 16'hbeef || c !== 16'hbeef || x !== 16'hbeef || y !== 16'hbeef)
      $fatal(1, "updated alias mismatch");

    force f1 = 16'h5a5a;
    #0;
    if (f1 !== 16'h5a5a || f2 !== 16'h5a5a)
      $fatal(1, "force f1 mismatch: %h %h", f1, f2);
    release f1;
    #0;

    force f2 = 16'ha5a5;
    #0;
    if (f1 !== 16'ha5a5 || f2 !== 16'ha5a5)
      $fatal(1, "force f2 mismatch: %h %h", f1, f2);
    release f2;

    $display("PASSED");
    $finish;
  end
endmodule
