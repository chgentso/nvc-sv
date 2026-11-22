interface sub_if (
  input logic clk,
  input logic rst_n
);
  logic q;
  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) q <= 1'b0;
    else        q <= 1'b1;
  end
endinterface

interface src_if;
  typedef struct packed {
    logic [1:0] rst_n;
  } resets_t;
  resets_t resets_o;
endinterface

module svporthier2;
  logic clk = 0;
  always #5 clk = ~clk;

  // Forward hierarchical reference through struct member and bit-select
  // into a sibling interface declared later in the module
  sub_if u_sub (
    .clk   (clk),
    .rst_n (u_src.resets_o.rst_n[1])
  );

  src_if u_src ();

  initial begin
    u_src.resets_o.rst_n = 2'b00;
    @(posedge clk); #1;
    if (u_sub.q !== 1'b0) $fatal(1, "q=%b exp=0", u_sub.q);
    u_src.resets_o.rst_n = 2'b10;
    @(posedge clk); #1;
    if (u_sub.q !== 1'b1) $fatal(1, "q=%b exp=1", u_sub.q);
    $display("PASSED");
    $finish;
  end
endmodule
