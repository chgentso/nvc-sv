module g1(input clk, rst_n, go, fin, input [1:0] mode, input [3:0] bus);
  typedef enum logic [2:0] {A = 0, B = 1, C = 3'd5} st_e;
  st_e st;
  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) st <= A;
    else begin
      unique case (st)
        A: if (go) st <= B; else if (fin) st <= C; else st <= A;
        B, C: begin
           st <= fin ? A : (go ? B : C);
        end
        default: st <= A;
      endcase
    end
  end
endmodule

module g1b(input clk, input [1:0] mode, input [3:0] bus, input go);
  import pk::*;
  st_e s2;
  always @(posedge clk) begin
    case (s2)
      IDLE: begin
        case (mode)
          2'd0: s2 <= RUN;
          default: s2 <= IDLE;
        endcase
      end
      RUN: if ({bus[1:0], bus[3:2]} == 4'b0101) s2 <= DONE; else s2 <= s2;
      DONE: s2 <= nxt(s2);
    endcase
  end
endmodule
