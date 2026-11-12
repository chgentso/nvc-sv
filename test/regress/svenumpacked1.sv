// An element of a packed array of enumeration values is a value of the
// enumeration: it has its methods and prints by name (IEEE 1800-2017
// 6.19.5 and 7.4.1); the array is still a vector of the elements.
module svenumpacked1;
  typedef enum logic [2:0] {A = 3'd1, B = 3'd2, C = 3'd4} st_e;
  typedef enum bit [1:0] {X0, X1, X2} two_e;
  st_e [3:0] arr = {A, B, C, A};
  two_e [2:0] t2;
  logic [11:0] raw;
  string s;

  initial begin
    if ($bits(arr) != 12 || $bits(t2) != 6) $fatal(1, "widths %0d %0d", $bits(arr), $bits(t2));
    raw = arr;
    if (raw !== 12'h2a1) $fatal(1, "raw %h", raw);
    arr[2] = A;
    if (arr !== 12'h261) $fatal(1, "arr %h", arr);
    t2 = {X2, X1, X0};
    if (t2 !== 6'b10_01_00) $fatal(1, "t2 %b", t2);
    s = t2[2].name();
    if (s != "X2") $fatal(1, "name %s", s);
    s = arr[1].next().name();
    if (s != "A") $fatal(1, "next %s", s);
    if (arr[3].first() != A || arr[3].last() != C || arr[3].num() != 3) $fatal(1, "first/last/num");
    for (int i = 0; i < 4; i++) begin
      s = arr[i].name();
      if (i == 1) begin
        if (s != "C") $fatal(1, "element %0d is %s", i, s);
      end
      else if (s != "A") $fatal(1, "element %0d is %s", i, s);
    end
    arr = {4{B}};
    if (arr != {B, B, B, B}) $fatal(1, "equality");
    if (!(arr[0] inside {A, B})) $fatal(1, "inside");
    case (arr[1])
      B: ;
      default: $fatal(1, "case");
    endcase
    $display("PASSED");
  end
endmodule
