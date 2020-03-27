#include <stdlib.h>
#include <stdio.h>
#include <Windows.h>
#include <Shlwapi.h>

extern "C" void *__enclave_config = 0;

typedef void (*Callback) (const char *, void *);

struct Config
{
    char logPath [500];
    unsigned int port;
    unsigned int baud;
    bool once;
    unsigned int pause;

    Config () : port (1), baud (4800), once (false), pause (100)
    {
        memset (logPath, 0, sizeof (logPath));
    }
};

bool checkArguments (int argCount, char *args [], Config& config)
{
    bool result = argCount > 0;

    if (result)
        strcpy (config.logPath, args [1]);

    for (int i = 2; result && i < argCount; ++ i)
    {
        const char *arg = args [i];

        if (*arg == '/' || *arg == '-')
        {
            switch (toupper (arg [1]))
            {
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
            }
        }
        else
        {
            result = false;
        }        
    }

    return result;
}

void showHelp ()
{
    printf  ("lp [options] logfile\n\n"
             "Options are:\n"
             "\t-p:port\n"
             "\t-b:baud\n"
             "\t-p:pause_ms\n"
             "\t-o[nce]\n");
}

char *loadFile (Config& config, size_t& size)
{
    char  *buffer;
    HANDLE file;
    unsigned long bytesRead;

    printf ("Serial Log Player\nCopyright (c) by jecat\n");

    file = CreateFileA (config.logPath, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);

    if (file == INVALID_HANDLE_VALUE)
    {
        printf ("Unable to read file '%s', error %d\n", config.logPath, GetLastError ());
        
        return 0;
    }

    size = GetFileSize (file, 0);
    buffer = (char *) malloc (size + 1);

    if (!buffer)
    {
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

char *getNextLine (char *curPos, char *buffer, const size_t size)
{
    memset (buffer, 0, size);

    for (size_t count = 0; count < size; ++ curPos, ++ buffer, ++ count)
    {
        switch (*curPos)
        {
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

size_t countLines (const char *buffer)
{
    size_t count = 1;

    for (const char *chr = buffer; *chr; ++ chr)
    {
        if (*chr == '\n')
            ++ count;
    }

    return count;
}

void processLine (const char *line, void *param)
{
    unsigned long bytesWritten;
    HANDLE port = (HANDLE) param;

    WriteFile (port, line, strlen (line), & bytesWritten, 0);
//printf("%s\n",line);    
}

void processFile (Config& config, char *buffer, Callback cb, void *param)
{
    char line [200];
    size_t numberOfLines = countLines (buffer);

    do
    {
        size_t count = 1;

        for (char *curLine = buffer; *curLine; curLine = getNextLine (curLine, line, sizeof (line)))
        {
            printf ("Line %zd of %zd\r", count ++, numberOfLines);

            strcat (line, "\r\n");
            cb (line, param);

            Sleep (config.pause);
        }
    }
    while (!config.once);
    
}

HANDLE openPort (Config& config)
{
    HANDLE port;
    char portUnc [100];

    sprintf (portUnc, "\\\\.\\COM%d", config.port);

    port = CreateFile (portUnc, GENERIC_READ | GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);

    if (port != INVALID_HANDLE_VALUE)
    {
        DCB dcb;

        GetCommState (port, & dcb);

        dcb.BaudRate = config.baud;

        SetCommState (port, & dcb);
    }

    return port;
}

int main (int argCount, char *args [])
{
    Config config;
    char  *buffer;
    size_t size;

    printf ("Serial Log Player\nCopyright (c) by jecat\n");

    if (!checkArguments (argCount, args, config))
    {
        showHelp ();
        exit (0);
    }

    if (!PathFileExistsA (config.logPath))
    {
        printf ("Unable to find file '%s'\n", config.logPath);
        exit (0);
    }

    buffer = loadFile (config, size);

    if (buffer)
    {
        HANDLE port = openPort (config);

        if (port != INVALID_HANDLE_VALUE)
        {
            processFile (config, buffer, processLine, port);
            CloseHandle (port);
        }
        else
        {
            printf ("Unable to open COM%d, error %d\n", config.port, GetLastError ());
        }

        free (buffer);
    }

    exit (0);
}