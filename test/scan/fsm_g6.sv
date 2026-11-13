`define dbg(x)
module g6(input clk, rst, input [3:0] mask, input act, input en, input ready);
  typedef enum {FETCH, LOAD, TRAP} st_e;
  st_e cpu_state;
  always_ff @(posedge clk) begin
    if (rst) cpu_state <= FETCH;
    else case (cpu_state)
      FETCH: begin
        `dbg($display("EBREAK OR UNSUPPORTED INSN AT 0x%08x", mask);)
        if (en && !mask[2] && !act) begin
          cpu_state <= LOAD;
        end else
          cpu_state <= TRAP;
      end
      LOAD: cpu_state <= ready ? FETCH : LOAD;
      TRAP: cpu_state <= TRAP;
    endcase
  end
endmodule

// A port which is declared again as a reg is one machine
module g6b(input clk, rst, go, output [1:0] tstate);
  localparam s_idle = 0, s_a = 1, s_b = 2;
  reg [1:0] tstate;
  always @(posedge clk) begin
    if (rst) tstate <= s_idle;
    else case (tstate)
      s_idle: if (go) tstate <= s_a;
      s_a: tstate <= s_b;
      s_b: tstate <= s_idle;
    endcase
  end
endmodule

// A target which is a name nobody declares is a state of the unknown type
module g6c(input clk, rst, go);
  import pk::*;
  st_e s;
  always_ff @(posedge clk) begin
    if (rst) s <= IDLE;
    else case (s)
      IDLE: if (go) s <= RUN;
      RUN: s <= DONE;
    endcase
  end
endmodule
