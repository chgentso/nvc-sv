entity access_reccons is
end entity;

architecture test of access_reccons is
    type frame is record
        data : bit_vector;
        tag  : string;
    end record;
    type frame_ptr is access frame;
    subtype small_ptr is frame_ptr(data(1 to 3), tag(1 to 1));
begin
    process is
        variable p : frame_ptr(data(1 to 3), tag(1 to 1));
        variable q : small_ptr;
    begin
        p := new frame'(data => (1 to 3 => '1'), tag => (1 => 'x'));
        assert p.data'left = 1 and p.data'right = 3 and p.tag'length = 1;
        q := new frame'(data => "101", tag => "y");
        assert q.data = "101" and q.tag = "y";
        wait;
    end process;
end architecture;
