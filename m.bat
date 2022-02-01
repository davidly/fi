del fi.exe
del fi.pdb
cl /nologo fi.cxx /I.\ /EHac /Zi /O2i /Gy /D_CRT_NON_CONFORMING_SWPRINTFS /D_CRT_SECURE_NO_DEPRECATE  /D_AMD64_ /link ntdll.lib /OPT:REF

