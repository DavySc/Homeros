`include "a.svh"
`include "b.svh"
`include "c.svh"
module m;
  always_ff @(posedge clk) begin
    a <= b;
  end
endmodule
`include "d.svh"
