/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/iosupport.h>
#include <time.h>
#include <unistd.h>
#include <dirent.h>
#include <switch.h>
#include <SDL2/SDL.h>

#include "common.h"
#include "sys.h"
#include "zone.h"

#ifdef SERVERONLY
#include "cvar.h"
#include "net.h"
#include "qwsvdef.h"
#include "server.h"
#else
#include "quakedef.h"
#endif

#ifdef NQ_HACK
#include "client.h"
#include "host.h"
qboolean isDedicated;
#endif

#ifdef SERVERONLY
static cvar_t sys_nostdout = {"sys_nostdout", "0"};
static cvar_t sys_extrasleep = {"sys_extrasleep", "0"};
static qboolean stdin_ready;
static int do_stdin = 1;
#else
static qboolean noconinput = false;
static qboolean nostdout = false;
#endif

void userAppInit (void)
{
	socketInitializeDefault();
}

void userAppExit (void)
{
	if (SDL_WasInit(0))
		SDL_Quit();
	socketExit();
}

/*
 * ===========================================================================
 * General Routines
 * ===========================================================================
 */

void Sys_Printf(const char *fmt, ...) {
    va_list argptr;
    char text[MAX_PRINTMSG];

    va_start(argptr, fmt);
    vsnprintf(text, sizeof(text), fmt, argptr);
    va_end(argptr);

#ifdef DEBUG
    printf(text);

#ifdef SERVERONLY
    if (sys_nostdout.value) return;
#else
    if (nostdout) return;
#endif

    /*FILE *flog = fopen(QBASEDIR "/console.log", "a");
    //if (!flog) return;

    for (p = (unsigned char *)text; *p; p++) {
#ifdef SERVERONLY
        *p &= 0x7f;
#endif
        if ((*p > 128 || *p < 32) && *p != 10 && *p != 13 && *p != 9)
            fprintf(flog, "[%02x]", *p);
        else
            putc(*p, flog);
    }
    fclose(flog);*/
#endif // DEBUG
}

void Sys_Quit(void) {
#ifndef SERVERONLY
    Host_Shutdown();
#endif
    // we'll have have to do this here
    socketExit();
    if (SDL_WasInit(0))
      SDL_Quit();
    exit(0);
}

void Sys_Init(void) {
#ifdef SERVERONLY
    Cvar_RegisterVariable(&sys_nostdout);
    Cvar_RegisterVariable(&sys_extrasleep);
#endif
}

void Sys_Error(const char *error, ...) {
    va_list argptr;
    char string[MAX_PRINTMSG];
    FILE *fout;

    va_start(argptr, error);
    vsnprintf(string, sizeof(string), error, argptr);
    va_end(argptr);

#ifdef DEBUG
    printf(string);
#endif

    fout = fopen(QBASEDIR "/error.log", "w");
    if (fout) {
        fprintf(fout, "Error: %s\n", string);
        fclose(fout);
    }

#ifndef SERVERONLY
    Host_Shutdown();
#endif
    // we'll have to do this here
    socketExit();
    if (SDL_WasInit(0))
      SDL_Quit();
    exit(1);
}

/*
============
Sys_FileTime

returns -1 if not present
============
*/
int Sys_FileTime(const char *path) {
    struct stat buf;

    if (stat(path, &buf) == -1) return -1;

    return buf.st_mtime;
}

void Sys_mkdir(const char *path) {
#ifdef SERVERONLY
    if (mkdir(path, 0777) != -1) return;
    if (errno != EEXIST) Sys_Error("mkdir %s: %s", path, strerror(errno));
#else
    mkdir(path, 0777);
#endif
}

double Sys_DoubleTime(void) {
    struct timeval tp;
    struct timezone tzp;
    static int secbase;

    gettimeofday(&tp, &tzp);

    if (!secbase) {
        secbase = tp.tv_sec;
        return tp.tv_usec / 1000000.0;
    }

    return (tp.tv_sec - secbase) + tp.tv_usec / 1000000.0;
}

#if defined(NQ_HACK) || defined(SERVERONLY)
/*
================
Sys_ConsoleInput

Checks for a complete line of text typed in at the console, then forwards
it to the host command processor
================
*/
char *Sys_ConsoleInput(void) {
    static char text[256];
    int len;
#ifdef NQ_HACK
    fd_set fdset;
    struct timeval timeout;

    if (cls.state != ca_dedicated) return NULL;

    FD_ZERO(&fdset);
    FD_SET(STDIN_FILENO, &fdset);
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
    if (select(STDIN_FILENO + 1, &fdset, NULL, NULL, &timeout) == -1 ||
        !FD_ISSET(STDIN_FILENO, &fdset))
        return NULL;
#endif
#ifdef SERVERONLY
    /* check that the select returned ready */
    if (!stdin_ready || !do_stdin) return NULL;
    stdin_ready = false;
#endif

    len = read(STDIN_FILENO, text, sizeof(text));
#ifdef SERVERONLY
    if (len == 0) do_stdin = 0; /* end of file */
#endif
    if (len < 1) return NULL;
    text[len - 1] = 0; /* remove the /n and terminate */

    return text;
}
#endif /* NQ_HACK || SERVERONLY */

#ifndef SERVERONLY
void Sys_Sleep(void) {
    struct timespec ts;

    ts.tv_sec = 0;
    ts.tv_nsec = 1000000;

    while (nanosleep(&ts, &ts) == -1)
        if (errno != EINTR) break;
}

