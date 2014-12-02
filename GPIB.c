
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <conio.h>

#include <windows.h>
#include "ni488.h"

#define arr_len(x) (sizeof(x) / sizeof(x[0]))

int GPIB = 0;                 // Board handle

int PAD = 1;                      // Primary address
int SAD = 0;                      // Secondary address
bool shutup = false;
bool port   = false;

typedef void (* f_on_receive)(const char *str, const int len);

struct gpib_dev
{
    int dev;
    f_on_receive on_receive;
};

struct instru_info 
{
    int pad;
    int sad;
    char idn[1024];
};

#define TIMEOUT               T10s  // Timeout value = 10 seconds
#define EOTMODE               1     // Enable the END message
#define EOSMODE               0     // Disable the EOS mode

char ErrorMnemonic[21][5] = {"EDVR", "ECIC", "ENOL", "EADR", "EARG",
                             "ESAC", "EABO", "ENEB", "EDMA", "",
                             "EOIP", "ECAP", "EFSO", "", "EBUS",
                             "ESTB", "ESRQ", "", "", "", "ETAB"};


void GPIBCleanup(int ud, const char* ErrorMsg);

#define dbg_print(...) if (!shutup) fprintf(stderr, __VA_ARGS__)

#define MAX_COMM_PACK_SIZE 65536

#define command_write_to_gpib       0
#define command_read_from_gpib      1
#define command_dbg_msg             2
#define command_shutdown            3

int read_exact(byte *buf, int len)
{
  int i, got=0;

  do 
  {
    if ((i = read(0, buf + got, len - got)) <= 0)
      return i;
    got += i;
  } while (got < len);

  return len;
}

int write_exact(byte *buf, int len)
{
  int i, wrote = 0;

  do 
  {
    if ((i = write(1, buf + wrote, len - wrote)) <= 0)
      return i;
    wrote += i;
  } while (wrote < len);

  return len;
}

int read_cmd(byte *buf)
{
  int len;

  if (read_exact(buf, 2) != 2)
    return -1;
  len = (buf[0] << 8) | buf[1];
  return read_exact(buf, len);
}

int write_cmd(byte *buf, int len)
{
  byte li;

  li = (len >> 8) & 0xff;
  write_exact(&li, 1);
  
  li = len & 0xff;
  write_exact(&li, 1);

  return write_exact(buf, len);
}

struct gpib_port_comm
{
    int len;
    char t;
    byte *b;
};

bool read_comm_cmd(gpib_port_comm &r)
{
    static byte cmd_buf[MAX_COMM_PACK_SIZE];
    r.len = -1;
    int len = read_cmd(cmd_buf);
    if (len < 0)
        return false;
    
    r.t = cmd_buf[0];
    r.len = len - 1;
    r.b = cmd_buf + 1;
    r.b[r.len] = 0;
    return true;
}

bool send_comm_response(const int t, const byte *s, const int len)
{
    static byte out_buf[MAX_COMM_PACK_SIZE];
    if (1 + len > MAX_COMM_PACK_SIZE)
        return false;

    out_buf[0] = t;
    memcpy(out_buf + 1, s, len);

    return write_cmd(out_buf, len + 1) > 0; 
}

void send_msg_response(const int t, const char *s)
{
    if (!shutup)
        send_comm_response(t, (const byte *)s, strlen(s));
}

void help()
{
    printf("GPIB client command options: \n");
    printf("    -port               as an Erlang port\n");
    printf("    -handle <N>         board handle\n");
    printf("    -pad    <N>         primary address\n");
    printf("    -sad    <N>         primary address\n");
    printf("    -ls                 list all instruments on a board and quit\n");
    printf("    -shutup             suppress all error/debug prints\n");    
    printf("    -help/-?            show this information\n");
    printf("Note: Press Enter (empty input) to read device response\n");
}

