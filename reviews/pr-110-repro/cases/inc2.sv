`include "a.svh"
`include "b.svh"

`include "c.svh"
`include "d.svh"
package p;
  import a::*;
  import b::*;
endpackage
`define A(x) \
  x + \
  1
