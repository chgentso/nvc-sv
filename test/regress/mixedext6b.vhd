-- Class mismatch: referencing a Verilog parameter as external signal
entity mixedext6b is
end entity;

architecture tb of mixedext6b is
    component mixedext6b_dut is
    end component;
begin
    u_dut : mixedext6b_dut;

    process is
        alias bad is << signal .mixedext6b.u_dut.DEPTH : integer >>;
    begin
        wait for 1 ns;
        report integer'image(bad);
        wait;
    end process;
end architecture;
