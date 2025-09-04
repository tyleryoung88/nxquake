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

#include <EGL/egl.h>
#include <SDL2/sdl.h>

#include "cmd.h"
#include "console.h"
#include "glquake.h"
#include "input.h"
#include "quakedef.h"
#include "sys.h"
#include "vid.h"

#include <switch.h>

#ifdef NQ_HACK
#include "host.h"
#endif

#define WARP_WIDTH 320
#define WARP_HEIGHT 200

int vid_modenum = VID_MODE_NONE;

static cvar_t vid_mode = {.name = "vid_mode", .string = stringify(VID_MODE_WINDOWED), .archive = false};

unsigned short d_8to16table[256];
unsigned d_8to24table[256];
unsigned char d_15to8table[65536];

viddef_t vid;

qboolean VID_IsFullScreen(void) { return vid_modenum != 0; }

qboolean VID_CheckAdequateMem(int width, int height) { return true; }
void VID_LockBuffer(void) {}
void VID_UnlockBuffer(void) {}

void (*VID_SetGammaRamp)(unsigned short ramp[3][256]) = NULL;

float gldepthmin, gldepthmax;
qboolean gl_mtexable;
cvar_t gl_ztrick = {"gl_ztrick", "1"};

void VID_Update(vrect_t *rects) {}
void D_BeginDirectRect(int x, int y, const byte *pbitmap, int width, int height) {}
void D_EndDirectRect(int x, int y, int width, int height) {}

/*
 * FIXME!!
 *
 * Move stuff around or create abstractions so these hacks aren't needed
 */

void Sys_SendKeyEvents(void) { IN_ProcessEvents(); }

static void VID_InitModeList(void) {}

static SDL_Window	*draw_context;
static SDL_Renderer	*draw_renderer;

static EGLDisplay s_display;
static EGLSurface s_surface;
static EGLContext s_context;

const char *gl_vendor;
const char *gl_renderer;
const char *gl_version;
const char *gl_extensions = NULL;

static int gl_num_texture_units;

static void setMesaConfig() {
    // Uncomment below to disable error checking and save CPU time (useful for
    // production): setenv("MESA_NO_ERROR", "1", 1);

#ifdef GPUDEBUG
    setenv("EGL_LOG_LEVEL", "debug", 1);
    setenv("MESA_VERBOSE", "all", 1);
    setenv("MESA_DEBUG", "1", 1);
    setenv("NOUVEAU_MESA_DEBUG", "1", 1);
    setenv("NV50_PROG_OPTIMIZE", "0", 1);
    setenv("NV50_PROG_DEBUG", "1", 1);
    setenv("NV50_PROG_CHIPSET", "0x120", 1);
#endif
}

NWindow *win = NULL;

void Sys_InitSDL (void)
{
	SDL_version v;
	SDL_version *sdl_version = &v;
	SDL_GetVersion(&v);

	Sys_Printf("Found SDL version %i.%i.%i\n",sdl_version->major,sdl_version->minor,sdl_version->patch);

	if (SDL_Init(SDL_INIT_VIDEO) < 0) {
		Sys_Error("Couldn't init SDL: %s", SDL_GetError());
	}

	int flags = SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN;
	draw_context = SDL_CreateWindow ("TyrQuakeNX", 0, 0, 1280, 720, flags);

	if (!draw_context) {
		Sys_Error ("SDLINIT: SDL_CreateWindow: Couldn't create window | %s", SDL_GetError());
	}

    // --- Initialize Nintendo Switch native window ---
    // Initialize applet + video interface
    appletInitialize();
    Result rc = viInitialize(ViServiceType_Default);

    if (R_FAILED(rc))
        Sys_Error("viInitialize failed: 0x%x", rc);

    // Open the default display
    ViDisplay display;
    viOpenDefaultDisplay(&display);

    // Create a layer
    ViLayer layer;
    viCreateLayer(&display, &layer);

    // Create an NWindow from the layer
    nwindowCreateFromLayer(win, &layer);

    // Set dimensions (match SDL window size)
    nwindowSetDimensions(win, 1280, 720);
}

