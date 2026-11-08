library ieee;
use ieee.std_logic_1164.all;

entity mixed32_sub is
    generic ( W : integer := 6 );
    port ( i_port : in std_logic;
           o_port : out std_logic_vector(3 downto 0) );
end entity;

architecture rtl of mixed32_sub is
    signal b      : std_logic;
    signal b_seen : std_logic := '0';
    signal v      : std_logic_vector(7 downto 0);
    signal v_copy : std_logic_vector(7 downto 0);
    signal up     : std_logic_vector(0 to 3);
    signal g      : std_logic_vector(W - 1 downto 0);
    signal h      : std_logic := 'H';
    signal l      : std_logic := 'L';
    signal u      : std_logic;
    signal res    : std_logic := 'Z';
    signal vres   : std_logic_vector(3 downto 0) := "ZZZZ";
    signal res_in : std_logic := 'Z';
    signal clk    : std_logic := '0';
    signal driven : std_logic := '0';
    signal go     : std_logic := '0';
begin

    b_seen <= '1' when b = '1' else '0';
    v_copy <= v;
    res <= res_in;
    clk <= not clk after 5 ns when now < 100 ns;
    o_port <= "1010";

    p2: process is
    begin
        wait for 70 ns;
        driven <= '1';
        wait for 1 ns;
        driven <= '0';
        wait;
    end process;

    p1: process is
    begin
        wait until go = '1';
        assert up = "1001" report "up" severity failure;
        assert up(0) = '1' and up(1) = '0' report "up index" severity failure;
        assert g = "110011" report "g" severity failure;
        assert i_port = '1' report "port" severity failure;
        res_in <= '1';
        wait;
    end process;

end architecture;
