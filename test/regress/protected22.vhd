-- VHDL-2019 variable ports of composite types with protected subelements
-- (LRM 6.5.6.3, 14.3.5).

package protected22_pkg is
    type cnt_t is protected
        procedure add(n : integer);
        impure function get return integer;
    end protected;

    type cnt_arr_t is array (natural range <>) of cnt_t;

    type cnt_rec_t is record
        a : cnt_t;
        b : cnt_t;
    end record;
end package;

package body protected22_pkg is
    type cnt_t is protected body
        variable val : integer := 0;
        procedure add(n : integer) is
        begin
            val := val + n;
        end procedure;
        impure function get return integer is
        begin
            return val;
        end function;
    end protected body;
end package body;

use work.protected22_pkg.all;

entity protected22_sub is
    port (
        variable single : inout cnt_t;
        variable arr    : inout cnt_arr_t(0 to 1);
        variable rec    : inout cnt_rec_t;
        signal   din    : in bit;
        signal   dout   : out bit
    );
end entity;

architecture rtl of protected22_sub is
begin
    dout <= din;

    p : process is
    begin
        single.add(1);
        arr(0).add(2);
        arr(1).add(3);
        rec.a.add(4);
        rec.b.add(5);
        wait;
    end process;
end architecture;

use work.protected22_pkg.all;

entity protected22 is
end entity;

architecture test of protected22 is
    shared variable sv_single : cnt_t;
    shared variable sv_arr    : cnt_arr_t(5 to 6);
    shared variable sv_rec    : cnt_rec_t;
    signal x, y : bit := '0';
begin
    u : entity work.protected22_sub
        port map (
            single => sv_single,
            arr    => sv_arr,
            rec    => sv_rec,
            din    => x,
            dout   => y
        );

    x <= '1';

    chk : process is
    begin
        wait for 1 ns;
        assert sv_single.get = 1;
        assert sv_arr(5).get = 2;
        assert sv_arr(6).get = 3;
        assert sv_rec.a.get = 4;
        assert sv_rec.b.get = 5;
        assert y = '1';
        wait;
    end process;
end architecture;
