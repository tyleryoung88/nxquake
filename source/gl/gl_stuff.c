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

#include "glquake.h"
#include "glshaders.h"
#include "quakedef.h"
#include "sys.h"
//#include <cglm/cglm.h>
#include <stdint.h>
#include <stdlib.h>

//
// shaders
//

static GLuint fs[9];
static GLuint vs[4];
static GLuint programs[9];
static GLuint cur_program = (GLuint)-1;

static qboolean just_color; // HACK

static int state_mask = 0x00;
static int texenv_mask = 0;
static int texcoord_state = 0;
static int alpha_state = 0;
static int color_state = 0;
static GLfloat cur_color[4] = {1.f, 1.f, 1.f, 1.f};
static GLfloat cur_texcoord[2] = {0.f, 0.f};
static GLint u_mvp[9];
static GLint u_monocolor;
static GLint u_modcolor[2];

static qboolean shaders_loaded = false;
static qboolean shaders_reload = false;

static qboolean program_changed = true;

static void CompileShader(const char *src, GLuint idx, GLboolean frag) {
    static GLchar msg[2048];
    GLint status;

    GLuint s = glCreateShader(frag ? GL_FRAGMENT_SHADER : GL_VERTEX_SHADER);
    if (!s)
        Sys_Error("Could not create %s shader #%d!", frag ? "frag" : "vert", idx);

    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    glGetShaderiv(s, GL_COMPILE_STATUS, &status);
    if (status == GL_FALSE) {
        glGetShaderInfoLog(s, sizeof(msg), NULL, msg);
        glDeleteShader(s);
        Sys_Error("Could not compile %s shader #%d:\n%s", frag ? "frag" : "vert", idx, msg);
    }

    if (frag)
        fs[idx] = s;
    else
        vs[idx] = s;
}

static inline void LinkShader(GLuint pidx, GLuint fidx, GLuint vidx, GLboolean texcoord, GLboolean color) {
    static GLchar msg[2048];
    programs[pidx] = glCreateProgram();
    glAttachShader(programs[pidx], fs[fidx]);
    glAttachShader(programs[pidx], vs[vidx]);
    // vglBindAttribLocation( programs[pidx], 0, "aPosition", 3, GL_FLOAT );
    // if( texcoord ) vglBindAttribLocation( programs[pidx], 1, "aTexCoord", 2,
    // GL_FLOAT ); if( color ) vglBindAttribLocation( programs[pidx], 2,
    // "aColor", 4, GL_FLOAT );
    glLinkProgram(programs[pidx]);
    GLint success;
    glGetProgramiv(programs[pidx], GL_LINK_STATUS, &success);
    if (success == GL_FALSE) {
        glGetProgramInfoLog(programs[pidx], sizeof(msg), NULL, msg);
        glDeleteProgram(programs[pidx]);
        Sys_Error("Could not link shader program #%d:\n%s", pidx, msg);
    }
}

void QGL_FreeShaders(void) {
    if (shaders_loaded) {
        for (int i = 0; i < 9; i++)
            glDeleteProgram(programs[i]);
        for (int i = 0; i < 9; i++)
            glDeleteShader(fs[i]);
        for (int i = 0; i < 4; i++)
            glDeleteShader(vs[i]);
        shaders_loaded = false;
    }
}

