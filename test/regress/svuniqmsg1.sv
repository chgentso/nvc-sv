// IEEE 1800-2017 12.4.2: a unique-if or unique0-if reports a violation
// when more than one condition matches, and a unique-if or priority-if
// when no condition matches and there is no else.  12.5.3: the same for
// unique-case, unique0-case and priority-case with case items and
// default.  nvc reports these as run-time warnings.
`timescale 1ns/1ns
module svuniqmsg1;
  int x;
  initial begin
    x = 1;
    unique if (x == 1) ; else if (x > 0) ;
    unique0 if (x == 1) ; else if (x > 0) ;
    unique if (x == 2) ; else if (x == 3) ;
    priority if (x == 2) ; else if (x == 3) ;
    unique0 if (x == 2) ; else if (x == 3) ;
    unique case (x) 1: ; 1, 2: ; endcase
    unique0 case (x) 1: ; 1, 2: ; endcase
    unique case (x) 2: ; 3: ; endcase
    priority case (x) 2: ; 3: ; endcase
    priority case (x) 1: ; 1: ; endcase
    unique case (x) 2: ; default: ; endcase
    $display("PASSED");
  end
endmodule
