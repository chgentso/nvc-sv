package port2_pkg is
    type bv_array_t is array (natural range <>) of bit_vector(7 downto 0);

    function make_bv(n : natural; w : natural) return bit_vector;
end package;

package body port2_pkg is
    function make_bv(n : natural; w : natural) return bit_vector is
        variable res : bit_vector(w - 1 downto 0) := (others => '0');
        variable v   : natural := n;
    begin
        for i in 0 to w - 1 loop
            if (v mod 2) = 1 then
                res(i) := '1';
            end if;
            v := v / 2;
        end loop;
        return res;
    end function;
end package body;

use work.port2_pkg.all;

entity port2_sub is
    port (
        reg_i : in bv_array_t
    );
end entity;

architecture test of port2_sub is
begin
    check : process is
    begin
        wait for 1 ns;
        assert reg_i'length = 1;
        assert reg_i(0) = x"2a";
        wait;
    end process;
end architecture;

use work.port2_pkg.all;

entity port2 is
end entity;

architecture test of port2 is
    signal val : natural := 42;
begin
    u : entity work.port2_sub
        port map (
            reg_i(0) => make_bv(val, 8)
        );
end architecture;
