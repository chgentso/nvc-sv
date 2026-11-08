// Expected values: IEEE 1800-2017 7.3.2 (tagged unions with unpacked array and struct members),
// 11.4.5 (equality: tags first, then members), 12.6 (pattern matching), 21.2.1.7 (%p content only)
module svtaggedarr1;
  typedef union tagged { int a; bit [7:0] arr[3]; struct {int x; bit [3:0] y[2];} s; void n; } tu_t;
  tu_t u, v, w;
  bit [7:0] got[3];
  int gq[$];
  int errs;
  int hit;
  logic r1, r2, r3;
  string str;
  initial begin
    u = tagged arr '{1,2,3}; v = tagged arr '{1,2,3}; w = tagged arr '{1,2,4};
    if (u == v) r1 = 1'b1; else r1 = 1'b0;
    if (u == w) r2 = 1'b1; else r2 = 1'b0;
    if (u != w) r3 = 1'b1; else r3 = 1'b0;
    if (r1 !== 1'b1 || r2 !== 1'b0 || r3 !== 1'b1) begin $error("arr eq %b %b %b", r1, r2, r3); errs++; end
    u = tagged a 5; v = tagged a 5; w = tagged n;
    if (u == v) r1 = 1'b1; else r1 = 1'b0;
    if (u == w) r2 = 1'b1; else r2 = 1'b0;
    if (r1 !== 1'b1 || r2 !== 1'b0) begin $error("tag eq %b %b", r1, r2); errs++; end
    u = tagged arr '{7,8,9};
    hit = 0;
    if (u matches tagged arr .x) begin
      hit = 1;
      if (x[0] !== 8'd7 || x[1] !== 8'd8 || x[2] !== 8'd9) begin $error("bound x"); errs++; end
    end
    if (!hit) begin $error("matches tagged arr failed"); errs++; end
    hit = 0;
    case (u) matches
      tagged arr .y: begin
        got = y; hit = 1;
        if (got[0] !== 8'd7 || got[1] !== 8'd8 || got[2] !== 8'd9) begin $error("case y"); errs++; end
      end
      default: ;
    endcase
    if (!hit) begin $error("case matches took default"); errs++; end
    u = tagged s '{3, '{1,2}};
    hit = 0;
    if (u matches tagged s '{.px, .py}) begin
      hit = 1;
      if (px !== 3 || py[0] !== 4'd1 || py[1] !== 4'd2) begin $error("s bindings %0d %0d %0d", px, py[0], py[1]); errs++; end
    end
    if (!hit) begin $error("matches tagged s failed"); errs++; end
    if (u matches tagged arr .zz) begin $error("wrong tag matched"); errs++; end
    str = $sformatf("%p", u);
    // content only: member names and values appear
    if (str.len() == 0) begin $error("empty %%p"); errs++; end
    foreach (str[k]) if (str[k] == "x") hit = 2;
    if (hit != 2) begin $error("%%p lacks member x: %s", str); errs++; end
    if (errs == 0) $display("PASSED");
  end
endmodule
