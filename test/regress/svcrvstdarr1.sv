// A fixed size array passed to std::randomize is solved as a whole: foreach, unique and element
// constraints apply to it, for an array of the module and a property of a class (IEEE 1800-2017 18.12)
class K;
  int arr[6];
  function int run;
    int fails = 0;
    repeat (10) begin
      if (!std::randomize(arr) with { unique {arr}; foreach (arr[i]) arr[i] inside {[1:6]}; }) fails++;
      foreach (arr[i]) foreach (arr[j]) if (i != j && arr[i] == arr[j]) fails++;
    end
    return fails;
  endfunction
endclass
module svcrvstdarr1;
  bit [15:0] w[4];
  logic [7:0] u[3:1];
  int r, fails = 0;
  K k;
  initial begin
    r = std::randomize(w) with { foreach (w[i]) w[i] == i * 3; };
    if (!r || w[0] != 0 || w[1] != 3 || w[2] != 6 || w[3] != 9) begin fails++; $error("w = %p", w); end
    repeat (10) begin
      r = std::randomize(u) with { unique {u}; foreach (u[i]) u[i] < 5; u[2] == 3; };
      if (!r || u[2] != 3 || u[1] == u[3] || u[1] >= 5 || u[3] >= 5 || u[1] == 3 || u[3] == 3) begin
        fails++; $error("u = %p", u);
      end
    end
    k = new;
    fails += k.run();
    if (fails == 0) $display("PASSED");
  end
endmodule
