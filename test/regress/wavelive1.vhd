-- The FST dump can be read while the simulation runs with --wave-flush: the
-- process waits in simulation time until the script has looked at the dump.
library ieee;
use ieee.std_logic_1164.all;
use std.textio.all;

entity wavelive1 is
end entity;

architecture test of wavelive1 is
    signal count : natural := 0;
    signal flag  : std_logic := '0';
begin

    main: process is
        file f : text;
        variable status : file_open_status;
    begin
        for i in 1 to 3 loop
            wait for 1 ns;
            count <= i;
            flag <= not flag;
        end loop;
        -- keep the simulation going until the script created the file
        loop
            file_open(status, f, "continue.txt", read_mode);
            exit when status = open_ok;
            wait for 1 fs;
        end loop;
        file_close(f);
        wait for 1 ns;
        count <= 99;
        wait;
    end process;

end architecture;
