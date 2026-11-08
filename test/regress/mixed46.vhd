library ieee;
use ieee.std_logic_1164.all;

entity mixed46 is
end entity;

architecture test of mixed46 is
    component vm is
        port (en : in std_logic;
              d  : in std_logic;
              io : inout std_logic_vector(1 downto 0);
              s  : inout std_logic;
              rd : out std_logic_vector(1 downto 0);
              rs : out std_logic);
    end component;

    signal en, d, rs : std_logic := '0';
    signal s : std_logic;
    signal io, rd : std_logic_vector(1 downto 0);
    signal vdrv : std_logic_vector(1 downto 0) := "ZZ";
    signal sdrv : std_logic := 'Z';
begin

    u: vm port map (en, d, io, s, rd, rs);

    io <= vdrv;
    s <= sdrv;

    process is
        procedure check (msg : string; eio : std_logic_vector(1 downto 0);
                         es : std_logic) is
        begin
            assert io = eio and s = es
                report msg & ": io=" & to_string(io) & " s=" & to_string(s)
                severity failure;
        end procedure;

        procedure check (msg : string; eio : std_logic_vector(1 downto 0);
                         es : std_logic; erd : std_logic_vector(1 downto 0);
                         ers : std_logic) is
        begin
            check(msg, eio, es);
            assert rd = erd and rs = ers
                report msg & ": rd=" & to_string(rd) & " rs=" & to_string(rs)
                severity failure;
        end procedure;
    begin
        wait for 1 ns;
        check("a", "ZZ", 'Z', "ZZ", 'Z');
        vdrv <= "10";
        sdrv <= '1';
        wait for 1 ns;
        check("b", "10", '1', "10", '1');
        en <= '1';
        d <= '0';
        wait for 1 ns;
        check("c", "X0", 'X');
        d <= '1';
        wait for 1 ns;
        check("d", "1X", '1');
        vdrv <= "ZZ";
        sdrv <= 'Z';
        wait for 1 ns;
        check("e", "11", '1', "11", '1');
        en <= '0';
        wait for 1 ns;
        check("f", "ZZ", 'Z', "ZZ", 'Z');
        vdrv <= "0Z";
        sdrv <= '0';
        wait for 1 ns;
        check("g", "0Z", '0', "0Z", '0');
        wait;
    end process;

end architecture;
