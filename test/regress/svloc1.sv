// Array locator methods on arrays of strings and structures
typedef struct { int id; string name; } rec_t;

module svloc1;
   `define CHECK(x, y) if ((x) !== (y)) begin \
      $display("FAILED line %0d: %0d !== %0d", `__LINE__, (x), (y)); \
      $finish; end

   string s[] = { "hello", "sad", "hello", "world" };
   string qs[$];
   int qi[$];
   rec_t r[$];
   rec_t found[$];
   rec_t t;

   initial begin
      qs = s.find with (item == "hello");
      `CHECK(qs.size(), 2);
      `CHECK(qs[1], "hello");

      qi = s.find_index with (item == "sad");
      `CHECK(qi.size(), 1);
      `CHECK(qi[0], 1);

      qi = s.find_first_index with (item == "hello");
      `CHECK(qi[0], 0);
      qi = s.find_last_index with (item == "hello");
      `CHECK(qi[0], 2);

      qs = s.find_first with (item.len() > 4 && item != "hello");
      `CHECK(qs.size(), 1);
      `CHECK(qs[0], "world");

      qs = s.find with (item == "nothing");
      `CHECK(qs.size(), 0);

      t = '{ 1, "one" };
      r.push_back(t);
      t = '{ 2, "two" };
      r.push_back(t);
      t = '{ 3, "three" };
      r.push_back(t);
      found = r.find with (item.id > 1);
      `CHECK(found.size(), 2);
      `CHECK(found[0].name, "two");
      qi = r.find_index with (item.name == "three");
      `CHECK(qi[0], 2);
      $display("PASSED");
   end
endmodule
