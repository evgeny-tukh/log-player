#include <stdlib.h>
#include <stdio.h>
#include <Windows.h>
#include <Shlwapi.h>

#define ASCII_XON       0x11 
#define ASCII_XOFF      0x13 

extern "C" void *__enclave_config = 0;

struct Config {
    char logPath [500];
    unsigned int port;
    unsigned int baud;
    bool once, verbose;
    unsigned int pause;
    unsigned int start, end;
    bool lineByLineMode;

    Config () : port (1), baud (4800), once (false), verbose (false), pause (100), start (0), end (0xffffffff), lineByLineMode (false) {
        memset (logPath, 0, sizeof (logPath));
    }
};

typedef void (*Callback) (Config& config, const char *, void *);

bool checkArguments (int argCount, char *args [], Config& config) {
    bool result = argCount > 0;

    if (result) {
        const char *arg = args [1];

        if ((arg [0] == '/' || arg [0] == '-') && (arg [1] == '?' || arg [1] == 'h' || arg [1] == 'H'))
            return false;

        strcpy (config.logPath, arg);
    }

    for (int i = 2; result && i < argCount; ++ i) {
        const char *arg = args [i];

        if (*arg == '/' || *arg == '-') {
            switch (toupper (arg [1])) {
                case 'S':
                    if (arg [2] == ':')
                        config.start = atoi (arg + 3);
                    else
                        result = false;
                    break;
                case 'L':
                    config.lineByLineMode = true; break;
                case 'E':
                    if (arg [2] == ':')
                        config.end = atoi (arg + 3);
                    else
                        result = false;
                    break;
                case 'P':
                    if (arg [2] == ':')
                        config.port = atoi (arg + 3);
                    else
                        result = false;
                    break;
                case 'D':
                    if (arg [2] == ':')
                        config.pause = atoi (arg + 3);
                    else
                        result = false;
                    break;
                case 'B':
                    if (arg [2] == ':')
                        config.baud = atoi (arg + 3);
                    else
                        result = false;
                    break;
                case 'O':
                    config.once = true; break;
                case 'V':
                    config.verbose = true; break;
            }
        }
        else
        {
            result = false;
        }        
    }

    return result;
}

void showHelp () {
    printf  ("\nlp [options] logfile\n\n"
             "Options are:\n"
             "\t-p:port\n"
             "\t-b:baud\n"
             "\t-p:pause (ms)\n"
             "\t-s:start line (one-based)\n"
             "\t-e:end line (one-based)\n"
             "\t-v[erbose]\n"
             "\t-o[nce]\n\n"
             "\t-l[inebylinemode]\n\n"
             "lp [-?|-h] for help\n\n");
}

HANDLE openFile (Config& config) {
    HANDLE file = CreateFileA (config.logPath, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);

    if (file == INVALID_HANDLE_VALUE) {
        printf ("Unable to read file '%s', error %d\n", config.logPath, GetLastError ());
    }

    return file;
}

char *loadFile (Config& config, size_t& size) {
    char  *buffer;
    HANDLE file;
    unsigned long bytesRead;

    file = openFile (config);

    if (file == INVALID_HANDLE_VALUE) return 0;

    size = GetFileSize (file, 0);
    buffer = (char *) malloc (size + 1);

    if (!buffer) {
        CloseHandle (file);
        printf ("Unable to allocate buffer\n");
        
        return 0;
    }

    memset (buffer, 0, size + 1);

    ReadFile (file, buffer, size, & bytesRead, 0);
    CloseHandle (file);

    size = bytesRead;

    return buffer;
}

char *getNextLine (char *curPos, char *buffer, const size_t size) {
    memset (buffer, 0, size);

    for (size_t count = 0; count < size; ++ curPos, ++ buffer, ++ count) {
        switch (*curPos) {
            case '\0':
                return curPos;

            case '\n':
                return curPos + 1;

            case '\r':
                break;

            default:
                *buffer = *curPos;
        }
    }

    return 0;
}

size_t countLines (const char *buffer) {
    size_t count = 1;

    for (const char *chr = buffer; *chr; ++ chr) {
        if (*chr == '\n')
            ++ count;
    }

    return count;
}

void processLine (Config& config, const char *line, void *param) {
    unsigned long bytesWritten;
    HANDLE port = (HANDLE) param;

    WriteFile (port, line, strlen (line), & bytesWritten, 0);

    if (config.verbose)
        printf ("\n%s\n", line);
}

void processFile (Config& config, char *buffer, Callback cb, void *param) {
    char line [200];
    size_t numberOfLines = countLines (buffer);
    char *start = buffer;

    for (unsigned int i = 1; *start && i < config.start; ++ i)
        start = getNextLine (start, line, sizeof (line));

    do {
        size_t count = config.start;

        for (char *curLine = start, *nextLine = getNextLine (curLine, line, sizeof (line));
             *curLine && count <= config.end;
             curLine = nextLine, nextLine = curLine ? getNextLine (curLine, line, sizeof (line)) : 0
        ) {
            printf ("Line %zd of %zd\r", count ++, numberOfLines);

            strcat (line, "\r\n");
            cb (config, line, param);

            Sleep (config.pause);
        }
    } while (!config.once);
}

