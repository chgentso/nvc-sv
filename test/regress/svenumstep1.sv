module svenumstep1;
  typedef enum { A = 10, B = 20, C = 30, D = 40 } e_t;
  typedef int int_t;

  e_t e;
  int n;

  initial begin
    e = A;
    for (n = 0; n < 8; n++) begin
      case (n % 4)
        0: if (e.next(n) !== A) $fatal(1, "next(%0d)=%0d", n, e.next(n));
        1: if (e.next(n) !== B) $fatal(1, "next(%0d)=%0d", n, e.next(n));
        2: if (e.next(n) !== C) $fatal(1, "next(%0d)=%0d", n, e.next(n));
        3: if (e.next(n) !== D) $fatal(1, "next(%0d)=%0d", n, e.next(n));
      endcase
      case (n % 4)
        0: if (e.prev(n) !== A) $fatal(1, "prev(%0d)=%0d", n, e.prev(n));
        1: if (e.prev(n) !== D) $fatal(1, "prev(%0d)=%0d", n, e.prev(n));
        2: if (e.prev(n) !== C) $fatal(1, "prev(%0d)=%0d", n, e.prev(n));
        3: if (e.prev(n) !== B) $fatal(1, "prev(%0d)=%0d", n, e.prev(n));
      endcase
    end
    if (e.next(int_t'(2)) !== C) $fatal(1, "cast next");
    if (e.prev(int_t'(3)) !== B) $fatal(1, "cast prev");
    $display("PASSED");
    $finish;
  end
endmodule
