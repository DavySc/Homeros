package main
import "os"
func main(){ b:=make([]byte,1); for { if n,_:=os.Stdin.Read(b); n==0 {return} } }
