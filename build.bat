del gpib.exe
g++ -fpermissive -o gpib.exe -I .\ni .\ni\gpib-32.obj GPIB.c

del "D:\Program Files\erl6.2\lib\uetest-0.1\priv\gpib.exe"
copy gpib.exe "D:\Program Files\erl6.2\lib\uetest-0.1\priv\"