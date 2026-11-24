module typeref1;
   initial begin
      case (type(real))
         type(int): ;
         1: ;               // Error: non-type compared with type()
         default: ;
      endcase

      case (1)
         type(int): ;       // Error: type() compared with non-type
         default: ;
      endcase
   end
endmodule
