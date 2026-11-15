set -xe

# The pins of a cell follow the order the nets were declared in, not
# their names or addresses, so output is the same from one build to the next

cat >ord.vhd <<EOT
entity ord is
end entity;

architecture a of ord is
    signal zq, mm, ba, ab : bit;
    signal z : bit;
begin
    p: process (ab, ba, mm, zq) is
    begin
        z <= ab xor ba xor mm xor zq;
    end process;
end architecture;
EOT

nvc -a ord.vhd -e ord
nvc --netlist ord --output=out1.json
nvc --netlist ord --evidence --output=out2.json

for f in out1.json out2.json; do
    grep -o '^"\(ZQ\|MM\|BA\|AB\|Z\)":"' $f | head -5 | tr -d '":\n' > order.txt
    [ "$(cat order.txt)" = "ZQMMBAABZ" ]
done
