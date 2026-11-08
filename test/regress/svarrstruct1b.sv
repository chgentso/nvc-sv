// Self-checking. Expected values: IEEE 1800-2017 7.12.1 (min/max with a with expression; string operands are
// compared with the string comparison operators of 6.16; ties give an unspecified one of the tied elements),
// 7.12.1 (min()/max() without a with clause use the element value itself).
`define CHK(tag, got, exp) if ((got) !== (exp)) begin errs++; $error("%s: got %0d expected %0d", tag, got, exp); end
`define CHKS(tag, got, exp) if ((got) != (exp)) begin errs++; $error("%s: got '%s' expected '%s'", tag, got, exp); end
module svarrstruct1b;
  int errs = 0;
  typedef struct { string name; int age; } person_t;
  person_t people[$];
  person_t mx[$];
  string s[$];
  string r[$];
  initial begin
    people.push_back('{"bob", 30}); people.push_back('{"alice", 25}); people.push_back('{"carol", 35}); people.push_back('{"dave", 25});
    mx = people.min(p) with (p.name); `CHKS("min name", mx[0].name, "alice")
    mx = people.max(p) with (p.name); `CHKS("max name", mx[0].name, "dave")
    s = '{"bob","alice","carol","dave"};
    r = s.max();                       `CHKS("s.max", r[0], "dave")
    r = s.min();                       `CHKS("s.min", r[0], "alice")
    r = s.max(x) with (x);             `CHKS("s.max with x", r[0], "dave")
    r = s.max(x) with (x.len());       `CHK("s.max len size", r.size(), 1)
    `CHK("s.max len tie", (r[0] == "alice") || (r[0] == "carol"), 1'b1)
    if (errs == 0) $display("PASSED");
  end
endmodule
