set -x

# A very long chain of concatenations is nested deeply in the parse tree
# and must not exhaust the stack

awk 'BEGIN {
  print "entity deepexpr is end;"
  print "architecture a of deepexpr is"
  print "  constant c : string := ("
  for (i = 0; i < 20000; i++) print "    \"ab\" &"
  print "    \"x\");"
  print "begin"
  print "  process is begin"
  print "    assert c'"'"'length = 40001;"
  print "    wait;"
  print "  end process;"
  print "end architecture;"
}' > deepexpr.vhd

nvc --std=08 -a deepexpr.vhd -e deepexpr -r
