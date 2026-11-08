-- Entity with no ports instantiated from Verilog
entity e22 is
end entity;

architecture test of e22 is
begin
    process is
    begin
        wait for 1 ns;
        report "ran " & e22'instance_name;
        wait;
    end process;
end architecture;