void Sys_DebugLog(const char *file, const char *fmt, ...) {
    va_list argptr;
    static char data[MAX_PRINTMSG];
    int fd;

    va_start(argptr, fmt);
    vsnprintf(data, sizeof(data), fmt, argptr);
    va_end(argptr);
    fd = open(file, O_WRONLY | O_CREAT | O_APPEND, 0666);
    write(fd, data, strlen(data));
    close(fd);
}

#ifndef USE_X86_ASM
void Sys_HighFPPrecision(void) {}
void Sys_LowFPPrecision(void) {}
void Sys_SetFPCW(void) {}
#endif

/*
================
Sys_MakeCodeWriteable
================
*/
void Sys_MakeCodeWriteable(void *start_addr, void *end_addr) {
#ifdef USE_X86_ASM
    void *addr;
    size_t length;
    intptr_t pagesize;
    int result;

    pagesize = getpagesize();
    addr = (void *)((intptr_t)start_addr & ~(pagesize - 1));
    length = ((byte *)end_addr - (byte *)addr) + pagesize - 1;
    length &= ~(pagesize - 1);
    result = mprotect(addr, length, PROT_READ | PROT_WRITE | PROT_EXEC);
    if (result < 0) Sys_Error("Protection change failed");
#endif
}
#endif /* !SERVERONLY */

/*
 * ===========================================================================
 * Launcher
 * ===========================================================================
 */

// looks like there's no way to disable the console in libnx, so we improvise

extern void Sys_InitSDL (void);

int Q_main(int argc, const char *argv[]);

int main(int argc, char *argv[]) {
    static char *args[16];
    int i, nargs, havebase;
    DIR *dir;

#ifdef DEBUG
    nxlinkStdio();
#endif

    //consoleInit(NULL);

    printf(">>> nxlink attached <<<\n");

    // just in case
    if (argc <= 0) {
        nargs = 1;
        args[0] = "nxquake.nro";
    } else {
        nargs = argc;
        for (i = 0; i < argc && i < 8; ++i)
            args[i] = argv[i];
    }

    dir = opendir(QBASEDIR);
    if (!dir) Sys_Error("could not open `" QBASEDIR "`");
    havebase = 1;
    closedir(dir);

    if (!havebase)
        Sys_Error("base game directory `id1` not found in `%s`", QBASEDIR);

    args[nargs] = NULL;

    return Q_main(nargs, (const char **)args);
}

/*
 * ===========================================================================
 * Actual main
 * ===========================================================================
 */

int Q_main(int argc, const char **argv) {
    double time, oldtime, newtime;
    quakeparms_t parms;
#ifdef SERVERONLY
    fd_set fdset;
    struct timeval timeout;
#endif

#ifndef SERVERONLY
    signal(SIGFPE, SIG_IGN);
#endif

    memset(&parms, 0, sizeof(parms));

    COM_InitArgv(argc, argv);
    parms.argc = com_argc;
    parms.argv = com_argv;
    parms.basedir = QBASEDIR;
    parms.memsize = 64*1024*1024;
    parms.membase = malloc(parms.memsize);
    if (!parms.membase) Sys_Error("Allocation of %d byte heap failed", parms.memsize);

#ifdef SERVERONLY
    SV_Init(&parms);

    /* run one frame immediately for first heartbeat */
    SV_Frame(0.1);
#else
    if (COM_CheckParm("-noconinput")) noconinput = true;
    if (COM_CheckParm("-nostdout")) nostdout = true;

    // Make stdin non-blocking
    // FIXME - check both return values
    if (!nostdout)
#ifdef NQ_HACK
        printf("Quake -- TyrQuake Version %s\n", stringify(TYR_VERSION));
#endif
#ifdef QW_HACK
        printf("QuakeWorld -- TyrQuake Version %s\n", stringify(TYR_VERSION));
#endif

    printf("Sys_InitSDL initialiazing\n");
    Sys_InitSDL();

    printf("Sys_Init initialiazing\n");
    Sys_Init();

    //printf("userAppInit initialiazing\n");
    //userAppInit();

    printf("Host_Init initialiazing\n");
    Host_Init(&parms);
    
#endif /* SERVERONLY */

    /*
     * Main Loop
     */
#if defined(NQ_HACK) || defined(SERVERONLY)
    oldtime = Sys_DoubleTime() - 0.1;
#else
    oldtime = Sys_DoubleTime();
#endif
    while (appletMainLoop()) {
        /* find time passed since last cycle */
        newtime = Sys_DoubleTime();
        time = newtime - oldtime;

#ifdef NQ_HACK
        if (cls.state == ca_dedicated) {
            if (time < sys_ticrate.value) {
                usleep(1);
                continue;  // not time to run a server only tic yet
            }
            time = sys_ticrate.value;
        }
        if (time > sys_ticrate.value * 2)
            oldtime = newtime;
        else
            oldtime += time;
#endif
#ifdef QW_HACK
        oldtime = newtime;
#endif

#ifdef SERVERONLY
        SV_Frame(time);

        /*
         * extrasleep is just a way to generate a fucked up connection
         * on purpose
         */
        if (sys_extrasleep.value) usleep(sys_extrasleep.value);
#else
        Host_Frame(time);
#endif
    }

    // due to appletLockExit, main loop will terminate if user exits via HOME
	// so we clean shit up
	Sys_Quit ();
    userAppExit();

    return 0;
}
