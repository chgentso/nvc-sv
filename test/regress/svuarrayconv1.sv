// Expected values: IEEE 1800-2017 7.5 / 7.10 / 7.6 (assignment between dynamic arrays and queues),
// 6.11.3 / 6.12 (4-state to 2-state conversion: x and z become 0)
// 7.6 requires equivalent element types (6.22.2), which bit[7:0] and logic[7:0] are not, so this
// runs with --relaxed, where nvc warns and converts each element as in an assignment.
module svuarrayconv1;
  bit [7:0] a[];
  logic [7:0] q[$];
  logic [7:0] d[];
  bit [3:0] bq[$];
  int errs = 0;
  initial begin
    a = new[3];
    a[0] = 8'h12; a[1] = 8'hab; a[2] = 8'hff;
    q = a;
    if (q.size() !== 3 || q[0] !== 8'h12 || q[1] !== 8'hab || q[2] !== 8'hff) begin
      errs++; $error("array->queue: size %0d %h %h %h", q.size(), q[0], q[1], q[2]);
    end
    d = new[2];
    d[0] = 8'h5x; d[1] = 8'h3c;
    a = d;
    if (a.size() !== 2 || a[0] !== 8'h50 || a[1] !== 8'h3c) begin
      errs++; $error("4-state->2-state: size %0d %h %h", a.size(), a[0], a[1]);
    end
    q.push_back(8'hzz);
    a = q;
    if (a.size() !== 4 || a[0] !== 8'h12 || a[1] !== 8'hab || a[2] !== 8'hff || a[3] !== 8'h00) begin
      errs++; $error("queue->array: size %0d a[3]=%h", a.size(), a[3]);
    end
    if (errs == 0) $display("PASSED");
  end
endmodule
