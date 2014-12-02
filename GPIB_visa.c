
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <conio.h>
#include <io.h>

#include <windows.h>
#include "visa.h"

#define arr_len(x) (sizeof(x) / sizeof(x[0]))

// TCP-IP instrument
int board = 0;                 // board index
char ip[200] = {'\0'};                   
char name[200] = {'\0'};        

// GPIB instrument
int handle = 0;
int pad = -1;
int sad = -1;

bool shutup = false;
bool port   = false;

typedef void (* f_on_receive)(const char *str, const int len);

struct gpib_dev
{
    char addr[500];
    ViSession rm;
    ViSession dev;
    f_on_receive on_receive;
};

void GPIBCleanup(int ud, const char* ErrorMsg);

//#define dbg_print(...) if (!shutup) fprintf(stderr, __VA_ARGS__)
#define dbg_print(s) if (!shutup) fprintf(stderr, s)

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
    printf("    -board  <N>         (LAN) board index \n");
    printf("    -ip     'IP addr'   (LAN) IP address string\n");
    printf("    -name   <Name>      (LAN) device name\n");
    printf("    -gpib   <N>         (GPIB) board handle \n");
    printf("    -pad    <N>         (GPIB) primary address\n");
    printf("    -sad    <N>         (GPIB) secondery address\n");
    printf("    -ls                 list all instruments on a board and quit\n");
    printf("    -shutup             suppress all error/debug prints\n");    
    printf("    -help/-?            show this information\n");
    printf("Note: Press Enter (empty input) to read device response\n");
}

ViStatus list_instruments()
{
    char instrDescriptor[VI_FIND_BUFLEN];
    ViUInt32 numInstrs;
    ViFindList findList;
    ViSession defaultRM, instr;
    ViStatus status;

    /* First we will need to open the default resource manager. */
   status = viOpenDefaultRM (&defaultRM);
   if (status < VI_SUCCESS)
   {
      printf("Could not open a session to the VISA Resource Manager!\n");
      exit (EXIT_FAILURE);
   }  

    /*
     * Find all the VISA resources in our system and store the number of resources
     * in the system in numInstrs.  Notice the different query descriptions a
     * that are available.

        Interface         Expression
    --------------------------------------
        GPIB              "GPIB[0-9]*::?*INSTR"
        VXI               "VXI?*INSTR"
        GPIB-VXI          "GPIB-VXI?*INSTR"
        Any VXI           "?*VXI[0-9]*::?*INSTR"
        Serial            "ASRL[0-9]*::?*INSTR"
        PXI               "PXI?*INSTR"
        All instruments   "?*INSTR"
        All resources     "?*"
    */
   status = viFindRsrc (defaultRM, "?*INSTR", &findList, &numInstrs, instrDescriptor);
   if (status < VI_SUCCESS)
   {
      printf ("An error occurred while finding resources.\nHit enter to continue.");
      fflush(stdin);
      getchar();
      viClose (defaultRM);
      return status;
   }

   printf("%d instruments, serial ports, and other resources found:\n\n",numInstrs);
   printf("%s \n",instrDescriptor);

   /* Now we will open a session to the instrument we just found. */
   status = viOpen (defaultRM, instrDescriptor, VI_NULL, VI_NULL, &instr);
   if (status < VI_SUCCESS)
   {
      printf ("An error occurred opening a session to %s\n",instrDescriptor);
   }
   else
   {
     /* Now close the session we just opened.                            */
     /* In actuality, we would probably use an attribute to determine    */
     /* if this is the instrument we are looking for.                    */
     viClose (instr);
   }
        
   while (--numInstrs)
   {
      /* stay in this loop until we find all instruments */
      status = viFindNext (findList, instrDescriptor);  /* find next desriptor */
      if (status < VI_SUCCESS) 
      {   /* did we find the next resource? */
         printf ("An error occurred finding the next resource.\n");
         continue;
      } 
      printf("List %s: \n",instrDescriptor);

      if ((strstr(instrDescriptor, "GPIB") != instrDescriptor) && (strstr(instrDescriptor, "TCPIP") == instrDescriptor))
          continue;
    
      /* Now we will open a session to the instrument we just found */
      status = viOpen(defaultRM, instrDescriptor, VI_NULL, VI_NULL, &instr);
      if (status < VI_SUCCESS)
      {
          printf ("An error occurred opening a session to %s\n",instrDescriptor);
      }
      else
      {
          char s[] = "*IDN?\n";
          char r[1024];
          ViUInt32 retCount = 0;
          viWrite(instr, (ViBuf)s, strlen(s), &retCount);
          retCount = 0;
          if (viRead(instr, (ViBuf)r, sizeof(r) - 1, &retCount) >= VI_SUCCESS)
          {
              r[retCount] = '\0';
              printf("%s\n", r);
          }
          viClose(instr);
      }
   }    /* end while */

   status = viClose(findList);
   status = viClose (defaultRM);
   printf ("\nHit enter to continue.");
   getchar();

   return 0;
}

void gpib_shutdown(gpib_dev *dev)
{
    viClose(dev->dev);
    viClose(dev->rm);
}

