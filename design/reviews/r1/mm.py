import mmap, os
f="/tmp/ne-t2/mm.txt"; open(f,"w").write("original text of the file\n"*1000)
fd=os.open(f,os.O_RDONLY); m=mmap.mmap(fd,0,prot=mmap.PROT_READ,flags=mmap.MAP_PRIVATE)
before=m[:13]
w=os.open(f,os.O_WRONLY); os.write(w,b"CLOBBERED by another program"); os.close(w)   # in-place write, no truncation
print("piece table 'original' bytes before:",before,"after outside in-place write:",m[:13], "(no signal, buffer and undo history silently changed)")