static void CreateUselessSDLWindow (void)
{
	int flags = SDL_WINDOW_SHOWN | SDL_WINDOW_FULLSCREEN;
	draw_context = SDL_CreateWindow("", 0, 0, 1280, 720, flags);
	if (!draw_context) printf("draw_context == NULL!\n%s\n", SDL_GetError());
	draw_renderer = SDL_CreateRenderer(draw_context, -1, 0);
	if (!draw_renderer) printf("draw_renderer == NULL!\n%s\n", SDL_GetError());
}

static qboolean InitEGL(NWindow* win) {
    s_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (!s_display) {
        Sys_Error("Could not connect to display! error: %d", eglGetError());
        goto _fail0;
    }

    EGLint major, minor;
    if (!eglInitialize(s_display, &major, &minor)) {
        Sys_Error("EGL init failed, error: %d", eglGetError());
        goto _fail0;
    }
    printf("EGL version %d.%d\n", major, minor);

    if (eglBindAPI(EGL_OPENGL_API) == EGL_FALSE) {
        Sys_Error("Could not set GL API! error: %d", eglGetError());
        goto _fail1;
    }

    EGLConfig config;
    EGLint numConfigs;
    static const EGLint framebufferAttributeList[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_DEPTH_SIZE, 24,
        EGL_STENCIL_SIZE, 8,
        EGL_NONE
    };
    if (!eglChooseConfig(s_display, framebufferAttributeList, &config, 1, &numConfigs) || numConfigs == 0) {
        Sys_Error("EGL: No config found! error: %d", eglGetError());
        goto _fail1;
    }

    s_surface = eglCreateWindowSurface(s_display, config, win, NULL);
    if (!s_surface) {
        Sys_Error("EGL: Surface creation failed! error: %d", eglGetError());
        goto _fail1;
    }

    static const EGLint contextAttributeList[] = {
        EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT,
		EGL_NONE
    };
    s_context = eglCreateContext(s_display, config, EGL_NO_CONTEXT, contextAttributeList);
    if (!s_context) {
        Sys_Error("EGL: Context creation failed! error: %d", eglGetError());
        goto _fail2;
    }

    if (!eglMakeCurrent(s_display, s_surface, s_surface, s_context)) {
        Sys_Error("eglMakeCurrent failed! error: %d", eglGetError());
        goto _fail2;
    }

    return true;

_fail2:
    eglDestroySurface(s_display, s_surface);
    s_surface = NULL;
_fail1:
    eglTerminate(s_display);
    s_display = NULL;
_fail0:
    return false;
}

static void DeinitEGL(void) {
    if (s_display) {
        eglMakeCurrent(s_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (s_context) {
            eglDestroyContext(s_display, s_context);
            s_context = NULL;
        }
        if (s_surface) {
            eglDestroySurface(s_display, s_surface);
            s_surface = NULL;
        }
        eglTerminate(s_display);
        s_display = NULL;
    }
}

static qboolean VID_GL_CheckExtn(const char *extn) {
    const char *found;
    const int len = strlen(extn);
    char nextc;

    found = strstr(gl_extensions, extn);
    while (found) {
        nextc = found[len];
        if (nextc == ' ' || !nextc)
            return true;
        found = strstr(found + len, extn);
    }

    return false;
}

Framebuffer fb;

static void VID_InitGL(void) {
    Cvar_RegisterVariable(&vid_mode);
    Cvar_RegisterVariable(&gl_npot);
    Cvar_RegisterVariable(&gl_ztrick);

    framebufferCreate(&fb, win, 1280, 720, PIXEL_FORMAT_RGBA_8888, 1);
	framebufferMakeLinear(&fb);

    CreateUselessSDLWindow ();

    setMesaConfig();
    Con_Printf ("Initializing GL\n");
    // Initialize EGL on the default window
    if (!InitEGL(win))
        Sys_Error ("Failed to initialize EGL!\n");

    // Load OpenGL routines using glad
    if (!gladLoadGL((GLADloadfunc)eglGetProcAddress)) {
        Sys_Error ("Failed to load glad!\n");
    }

    Con_Printf ("GL Loaded\n");

    eglSwapInterval(s_display, 1);

    gl_vendor = (const char *)glGetString(GL_VENDOR);
    gl_renderer = (const char *)glGetString(GL_RENDERER);
    gl_version = (const char *)glGetString(GL_VERSION);
    gl_extensions = (const char *)glGetString(GL_EXTENSIONS);

    Con_Printf("GL_VENDOR: %s\n", gl_vendor);
    Con_Printf("GL_RENDERER: %s\n", gl_renderer);
    Con_Printf("GL_VERSION: %s\n", gl_version);
    Con_Printf("GL_EXTENSIONS: %s\n", gl_extensions);

    qglMultiTexCoord2fARB = NULL;
    qglActiveTextureARB = NULL;
    gl_mtexable = false;

    GL_ExtensionCheck_NPoT();

    Con_Printf ("QGL Init:\n");

    QGL_Init();

    glCullFace(GL_FRONT);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_ALPHA_TEST);

    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

}

