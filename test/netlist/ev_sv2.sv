// More shapes of Verilog processes
module ev_sv2;
  logic clk, rst, set_n; logic [1:0] op; logic [7:0] a, b, y, z;
  localparam logic [7:0] INIT = 8'hA5;
  logic [7:0] st, acc, r1, r2, r3, bench_q, w;
  logic clk2, gated;
  integer i;

  function automatic [7:0] f(input [7:0] x);
    f = x ^ a;
  endfunction

  task automatic t(input [7:0] x);
    r3 = x + b;
  endtask

  // Reset to a parameter, case and nested blocks
  always @(posedge clk or posedge rst) begin
    if (rst) begin
      st <= INIT;
    end else begin
      case (op)
        2'd0: st <= a;
        2'd1: st <= b;
        default: st <= st;
      endcase
    end
  end

  // Reset and set in one process, else-if chain
  always @(posedge clk or posedge rst or negedge set_n)
    if (rst) acc <= 8'h00;
    else if (!set_n) acc <= 8'hFF;
    else acc <= acc + a;

  // Function call: approximated
  always @(posedge clk) r1 <= f(b);

  // Task call: approximated
  always @(posedge clk) t(a);

  // Loop with a constant bound
  always @(posedge clk) for (i = 0; i < 8; i = i + 1) r2[i] <= a[7 - i];

  // Clock used as data
  always @(posedge clk) gated <= clk & op[0];

  // Bench-like code
  initial begin bench_q = 0; #5 bench_q = 1; end
  always #5 clk2 = ~clk2;
  always @(posedge clk) begin @(posedge clk2) w <= a; end

  assign y = st + acc;
  assign z = {r1[3:0], r2[3:0]};
endmodule
