#ifndef SERIAL_AT
#define SERIAL_AT

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#ifdef __cplusplus
   extern "C" {
#endif
//////////////////////////////////////////////////////////////////////////////

typedef unsigned long long      ULONG64;
typedef unsigned long             DWORD;
typedef unsigned short            WORD;
typedef unsigned int                UINT;
typedef int                               HANDLE;
typedef bool                             BOOL;
typedef unsigned char             BYTE;

#define FALSE                   false
#define TRUE                    true
#define INVALID_HANDLE_VALUE    -1

//////////////////////////////////////////////////////////////////////////////

#define ARRAYSIZE(a)    (sizeof(a) / sizeof(a[0]))
#define max(a, b)       ((a > b) ? a : b)

//////////////////////////////////////////////////////////////////////////////

#define MAX_COMPORT_NAME           50

#define INVALID_BAUDRATE      0xFFFFFFFF    // Used by l2_EnterATMode() -> AT command mode not active
#define RETRY_ENTER_AT_MODE            2    // Number of retries each baud rate is tried for entering AT command mode
#define CTS_WAIT_TIMEOUT           15000    // Timeout (in ms) while waiting for CTS signal change
#define CTS_WAIT_TIMEOUT_SHUTDOWN   4000    // Timeout (in ms) while waiting for CTS signal change#define WAIT_BEFORE_INIT_ATC         200  // Wait time (in ms) before the init AT command is sent
#define WAIT_AFTER_CTS_ON            100    // Wait time (in ms) after valid CTS high signal detected
#define WAIT_AFTER_CTS_OFF           500    // Wait time (in ms) after valid CTS low signal detected
#define WAIT_BEFORE_INIT_ATC         200    // Wait time (in ms) before the init AT command is sent
#define WAIT_BEFORE_ATC               50    // Wait time (in ms) before an AT command is sent
#define WAIT_BEFORE_START           2000    // Wait time (in ms) before start of download
#define WAIT_BETWEEN_RESPONSE_CHARS   10    // Wait time (in ms) between two characters of an AT-command response
#define WAIT_WAITFORTIMEOUT            0    // Intervals to wait for timeout (on Windows: set to 0 for max. performance or 1..10 for minimum processor usage)
#define WAIT_CTS_START               300    // Wait time (in ms) after starting ingnition pulse before checking CTS
#define WAIT_CTS_RESET              1000    // Wait time (in ms) after module reset before checking CTS
#define WAIT_DUMMY                     1    // Dummy wait time, just to give back control of program back to OS.
#define ATC_TIMEOUT                 4000    // TimeoutWait (in ms) while waiting for ATC response
#define MAX_START_ACK_TRIES           80    // Tries waiting for first Ack.
#define WAIT_BOOTLOADER_TIMEOUT     5000    // Bootloader timeout
#define WAIT_AFTER_REOPEN            100    // Wait time (in ms) after opening the COM port during re-open
#define WAIT_AFTER_ATC                50    // Wait time (in ms) after first byte of ATC response is received
#define RETRY_ATC                      5    // Number of retries for an AT command if there was no response
#define IGNITION_TIME                100    // Starting ignition pulse time (in ms)
#define BLOCK_TIMEOUT               5000    // TimeoutWait (in ms) while waiting for block transmission response
#define BLOCK_RETRIES                 10    // Maximuim numbers of retries per block
#define START_TIMEOUT              20000    // Timeout (in ms) for start of download
#define BOOTLOADER_DEFAULT_BAUD   115200    // Default baudrate used by the bootloader in case no valid firmware is in the module.

#define MAX_RESPOND 2000                    // maximum of expected size of resonses for AT commands

//#define BAUD_TO_USE(x) min(nBaud[x], 230400)

#define  BAUD_19200         19200
#define  BAUD_38400         38400
#define  BAUD_57600         57600
#define  BAUD_115200     115200
#define  BAUD_230400     230400
#define  BAUD_460800     460800
#define  BAUD_921600     921600

/* when SDPORT = 4 */
#define CENTERION_MDM_PORT "/dev/ttyUSB3"

// App port is serial and should always be present
#define CENTERION_APP_PORT "/dev/ttyS2"
#define QUECTEL_COMM_PORT  "/dev/ttyUSB2"

/* when SDPORT = 6 */
// Modem port, enumerated as  /sys/bus/usb/devices/1-1:1.0/tty:ttyACM0
#define CENTERION_ACM_PORT   "/dev/ttyACM0"  /
//////////////////////////////////////////////////////////////////////////////


// Description of the functions declared below in func.c

ULONG64 GetTickCount();

// COM-Port (USB-Port) functions
BOOL SetTimeouts();
BOOL StartModuleWait();
void ToggleRTS();
BOOL WaitCTS(BOOL fState, DWORD dwTimeOut);
BOOL PurgeCOM();
BOOL ChangeBaud(DWORD nNewBaud);
DWORD SerWrite(const BYTE *pbData, DWORD dwNr);
BOOL SerGetch(BYTE *pbData);
BOOL OpenCOM(char *szCOM, DWORD nxBaud);
void CloseCOM();
BOOL SerGetchTimeout(BYTE *pbData, DWORD dwTimeout);
void WaitMs(DWORD dwSleep);
DWORD EnterATMode(DWORD nBaud);
BOOL ReadLine(char *szResponse, DWORD dwSize, DWORD dwToMs) ;
BOOL ReadResponse(const char *szATCmd, char *szResponse, DWORD dwSize, BOOL bExpectOK);
BOOL ATCommand(const char *szATCmd, char *szResponse, DWORD dwSize);
BOOL PurgeCOM();
BOOL ATCommandWait(const char *szATCmd, char *szResponse, DWORD dwSize, DWORD dwWaitMs);
BOOL ATCommandUrc(const char *szATCmd, char *szResponse, DWORD dwSize, DWORD dwWaitMs, char *urc) ;
BOOL ReadResponseUrc(const char *szATCmd, char *szResponse, DWORD dwSize,   DWORD dwWaitMs, char *urc);
BOOL wait_for_connection( int maxSec) ;
BOOL ReadComPort(char *szResponse, DWORD dwSize, DWORD dwTo) ;

#ifdef __cplusplus
  };
#endif

#endif