-- Unsupported Verilog unpacked array produces a clean "sorry" message
entity mixedext4b is
end entity;

architecture tb of mixedext4b is
    component mixedext4b_dut is
    end component;
begin
    u_dut : mixedext4b_dut;

    process is
        alias bad is << signal .mixedext4b.u_dut.mem : integer >>;
    begin
        wait for 1 ns;
        report integer'image(bad);
        wait;
    end process;
end architecture;
