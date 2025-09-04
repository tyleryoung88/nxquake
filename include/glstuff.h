#ifndef __GLSTUFF_H
#define __GLSTUFF_H

#include "quakedef.h"

#define QGL_MAXVERTS 16384
#define QGL_MSTACKLEN 8

// internal shader shit

// fragment shaders
#define QGL_SHADER_MODULATE_COLOR 0
#define QGL_SHADER_MODULATE 1
#define QGL_SHADER_REPLACE 2
#define QGL_SHADER_RGBA_COLOR 3
#define QGL_SHADER_MONO_COLOR 4
#define QGL_SHADER_MODULATE_COLOR_A 5
#define QGL_SHADER_MODULATE_A 6
#define QGL_SHADER_RGBA_A 7
#define QGL_SHADER_REPLACE_A 8
// vertex shaders
#define QGL_SHADER_TEXTURE2D 0
#define QGL_SHADER_TEXTURE2D_WITH_COLOR 1
#define QGL_SHADER_COLOR 2
#define QGL_SHADER_VERTEX_ONLY 3
// shader programs
#define QGL_SHADER_TEX2D_REPL 0
#define QGL_SHADER_TEX2D_MODUL 1
#define QGL_SHADER_TEX2D_MODUL_CLR 2
// #define QGL_SHADER_RGBA_COLOR        3  // already defined above
#define QGL_SHADER_NO_COLOR 4
#define QGL_SHADER_TEX2D_REPL_A 5
#define QGL_SHADER_TEX2D_MODUL_A 6
#define QGL_SHADER_RGBA_CLR_A 7
#define QGL_SHADER_FULL_A 8

#define GL_TEXTURE0_ARB GL_TEXTURE0
#define GL_TEXTURE1_ARB GL_TEXTURE1

qboolean QGL_Init(void);
void QGL_Deinit(void);
void QGL_EndFrame(void);

#endif