/*
 *  After each GPIB call, the application checks whether the call
 *  succeeded. If an NI-488.2 call fails, the GPIB driver sets the
 *  corresponding bit in the global status variable. If the call
 *  failed, this procedure prints an error message, takes the board
 *  offline and exits.
 */
void GPIBCleanup(gpib_dev *dev, const char* ErrorMsg)
{
    dbg_print("GPIBCleanup: "); dbg_print(ErrorMsg);
    viClose(dev->dev);
    viClose(dev->rm);
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
    dev.rm = 0;
    dev.dev = 0;
    dev.addr[0] = '\0';

#define load_i_param(var, param) \
    if (strcmp(args[i], "-"#param) == 0)   \
    {   var = atoi(args[i + 1]); i += 2; }

#define load_s_param(var, param) \
    if (strcmp(args[i], "-"#param) == 0)   \
    {   strcpy(var, args[i + 1]); i += 2; }

#define load_b_param(param) \
    if (strcmp(args[i], "-"#param) == 0)   \
    {   param = true; i++; }    


    int i = 1;
    while (i < argc)
    {
        load_i_param(handle, handle)
        else load_i_param(sad, sad)
        else load_i_param(pad, pad)
        else load_i_param(board, board)
        else load_s_param(ip, ip)
        else load_s_param(name, name)
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

    if ((pad < 0) && (strlen(ip) < 1))
    {
        dbg_print("neigher LAN or GPIB address is specified!\n");
        return -1;
    }

    if ((pad >= 0) && (strlen(ip) > 0))
    {
        dbg_print("both LAN and GPIB address are specified!\n");
        return -1;
    }

    if (pad >= 0)
    {
        // GPIB[board]::primary address[::secondary address][::INSTR]        
        if (sad >= 0)
            sprintf(dev.addr, "GPIB%d::%d::%d::INSTR", handle, pad, sad);
        else
            sprintf(dev.addr, "GPIB%d::%d::INSTR", handle, pad);
    }

    if (strlen(ip) > 0)
    {
        //TCPIP[board]::host address[::LAN device name][::INSTR]
        sprintf(dev.addr, "TCPIP%d::%s::%s::INSTR", board, ip, name);
    }

    if (!SetConsoleCtrlHandler((PHANDLER_ROUTINE)ctrl_handler, TRUE))
        dbg_print("WARNING: SetConsoleCtrlHandler failed.\n");

    if (viOpenDefaultRM(&dev.rm) < VI_SUCCESS)
    {
        dbg_print("Could not open a session to the VISA Resource Manager!\n");
        exit(EXIT_FAILURE);
    }  

    if (viOpen(dev.rm, dev.addr, VI_NULL, VI_NULL, &dev.dev) < VI_SUCCESS)
    {
       GPIBCleanup(&dev, "Unable to open device\n");
       return 1;
    }    

    if (viClear(dev.dev) < VI_SUCCESS)
    {
       GPIBCleanup(&dev, "Unable to clear device\n");
       return 1;
    }

    dev.on_receive = stdout_on_receive;
    if (port)
        dev.on_receive = port_on_receive;

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
    ViUInt32 cnt = 0;
    ViStatus status = viRead(dev->dev, (ViBuf)s, sizeof(s) - 1, &cnt);
    if (status < VI_SUCCESS)
    {
        if (status != VI_ERROR_TMO)
        {
            GPIBCleanup(dev, "Unable to read data from device\n");
            return 1;
        }
        else
            return 0;
    }

    send_msg_response(command_dbg_msg, "ibrd");

    send_msg_response(command_dbg_msg, "send_ ing");
    send_comm_response(command_read_from_gpib, (byte *)s, cnt);
    return 0;
}

int as_port(gpib_dev *dev)
{
    ViUInt32 cnt = 0;
    char s[3240 + 1]; 
    send_msg_response(command_dbg_msg, "as_port");
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

            if (viWrite(dev->dev, c.b, c.len, &cnt) < VI_SUCCESS)
            {
               GPIBCleanup(dev, "Unable to write to device\n");
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
        ViUInt32 cnt = 0;
        char s[10240 + 1];
        ViStatus status;

        s[0] = '\0';
        gets(s);
        if (strlen(s) >= sizeof(s) - 1)
        {
            gpib_shutdown(dev);
            break;
        }

        if (strlen(s) > 0)
        {
            status = viWrite(dev->dev, (ViBuf)s, strlen(s), &cnt);
            if (status < VI_SUCCESS)
            {
               GPIBCleanup(dev, "Unable to write to device\n");
               return 1;
            }
        }
        else    // strlen(s) = 0, read response
        {
            status = viRead(dev->dev, (ViBuf)s, sizeof(s) - 1, &cnt);
            if (status < VI_SUCCESS)
            {
                if (status != VI_ERROR_TMO)
                {
                    GPIBCleanup(dev, "Unable to read data from device\n");
                    return 1;
                }
                else
                    continue;
            }
            s[cnt] = '\n'; s[cnt + 1] = '\0';
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


