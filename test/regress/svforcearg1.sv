// A force whose value is an argument of the enclosing function: the value is
// taken when the statement executes (IEEE 1800-2017 10.6.2).
module svforcearg1;
  logic [7:0] sig;
  function static logic [7:0] probe(int kind, logic [7:0] value = '0);
    case (kind)
      1: force sig = value;
      2: release sig;
      default: ;
    endcase
    return sig;
  endfunction
  initial begin
    sig = 8'h01;
    $display("%h", probe(1, 8'h55));
    #1 $display("%h", sig);
    void'(probe(2)); sig = 8'h33;
    #1 $display("%h", sig);
  end
endmodule