void QGL_ReloadShaders(void) {
    // glFinish( );
    QGL_FreeShaders();

    CompileShader(QGL_SHADER_MODULATE_SRC, QGL_SHADER_MODULATE, GL_TRUE);
    CompileShader(QGL_SHADER_MODULATE_COLOR_SRC, QGL_SHADER_MODULATE_COLOR, GL_TRUE);
    CompileShader(QGL_SHADER_REPLACE_SRC, QGL_SHADER_REPLACE, GL_TRUE);
    CompileShader(QGL_SHADER_MODULATE_A_SRC, QGL_SHADER_MODULATE_A, GL_TRUE);
    CompileShader(QGL_SHADER_MODULATE_COLOR_A_SRC, QGL_SHADER_MODULATE_COLOR_A, GL_TRUE);
    CompileShader(QGL_SHADER_REPLACE_A_SRC, QGL_SHADER_REPLACE_A, GL_TRUE);
    CompileShader(QGL_SHADER_TEXTURE2D_SRC, QGL_SHADER_TEXTURE2D, GL_FALSE);
    CompileShader(QGL_SHADER_TEXTURE2D_WITH_COLOR_SRC, QGL_SHADER_TEXTURE2D_WITH_COLOR, GL_FALSE);
    CompileShader(QGL_SHADER_RGBA_COLOR_SRC, QGL_SHADER_RGBA_COLOR, GL_TRUE);
    CompileShader(QGL_SHADER_MONO_COLOR_SRC, QGL_SHADER_MONO_COLOR, GL_TRUE);
    CompileShader(QGL_SHADER_RGBA_A_SRC, QGL_SHADER_RGBA_A, GL_TRUE);
    CompileShader(QGL_SHADER_COLOR_SRC, QGL_SHADER_COLOR, GL_FALSE);
    CompileShader(QGL_SHADER_VERTEX_ONLY_SRC, QGL_SHADER_VERTEX_ONLY, GL_FALSE);

    LinkShader(QGL_SHADER_TEX2D_REPL, QGL_SHADER_REPLACE, QGL_SHADER_TEXTURE2D, GL_TRUE, GL_FALSE);
    LinkShader(QGL_SHADER_TEX2D_MODUL, QGL_SHADER_MODULATE, QGL_SHADER_TEXTURE2D, GL_TRUE, GL_FALSE);
    LinkShader(QGL_SHADER_TEX2D_MODUL_CLR, QGL_SHADER_MODULATE_COLOR, QGL_SHADER_TEXTURE2D_WITH_COLOR, GL_TRUE, GL_TRUE);
    LinkShader(QGL_SHADER_RGBA_COLOR, QGL_SHADER_RGBA_COLOR, QGL_SHADER_COLOR, GL_FALSE, GL_TRUE);
    LinkShader(QGL_SHADER_NO_COLOR, QGL_SHADER_MONO_COLOR, QGL_SHADER_VERTEX_ONLY, GL_FALSE, GL_FALSE);
    LinkShader(QGL_SHADER_TEX2D_REPL_A, QGL_SHADER_REPLACE_A, QGL_SHADER_TEXTURE2D, GL_TRUE, GL_FALSE);
    LinkShader(QGL_SHADER_TEX2D_MODUL_A, QGL_SHADER_MODULATE_A, QGL_SHADER_TEXTURE2D, GL_TRUE, GL_FALSE);
    LinkShader(QGL_SHADER_FULL_A, QGL_SHADER_MODULATE_COLOR_A, QGL_SHADER_TEXTURE2D_WITH_COLOR, GL_TRUE, GL_TRUE);
    LinkShader(QGL_SHADER_RGBA_CLR_A, QGL_SHADER_RGBA_A, QGL_SHADER_COLOR, GL_FALSE, GL_TRUE);

    for (GLuint p = 0; p < 9; ++p)
        u_mvp[p] = glGetUniformLocation(programs[p], "uMVP");
    u_modcolor[0] = glGetUniformLocation(programs[QGL_SHADER_TEX2D_MODUL], "uColor");
    u_modcolor[1] = glGetUniformLocation(programs[QGL_SHADER_TEX2D_MODUL_A], "uColor");
    u_monocolor = glGetUniformLocation(programs[QGL_SHADER_NO_COLOR], "uColor");
    // uTexture should be 0 by default, so it should be fine

    shaders_loaded = true;
}

void QGL_SetShader(void) {
    qboolean changed = true;
    just_color = false;

    switch (state_mask + texenv_mask) {
    case 0x00: // Everything off
    case 0x04: // Modulate
    case 0x08: // Alpha Test
    case 0x0C: // Alpha Test + Modulate
        just_color = true;
        glUseProgram(programs[QGL_SHADER_NO_COLOR]);
        cur_program = QGL_SHADER_NO_COLOR;
        break;
    case 0x01: // Texcoord
    case 0x03: // Texcoord + Color
        glUseProgram(programs[QGL_SHADER_TEX2D_REPL]);
        cur_program = QGL_SHADER_TEX2D_REPL;
        break;
    case 0x02: // Color
    case 0x06: // Color + Modulate
        glUseProgram(programs[QGL_SHADER_RGBA_COLOR]);
        cur_program = QGL_SHADER_RGBA_COLOR;
        break;
    case 0x05: // Modulate + Texcoord
        glUseProgram(programs[QGL_SHADER_TEX2D_MODUL]);
        cur_program = QGL_SHADER_TEX2D_MODUL;
        break;
    case 0x07: // Modulate + Texcoord + Color
        glUseProgram(programs[QGL_SHADER_TEX2D_MODUL_CLR]);
        cur_program = QGL_SHADER_TEX2D_MODUL_CLR;
        break;
    case 0x09: // Alpha Test + Texcoord
    case 0x0B: // Alpha Test + Color + Texcoord
        glUseProgram(programs[QGL_SHADER_TEX2D_REPL_A]);
        cur_program = QGL_SHADER_TEX2D_REPL_A;
        break;
    case 0x0A: // Alpha Test + Color
    case 0x0E: // Alpha Test + Modulate + Color
        glUseProgram(programs[QGL_SHADER_RGBA_CLR_A]);
        cur_program = QGL_SHADER_RGBA_CLR_A;
        break;
    case 0x0D: // Alpha Test + Modulate + Texcoord
        glUseProgram(programs[QGL_SHADER_TEX2D_MODUL_A]);
        cur_program = QGL_SHADER_TEX2D_MODUL_A;
        break;
    case 0x0F: // Alpha Test + Modulate + Texcoord + Color
        glUseProgram(programs[QGL_SHADER_FULL_A]);
        cur_program = QGL_SHADER_FULL_A;
        break;
    default:
        changed = false;
        break;
    }
    if (changed) {
        program_changed = true;
        // printf("PROGRAM CHANGE %u -> %u\n", old_program, cur_program);
    }
}

typedef struct {
    GLfloat pos[3];
    GLfloat uv[2];
    GLfloat color[4];
} bufvert_t;

qboolean QGL_Init(void) {
    QGL_ReloadShaders();

    glActiveTexture(GL_TEXTURE0);

    glEnableVertexAttribArray(0);

    return true;
}

void QGL_Deinit(void) {
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glDisableVertexAttribArray(2);
    QGL_FreeShaders();
}
