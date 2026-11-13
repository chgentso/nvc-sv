package visibility24_pkg is
    type work is (foo, bar);
end package;

use work.visibility24_pkg.all;

entity visibility24 is
end entity;

architecture test of visibility24 is
    -- The library name is directly visible: the type WORK made potentially
    -- visible by the use clause does not hide it
    use work.visibility24_pkg;
    constant c : work.visibility24_pkg.work := bar;
begin
    p: process is
        variable v : work.visibility24_pkg.work;
    begin
        v := foo;
        assert v = foo;
        assert c = bar;
        wait;
    end process;
end architecture;