int list_instruments()
{

    int        Num_Instruments;            // Number of instruments on GPIB
    Addr4882_t Instruments[31];            // Array of primary addresses
    Addr4882_t Result[31];                 // Array of listen addresses
    instru_info infos[31];
    int loop;

    SendIFC(GPIB);
    if (ibsta & ERR)
    {
       GPIBCleanup(GPIB, "Unable to open board");
       return 1;
    }

    for (loop = 0; loop < 30; loop++) {
       Instruments[loop] = (Addr4882_t)(loop + 1);
    }
    Instruments[arr_len(Instruments) - 1] = NOADDR;

    printf("Finding all instruments on the bus...\n\n");
    FindLstn(GPIB, Instruments, Result, arr_len(Instruments));
    if (ibsta & ERR)
    {
       GPIBCleanup(GPIB, "Unable to issue FindLstn call");
       return 1;
    }

    Num_Instruments = ibcntl;
    printf("Number of instruments found = %d\n\n", Num_Instruments);
    Result[Num_Instruments] = NOADDR;

    for (loop = 0; loop < Num_Instruments; loop++)
    {
       infos[loop].pad = GetPAD(Result[loop]);
       infos[loop].sad = GetSAD(Result[loop]);
    }
    ibonl(GPIB, 0);

    for (loop = 0; loop < Num_Instruments; loop++)
    {
        int Dev = ibdev(GPIB, infos[loop].pad, infos[loop].sad,
              TIMEOUT, EOTMODE, EOSMODE);
        if (ibsta & ERR)
        {
           GPIBCleanup(Dev, "Unable to open device");
           return 1;
        }

        ibclr(Dev);
        if (ibsta & ERR)
        {
           GPIBCleanup(Dev, "Unable to clear device");
           return 1;
        }

        ibwrt(Dev, (void *)"*IDN?", 5L);
        if (ibsta & ERR)
        {
           GPIBCleanup(Dev, "Unable to write to device");
           return 1;
        }

        ibrd(Dev, infos[loop].idn, arr_len(infos[loop].idn) - 1);
        if (ibsta & ERR)
        {
           GPIBCleanup(Dev, "Unable to read data from device");
           return 1;
        }
        infos[loop].idn[ibcntl] = '\0';
        ibonl(Dev, 0);
    }

    for (loop = 0; loop < Num_Instruments; loop++)
    {
        if (infos[loop].sad == NO_SAD)
        {
            printf("The instrument at %d is %s(PAD = %d, SAD = NONE)\n",
                   loop + 1, infos[loop].idn, infos[loop].pad);
        }
        else
        {
            printf("The instrument at %d is %s(PAD = %d, SAD = %d)\n",
                   loop + 1, infos[loop].idn, infos[loop].pad, infos[loop].sad);
        }
    }    

    return 0;
}

void gpib_shutdown(gpib_dev *dev)
{
    //ibnotify(dev->dev, 0, NULL, NULL);
    ibonl(dev->dev, 0);
    dev->dev = -1;
}

/*
 *  After each GPIB call, the application checks whether the call
 *  succeeded. If an NI-488.2 call fails, the GPIB driver sets the
 *  corresponding bit in the global status variable. If the call
 *  failed, this procedure prints an error message, takes the board
 *  offline and exits.
 */
void GPIBCleanup(int ud, const char* ErrorMsg)
{
    dbg_print("Error : %s\nibsta = 0x%x iberr = %d (%s)\n",
               ErrorMsg, ibsta, iberr, ErrorMnemonic[iberr]);
    dbg_print("Cleanup: Taking board offline\n");
    ibnotify(ud, 0, NULL, NULL);    
    ibonl(ud, 0);
}

static gpib_dev dev;

BOOL ctrl_handler(DWORD fdwCtrlType) 
{ 
    switch (fdwCtrlType) 
    {
        case CTRL_C_EVENT: 
            gpib_shutdown(&dev);
            exit(0);
            return TRUE;
  
        case CTRL_BREAK_EVENT: 
            return FALSE; 

        // console close/logoff/shutdown
        case CTRL_CLOSE_EVENT: 
        case CTRL_LOGOFF_EVENT: 
        case CTRL_SHUTDOWN_EVENT:
            gpib_shutdown(&dev);
            exit(0);
            return FALSE; 

        default: 
            return FALSE; 
    } 
}

int as_port(gpib_dev *dev);
int interactive(gpib_dev *dev);
int __stdcall cb_on_rqs(int LocalUd, int LocalIbsta, int LocalIberr, 
      long LocalIbcntl, void *RefData);
void stdout_on_receive(const char *s, const int len);
void port_on_receive(const char *s, const int len);

