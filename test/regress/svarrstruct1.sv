// Self-checking. Expected values: IEEE 1800-2017 7.12.1 (min/max return a queue with one element holding the
// minimum/maximum of the with expression; when several elements tie, which one is returned is unspecified;
// unique/unique_index return one entry per distinct key, in unspecified order and with an unspecified
// representative; find_index returns indices in array order; find_last returns the last match),
// 7.12.3 (sum with), 6.16 (string comparison).
`define CHK(tag, got, exp) if ((got) !== (exp)) begin errs++; $error("%s: got %0d expected %0d", tag, got, exp); end
`define CHKS(tag, got, exp) if ((got) != (exp)) begin errs++; $error("%s: got '%s' expected '%s'", tag, got, exp); end
module svarrstruct1;
  int errs = 0;
  typedef struct { string name; int age; } person_t;
  person_t people[$];
  person_t mx[$], mn[$], un[$];
  int ix[$], ages[$];
  initial begin
    people.push_back('{"bob", 30}); people.push_back('{"alice", 25}); people.push_back('{"carol", 35}); people.push_back('{"dave", 25});
    mx = people.max(p) with (p.age);
    `CHKS("max age name", mx[0].name, "carol") `CHK("max age size", mx.size(), 1)
    mn = people.min(p) with (p.age);
    `CHK("min age size", mn.size(), 1) `CHK("min age", mn[0].age, 25)
    `CHK("min age name", (mn[0].name == "alice") || (mn[0].name == "dave"), 1'b1)
    mx = people.max(p) with (p.name);
    `CHKS("max name", mx[0].name, "dave")
    un = people.unique(p) with (p.age);
    `CHK("unique size", un.size(), 3)
    ages.delete();
    foreach (un[i]) begin
      ages.push_back(un[i].age);
      if (un[i].age == 25) `CHK("unique 25 rep", (un[i].name == "alice") || (un[i].name == "dave"), 1'b1)
      if (un[i].age == 30) `CHKS("unique 30 rep", un[i].name, "bob")
      if (un[i].age == 35) `CHKS("unique 35 rep", un[i].name, "carol")
    end
    ages.sort;
    `CHK("unique ages[0]", ages[0], 25) `CHK("unique ages[1]", ages[1], 30) `CHK("unique ages[2]", ages[2], 35)
    ix = people.unique_index(p) with (p.age);
    `CHK("unique_index size", ix.size(), 3)
    ages.delete();
    foreach (ix[i]) begin
      `CHK("unique_index range", (ix[i] >= 0) && (ix[i] < people.size()), 1'b1)
      ages.push_back(people[ix[i]].age);
    end
    ages.sort;
    `CHK("unique_index ages[0]", ages[0], 25) `CHK("unique_index ages[1]", ages[1], 30) `CHK("unique_index ages[2]", ages[2], 35)
    `CHK("sum age", people.sum(p) with (p.age), 115)
    `CHK("sum cond", people.sum(p) with (p.age > 26 ? 1 : 0), 2)
    ix = people.find_index(p) with (p.age == 25);
    `CHK("find_index size", ix.size(), 2) `CHK("find_index[0]", ix[0], 1) `CHK("find_index[1]", ix[1], 3)
    mx = people.find_last(p) with (p.age == 25);
    `CHKS("find_last", mx[0].name, "dave")
    if (errs == 0) $display("PASSED");
  end
endmodule