qboolean VID_SetMode(const qvidmode_t *mode, const byte *palette) {
    vid.numpages = 1;
    vid.width = vid.conwidth = mode->width;
    vid.height = vid.conheight = mode->height;
    vid.maxwarpwidth = WARP_WIDTH;
    vid.maxwarpheight = WARP_HEIGHT;
    vid.aspect = ((float)vid.height / (float)vid.width) * (320.0 / 240.0);

    vid.colormap = host_colormap;
    vid.fullbright = 256 - LittleLong(*((int *)vid.colormap + 2048));

    vid_modenum = mode - modelist;
    Cvar_SetValue("vid_mode", vid_modenum);
    Cvar_SetValue("vid_fullscreen", 1);
    Cvar_SetValue("vid_width", mode->width);
    Cvar_SetValue("vid_height", mode->height);
    Cvar_SetValue("vid_bpp", mode->bpp);
    Cvar_SetValue("vid_refreshrate", mode->refresh);

    vid.recalc_refdef = 1;

    return true;
}

static SDL_Renderer *sdl_renderer;

void VID_Init(const byte *palette) {
    static qboolean did_init = false;
    int err;
    qvidmode_t *mode;
    const qvidmode_t *setmode;

    if (did_init) return;

    VID_InitModeCvars();

    Cmd_AddCommand("vid_describemodes", VID_DescribeModes_f);

    /* Init the default windowed mode */
    mode = modelist;
    mode->modenum = 0;
    mode->bpp = 32;
    mode->refresh = 0;
    mode->width = 1280;
    mode->height = 720;
    nummodes = 1;

    /* TODO: read config files first to avoid multiple mode sets */
    VID_InitModeList();
    setmode = &modelist[0];

    VID_InitGL();
    VID_SetMode(setmode, palette);
    VID_SetPalette(palette);

    vid_menudrawfn = VID_MenuDraw;
    vid_menukeyfn = VID_MenuKey;
    did_init = true;
}

void VID_Shutdown(void) {
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    QGL_Deinit ();
	DeinitEGL();
}

void GL_BeginRendering(int *x, int *y, int *width, int *height) {
    *x = *y = 0;
    *width = vid.width;
    *height = vid.height;
}

void GL_EndRendering(void) {
    glFlush();
    eglSwapBuffers(s_display, s_surface);
}

void VID_SetPalette(const byte *palette) {
    unsigned i, r, g, b, pixel;

    switch (gl_solid_format) {
    case GL_RGB:
    case GL_RGBA:
        for (i = 0; i < 256; i++) {
            r = palette[0];
            g = palette[1];
            b = palette[2];
            palette += 3;
            pixel = (0xff << 24) | (r << 0) | (g << 8) | (b << 16);
            d_8to24table[i] = LittleLong(pixel);
        }
        break;
    default:
        Sys_Error("%s: unsupported texture format (%d)", __func__, gl_solid_format);
    }
}

void VID_ShiftPalette(const byte *palette) {
    /* Done via gl_polyblend instead */
    // VID_SetPalette(palette);
}
