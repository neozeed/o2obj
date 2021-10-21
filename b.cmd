@del o2obj.exe
gcc -I. -Ignu/os2incl o2obj.c -o o2obj.exe 2> x
grep error: x

o2obj.exe void.o -o void.obj