void processFile (Config& config, HANDLE file, Callback cb, void *param) {
    char line [1000], chr;
    size_t numOfChars, bytesProcessed, lineCount;
    unsigned long fileSize = GetFileSize (file, 0), bytesRead;

    auto reset = [&line, &numOfChars, &bytesProcessed, & lineCount] () {
        memset (line, 0, sizeof (line));
        numOfChars = 0;
    };
    auto restart = [&file, reset] {
        SetFilePointer (file, 0, 0, SEEK_SET);
        reset ();
    };

    reset ();

    while (true) {        
        if (ReadFile (file, & chr, 1, & bytesRead, 0) && bytesRead > 0) {
            if (chr == '\n') {
                line [numOfChars] = chr;
                if (lineCount >= config.start && numOfChars > 0) {
                    cb (config, line, param);
                    Sleep (config.pause);
                }
                printf ("Line %zd\r", ++ lineCount);
                if (lineCount >= config.end) {
                    if (config.once) break;
                    restart ();
                    continue;
                }
                reset ();
            } else {
                line [numOfChars++] = chr;
            }

            ++ bytesProcessed;

            if (bytesProcessed == fileSize) {
                if (config.once) break;

                restart ();
            }
        } else {
            auto error = GetLastError ();
            if (error) {
                printf ("Error %d reading file, stopping\n", error); break;
            } else {
                restart ();
            }
        }
    }
}

HANDLE openPort (Config& config) {
    HANDLE port;
    char portUnc [100];

    sprintf (portUnc, "\\\\.\\COM%d", config.port);

    port = CreateFile (portUnc, GENERIC_READ | GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);

    if (port != INVALID_HANDLE_VALUE) {
        COMMTIMEOUTS timeouts;
        DCB dcb;

        memset (& dcb, 0, sizeof (dcb));

        SetupComm (port, 4096, 4096); 
        PurgeComm (port, PURGE_TXABORT | PURGE_RXABORT | PURGE_TXCLEAR | PURGE_RXCLEAR); 
        GetCommState (port, & dcb);
        GetCommTimeouts (port, & timeouts);

        dcb.BaudRate     = config.baud;
        dcb.ByteSize     = 8;
        dcb.StopBits     = ONESTOPBIT;
        dcb.Parity       = NOPARITY;
        dcb.fOutxDsrFlow = 0;              //TRUE; 
        dcb.fDtrControl  = DTR_CONTROL_ENABLE; 
        dcb.fOutxCtsFlow = 0;              //TRUE; 
        dcb.fRtsControl  = RTS_CONTROL_ENABLE;
        dcb.fInX         = 
        dcb.fOutX        = 1; 
        dcb.XonChar      = ASCII_XON; 
        dcb.XoffChar     = ASCII_XOFF; 
        dcb.XonLim       = 100; 
        dcb.XoffLim      = 100; 
        dcb.fBinary      = 1; 
        dcb.fParity      = 1; 

        SetCommState (port, & dcb);

        timeouts.ReadIntervalTimeout        = 1000;
        timeouts.ReadTotalTimeoutMultiplier = 1;
        timeouts.ReadTotalTimeoutConstant   = 3000;

        SetCommTimeouts (port, & timeouts);
        EscapeCommFunction (port, SETDTR);
    }

    return port;
}

int main (int argCount, char *args []) {
    Config config;
    char  *buffer;
    size_t size;

    printf ("Serial Log Player\nCopyright (c) by jecat\n");

    if (!checkArguments (argCount, args, config)) {
        showHelp ();
        exit (0);
    }

    if (!PathFileExistsA (config.logPath)) {
        printf ("Unable to find file '%s'\n", config.logPath);
        exit (0);
    }

    if (config.lineByLineMode) {
        HANDLE file = openFile (config);

        if (file != INVALID_HANDLE_VALUE) {
            HANDLE port = openPort (config);
            if (port != INVALID_HANDLE_VALUE) {
                processFile (config, file, processLine, port);
                CloseHandle (port);
            } else {
                printf ("Unable to open COM%d, error %d\n", config.port, GetLastError ());
            }

            CloseHandle (file);
        }
    } else {
        buffer = loadFile (config, size);

        if (buffer) {
            HANDLE port = openPort (config);

            if (port != INVALID_HANDLE_VALUE) {
                processFile (config, buffer, processLine, port);
                CloseHandle (port);
            } else {
                printf ("Unable to open COM%d, error %d\n", config.port, GetLastError ());
            }

            free (buffer);
        }
    }

    exit (0);
}
