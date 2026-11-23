-- Unsupported VHDL external variable into Verilog produces a clean "sorry"
entity mixedext4c is
end entity;

architecture tb of mixedext4c is
    component mixedext4c_dut is
    end component;
begin
    u_dut : mixedext4c_dut;

    process is
        alias bad is << variable .mixedext4c.u_dut.v : integer >>;
    begin
        wait for 1 ns;
        report integer'image(bad);
        wait;
    end process;
end architecture;
