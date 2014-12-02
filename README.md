KissGPIO
========

A GPIB client based on KISS principle.

Input an string and press Enter to write it to GPIB device. 
Press Enter directly (an empty input) to read device's response.  

Use -? to get help on command line options.

There are two implementations.

#### Classic

GPIB.c uses NI's classic APIs, ibrd, ibwrt, etc. GCC can be used to build this.

```
 GPIB client command options:
     -port               as an Erlang port
     -board  <N>         (LAN) board index
     -ip     'IP addr'   (LAN) IP address string
     -name   <Name>      (LAN) device name
     -gpib   <N>         (GPIB) board handle
     -pad    <N>         (GPIB) primary address
     -sad    <N>         (GPIB) secondery address
     -ls                 list all instruments on a board and quit
     -shutup             suppress all error/debug prints
     -help/-?            show this information
```

#### VISA

GPIB.c uses VISA APIs, viRead, viWrite, etc. GCC can't be used to build this, while VC is OK.

This version supports both GPIB and LAN-GPIB (VXI 11.3).

```
 GPIB client command options:
     -port               as an Erlang port
     -board  <N>         (LAN) board index
     -ip     'IP addr'   (LAN) IP address string
     -name   <Name>      (LAN) device name
     -gpib   <N>         (GPIB) board handle
     -pad    <N>         (GPIB) primary address
     -sad    <N>         (GPIB) secondery address
     -ls                 list all instruments on a board and quit
     -shutup             suppress all error/debug prints
     -help/-?            show this information
```

NOTE: 
* ./ni: Copyright 2001 National Instruments Corporation
* ./visa: Distributed by IVI Foundation Inc., Contains National Instruments extensions. 
