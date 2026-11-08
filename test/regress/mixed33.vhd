library ieee;
use ieee.std_logic_1164.all;

package mixed33_pkg is
    type t_cfg is record
        unused : integer;
        count  : integer;
    end record;

    constant c_cfg : t_cfg := (unused => 7, count => 2);

    type t_rec is record
        valid : std_logic;
        data  : std_logic_vector(3 downto 0);
    end record;

    type t_rec_array is array (natural range <>) of t_rec;

    subtype t_down is t_rec_array(1 downto 0);
    subtype t_up is t_rec_array(0 to 1);

    type t_outer is record
        first : t_rec;
        items : t_down;
    end record;
end package;

-------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use work.mixed33_pkg.all;

entity mixed33_sub is
    generic ( g_cfg : t_cfg := c_cfg );
    port ( rst_n : in std_logic_vector(g_cfg.count - 1 downto 0) := (others => '1');
           en    : in std_logic := '1';
           r_in  : in t_rec := ('1', x"5");
           plain : in std_logic;
           down  : out t_rec_array(g_cfg.count - 1 downto 0);
           up    : out t_rec_array(0 to g_cfg.count - 1);
           o_rec : out t_rec;
           o_arr : out t_rec );
end entity;

architecture test of mixed33_sub is
begin
    down(1) <= (rst_n(1), x"a");
    down(0) <= (rst_n(0), r_in.data);
    up(0)   <= (en, x"1");
    up(1)   <= (r_in.valid, x"2");
    o_rec   <= (plain, x"c");
    o_arr   <= (plain, x"d");
end architecture;
