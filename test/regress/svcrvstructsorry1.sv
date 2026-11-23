// An array member of an element of an array of structures with the index of
// a foreach is not supported
class c;
  typedef struct { rand int arr[3]; rand int a; } st_t;
  rand st_t s_arr[2];
  constraint k {
    foreach (s_arr[i]) foreach (s_arr[i].arr[j]) s_arr[i].arr[j] < 10;
  }
endclass

module svcrvstructsorry1;
  c o;
  initial begin
    o = new;
    void'(o.randomize());
  end
endmodule
