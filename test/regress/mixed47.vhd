library ieee;
use ieee.std_logic_1164.all;

entity mixed47 is
end entity;

architecture test of mixed47 is
    component vm is
        port (a0, a1, ax, az, b1, b0, c1, c0, e1, e0, s1, u1 : out std_logic);
    end component;

    signal v : std_logic_vector(11 downto 0);
begin

    u: vm port map (a0 => v(11), a1 => v(10), ax => v(9), az => v(8),
                    b1 => v(7), b0 => v(6), c1 => v(5), c0 => v(4),
                    e1 => v(3), e0 => v(2), s1 => v(1), u1 => v(0));

    process is
    begin
        wait for 1 ns;
        assert v = "0HUZ1LHLHL1Z" report to_string(v) severity failure;
        wait;
    end process;

end architecture;
