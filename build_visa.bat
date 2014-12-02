call "C:\Program Files\Microsoft Visual Studio\VC98\Bin\VCVARS32.BAT"
del gpib_visa.exe
cl /TP -I./visa gpib_visa.c /link ./visa/visa32.lib

del "D:\Program Files\erl6.2\lib\uetest-0.1\priv\gpib_visa.exe"
copy gpib_visa.exe "D:\Program Files\erl6.2\lib\uetest-0.1\priv\"
