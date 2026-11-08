// rand array of object handles with a null element and a foreach constraint dereferencing it.
// Expected values: IEEE 1800-2017 18.4 (rand object handles: members and constraints of the
// objects are solved together), 18.5.3 (foreach), 18.6.3 (randomize returns 0 on failure and the
// variables then keep their values), 18.5.11 (constraint guards: an unguarded null dereference is
// not defined to succeed).
// The LRM does not fix whether randomize() fails or skips the null element in the first call;
// both outcomes are accepted, but each must be self-consistent. After every element exists,
// the result is fully determined (v inside {[10:20]} and v > 15).
class item;
  rand bit [7:0] v;
  constraint c { v inside {[10:20]}; }
endclass
class holder;
  rand item arr[3];
  constraint cq { foreach (arr[i]) arr[i].v > 15; }
  function new();
    arr[0] = new; arr[2] = new;
  endfunction
endclass
module svrandnull2;
  holder h;
  int r, errs = 0;
  initial begin
    h = new;
    r = h.randomize();
    if (r === 1) begin
      if (!(h.arr[0].v inside {[16:20]}) || !(h.arr[2].v inside {[16:20]})) begin
        errs++; $error("success but a0=%0d a2=%0d", h.arr[0].v, h.arr[2].v);
      end
    end else if (r === 0) begin
      if (h.arr[0].v !== 0 || h.arr[2].v !== 0) begin
        errs++; $error("failure but values changed a0=%0d a2=%0d", h.arr[0].v, h.arr[2].v);
      end
    end else begin errs++; $error("randomize returned %0d", r); end
    h.arr[1] = new;
    r = h.randomize();
    if (r !== 1) begin errs++; $error("second randomize returned %0d", r); end
    foreach (h.arr[i])
      if (!(h.arr[i].v inside {[16:20]})) begin errs++; $error("arr[%0d].v=%0d", i, h.arr[i].v); end
    if (errs == 0) $display("PASSED");
  end
endmodule
