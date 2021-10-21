
o2obj.exe: o2obj.c
	gcc -static -o o2obj.exe -O2 o2obj.c
install: \gnu\bin\o2obj.exe
\gnu\bin\o2obj.exe: o2obj.exe
	cp o2obj.exe \gnu\bin\o2obj.exe
.PHONY: clean
clean:
	del *.obj *.exe *~
