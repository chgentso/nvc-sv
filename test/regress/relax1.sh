set -xe

# Errors that --relaxed would downgrade or accept are marked in the JSON
# messages and have a hint in the text

cat >a.vhd <<EOT
entity sub is
    port ( a : in bit_vector := (others => '0') );
end entity;

entity e2 is
    generic ( g : integer := 1 );
end entity;

architecture t of e2 is
    signal x : integer;
begin
    process is
    begin
        case x is
            when g + 1 => null;
            when others => null;
        end case;
        undeclared_name <= 1;
        wait;
    end process;
end architecture;
EOT

cat >b.sv <<EOT
class c;
  function static void f(); endfunction
endclass
EOT

! nvc --std=08 --messages=json -a a.vhd 2>a.json
! nvc --messages=json -a b.sv 2>b.json

[ $(grep -c '"level":"error"' a.json) = 3 ]
[ $(grep -c '"relaxable":true' a.json) = 2 ]
grep '"relaxable":true' a.json | grep -q "index range of array aggregate"
grep '"relaxable":true' a.json | grep -q "case choice must be locally static"
grep -v '"relaxable"' a.json | grep -q "no visible declaration for UNDECLARED_NAME"

[ $(grep -c '"relaxable":true' b.json) = 1 ]
grep '"relaxable":true' b.json | grep -q "cannot have a static lifetime"

! nvc --std=08 -a a.vhd 2>a.txt
[ $(grep -c "option downgrades this to a warning" a.txt) = 1 ]
grep -q "allowed with the --relaxed option" a.txt

# Nothing is marked in the output of the relaxed run
! nvc --std=08 --messages=json -a --relaxed a.vhd 2>r.json
! grep -q relaxable r.json
