typedef struct packed {
  logic       d_valid;
  logic [7:0] d_data;
  logic       a_ready;
} bus_d2h_t;

interface svcbstruct1_if (input logic clk);
  bus_d2h_t d2h_int;

  clocking device_cb @(posedge clk);
    output d2h = d2h_int;
  endclocking
endinterface

module svcbstruct1;
  logic clk = 0;
  always #5 clk = ~clk;

  svcbstruct1_if u_if (.clk(clk));

  initial begin
    u_if.d2h_int.d_valid <= 1'b0;
    u_if.d2h_int.d_data  <= 8'hA5;
    u_if.d2h_int.a_ready <= 1'b0;

    @(u_if.device_cb);
    u_if.device_cb.d2h.a_ready <= 1'b1;
    @(u_if.device_cb);
    #1;
    if (u_if.d2h_int.a_ready !== 1'b1 || u_if.d2h_int.d_valid !== 1'b0
        || u_if.d2h_int.d_data !== 8'hA5)
      $fatal(1, "FAIL1: valid=%b data=%h ready=%b",
             u_if.d2h_int.d_valid, u_if.d2h_int.d_data, u_if.d2h_int.a_ready);

    u_if.d2h_int.d_valid <= 1'b1;
    u_if.device_cb.d2h.a_ready <= 1'b0;
    @(u_if.device_cb);
    #1;
    if (u_if.d2h_int.a_ready !== 1'b0 || u_if.d2h_int.d_valid !== 1'b1
        || u_if.d2h_int.d_data !== 8'hA5)
      $fatal(1, "FAIL2: valid=%b data=%h ready=%b",
             u_if.d2h_int.d_valid, u_if.d2h_int.d_data, u_if.d2h_int.a_ready);

    $display("PASSED");
    $finish;
  end
endmodule
