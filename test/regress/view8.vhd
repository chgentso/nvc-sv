-- Element mode view indications for record and array elements inside a
-- record mode view (IEEE 1076-2019 6.5.2).

package view8_pkg is
    type leaf_t is record
        req : bit;
        val : integer;
    end record;

    type leaf_vec_t is array (natural range <>) of leaf_t;

    view leaf_v of leaf_t is
        req : in;
        val : out;
    end view;

    type top_t is record
        single : leaf_t;
        arr    : leaf_vec_t(0 to 1);
        en     : bit;
    end record;

    view top_v of top_t is
        single : view leaf_v;
        arr    : view (leaf_v);
        en     : in;
    end view;
end package;

use work.view8_pkg.all;

entity view8_sub is
    port (p : view top_v);
end entity;

architecture rtl of view8_sub is
begin
    p.single.val <= 10 when p.single.req = '1' else 0;
    p.arr(0).val <= 20 when p.arr(0).req = '1' else 0;
    p.arr(1).val <= 30 when p.en = '1' else 0;
end architecture;

use work.view8_pkg.all;

entity view8 is
end entity;

architecture test of view8 is
    signal s : top_t;
begin
    dut: entity work.view8_sub
        port map (p => s);

    stim: process is
    begin
        s.single.req <= '1';
        s.arr(0).req <= '1';
        s.en <= '1';
        wait for 1 ns;
        assert s.single.val = 10;
        assert s.arr(0).val = 20;
        assert s.arr(1).val = 30;
        wait;
    end process;
end architecture;
