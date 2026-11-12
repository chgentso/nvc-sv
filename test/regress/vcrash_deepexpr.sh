set -x

# A very long chain of concatenations is nested deeply in the parse tree
# and must not exhaust the stack.  Start with a small soft limit so the
# chain does not need to be very long to overflow the default one.

awk 'BEGIN {
  print "entity deepexpr is end;"
  print "architecture a of deepexpr is"
  print "  constant c : string := ("
  for (i = 0; i < 3000; i++) print "    \"ab\" &"
  print "    \"x\");"
  print "begin"
  print "  process is begin"
  print "    assert c'"'"'length = 6001;"
  print "    wait;"
  print "  end process;"
  print "end architecture;"
}' > deepexpr.vhd

ulimit -S -s 1024

nvc --std=08 -a deepexpr.vhd -e deepexpr -r