int main(const int argc, const char *args[])
{   
    dev.dev = -1;

#define load_i_param(var, param) \
    if (strcmp(args[i], "-"#param) == 0)   \
    {   var = atoi(args[i + 1]); i += 2; }

#define load_b_param(param) \
    if (strcmp(args[i], "-"#param) == 0)   \
    {   param = true; i++; }    


    int i = 1;
    while (i < argc)
    {
        load_i_param(GPIB, handle)
        else load_i_param(SAD, sad)
        else load_i_param(PAD, pad)
        else load_b_param(shutup)
        else load_b_param(port)
        else if (strcmp(args[i], "-ls") == 0) 
        {
            return list_instruments();
        }
        else if ((strcmp(args[i], "-help") == 0) || (strcmp(args[i], "-?") == 0))
        {
            help();
            return -1;
        }
        else
            i++;
    }

    if (!SetConsoleCtrlHandler((PHANDLER_ROUTINE)ctrl_handler, TRUE))
        dbg_print("WARNING: SetConsoleCtrlHandler failed.\n");

    ibconfig(GPIB, IbcAUTOPOLL, 1);

    dev.dev = ibdev(GPIB, PAD, SAD, TIMEOUT, EOTMODE, EOSMODE);
    if (ibsta & ERR)
    {
       GPIBCleanup(dev.dev, "Unable to open device");
       return 1;
    }    

    ibclr(dev.dev);
    if (ibsta & ERR)
    {
       GPIBCleanup(dev.dev, "Unable to clear device");
       return 1;
    }

    dev.on_receive = stdout_on_receive;
    if (port)
        dev.on_receive = port_on_receive;

    // set up the asynchronous event notification on RQS
    // ibnotify(dev.dev, RQS | TIMO, cb_on_rqs, &dev);
    // if (ibsta & ERR)  
    // {
    //     GPIBCleanup(dev.dev, "ibnotify call failed.\n");
    //     return 0;
    // }

    if (port)
    {
        setmode(0, O_BINARY);
        setmode(1, O_BINARY);
        return as_port(&dev);
    }
    else
    {
        if (!shutup)
            printf("Tip: Press Enter to read response\n");
        return interactive(&dev);
    }
}

int port_read(gpib_dev *dev)
{
    char s[3240 + 1];
    ibrd(dev->dev, s, sizeof(s) - 1); 
    send_msg_response(command_dbg_msg, "ibrd");
    if (ibsta & ERR)
    {
        if (iberr == EABO)
            return 0;
        GPIBCleanup(dev->dev, "Unable to read data from device");
        return 1;
    }
    send_msg_response(command_dbg_msg, "send_ ing");
    send_comm_response(command_read_from_gpib, (byte *)s, ibcntl);
    return 0;
}

int as_port(gpib_dev *dev)
{
    char s[3240 + 1]; send_msg_response(command_dbg_msg, "as_port");
    while (true)
    {
        gpib_port_comm c;
        send_msg_response(command_dbg_msg, "wait for command");
        read_comm_cmd(c);
        send_msg_response(command_dbg_msg, "read_comm_cmd");
        switch (c.t)
        {
        case command_write_to_gpib:
            send_msg_response(command_dbg_msg, "command_write_to_gpib");
            if (c.len < 1) 
                continue;

            ibwrt(dev->dev, c.b, c.len);
            if (ibsta & ERR)
            {
               GPIBCleanup(dev->dev, "Unable to write to device");
               return 1;
            }

            break;
        case command_read_from_gpib:
            send_msg_response(command_dbg_msg, "command_read_from_gpib");

            if (port_read(dev) != 0)
                return 1;
            break;
        default:
            gpib_shutdown(dev);
            return 0;
        }

        
    }
}

int interactive(gpib_dev *dev)
{
    while (true)
    {
        char s[10240 + 1];

        s[0] = '\0';
        gets(s);
        if (strlen(s) >= sizeof(s) - 1)
        {
            gpib_shutdown(dev);
            break;
        }

        if (strlen(s) > 0)
        {
            ibwrt(dev->dev, s, strlen(s));
            if (ibsta & ERR)
            {
               GPIBCleanup(dev->dev, "Unable to write to device");
               return 1;
            }
        }
        else    // strlen(s) = 0, read response
        {
            ibrd(dev->dev, s, sizeof(s) - 1);
            if (ibsta & ERR)
            {
                if (iberr == EABO) continue;
                GPIBCleanup(dev->dev, "Unable to read data from device");
                return 1;
            }
            s[ibcntl] = '\n'; s[ibcntl + 1] = '\0';
            printf(s);
        }
    }
}

void stdout_on_receive(const char *s, const int len)
{
    printf(s);
}

void port_on_receive(const char *s, const int len)
{
    send_comm_response(command_read_from_gpib, (const byte *)s, len);
}

int __stdcall cb_on_rqs(int LocalUd, int LocalIbsta, int LocalIberr, 
      long LocalIbcntl, void *RefData)
{
    gpib_dev *dev = (gpib_dev *)(RefData);

#define fatal_error(s)  \
    {                   \
        GPIBCleanup(dev->dev, s); \
        exit(-1);                   \
    } while (false)   

#define expectedResponse 0x43

   char SpollByte;
   char ReadBuffer[4000];

   // If the ERR bit is set in LocalIbsta, then print an error message 
   // and return.
   if (LocalIbsta & ERR)  {
      fatal_error("GPIB error has occurred. No more callbacks.");
   }
   
   // Read the serial poll byte from the device. If the ERR bit is set
   // in ibsta, then print an error message and return.
   LocalIbsta = ibrsp(LocalUd, &SpollByte);
   if (LocalIbsta & ERR)  {
      fatal_error("GPIB error has occurred. No more callbacks.");
   }

   // If the returned status byte equals the expected response, then 
   // the device has valid data to send; otherwise it has a fault 
   // condition to report.
   //if (SpollByte != expectedResponse)   {
   //   fatal_error("GPIB error has occurred. No more callbacks.");
   // }

   // Read the data from the device. If the ERR bit is set in ibsta, 
   // then print an error message and return.
   LocalIbsta = ibrd(LocalUd, ReadBuffer, sizeof(ReadBuffer) - 1);
   if (LocalIbsta & ERR)  {
      fatal_error("GPIB error has occurred. No more callbacks.");
   }

   ReadBuffer[ibcntl] = '\0';
   dev->on_receive(ReadBuffer, ibcntl);

   return RQS;
}

