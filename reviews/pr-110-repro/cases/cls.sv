class c;
  function void f();
    if (a) begin
      x = 1;
    end else begin
      x = 2;
    end
  endfunction
  task t();
  endtask
endclass
module m;
  generate
    for (genvar i = 0; i < 2; i++) begin : g
      sub u (
        .a(a)
      );
    end
  endgenerate
endmodule
