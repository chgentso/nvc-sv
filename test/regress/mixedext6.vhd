-- Type mismatch between VHDL external constant and Verilog parameter names both types
entity mixedext6 is
end entity;

architecture tb of mixedext6 is
    component mixedext6_dut is
    end component;
begin
    u_dut : mixedext6_dut;

    process is
        alias bad is << constant .mixedext6.u_dut.GAIN : integer >>;
    begin
        wait for 1 ns;
        report integer'image(bad);
        wait;
    end process;
end architecture;
