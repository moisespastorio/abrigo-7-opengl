/*
 * ABRIGO 7 - mini jogo de terror em C
 * OpenGL 3.3 core + GLFW + GLAD (v1) + cglm
 *
 * Controles: WASD mover | Shift correr | Mouse olhar | E interagir
 *            0-9 digitar no teclado numerico | Backspace apagar | ESC sair
 *
 * Os textos da historia aparecem no TERMINAL (e um resumo na barra de titulo).
 */
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <cglm/cglm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#ifdef _WIN32
#include <windows.h>
#endif

#define MW 30
#define MH 15
#define CELL 2.0f
#define WALL_H 3.0f
#define EYE_H 1.6f

/* ---------------------------------------------------------------
 * MAPA  (# parede | D porta com cartao | E porta eletrica
 *        X porta com teclado | b caixote/cama | G gerador | T mesa)
 * Cada celula tem 2x2 unidades.
 * --------------------------------------------------------------- */
static const char *LEVEL[MH] = {
    "##############################",
    "###########.......############",
    "###########.......############",
    "###########...G...############",
    "###########.......############",
    "#......####.......######.....#",
    "#......#######.#########..T..#",
    "#......D...............X..T..#",
    "#b.....#######E#########..T..#",
    "#b....b####.......######.....#",
    "###########.......############",
    "###########.bb....############",
    "###########.......############",
    "###########.......############",
    "##############################",
};
static char map[MH][MW + 1];

/* ---------------------------------------------------------------
 * ESTADO DO JOGO
 * --------------------------------------------------------------- */
static float px, pz;                 /* posicao do jogador */
static float yaw = 0.0f, pitch = 0.0f;
static int hasKey = 0, power = 0, state = 0; /* state: 0 jogando, 1 fim */
static int leverSeq = 0;
static const int LEVER_ORDER[3] = {2, 0, 1}; /* azul, vermelha, verde */
static const char *CODE = "4721";
static char code[8] = "";
static int codeLen = 0;
static int nearKeypad = 0;

static char statusMsg[256] = "";
static float msgTimer = 0.0f;
static const char *objective = "Descobrir onde estou";

/* ---------------------------------------------------------------
 * OBJETOS INTERATIVOS
 * --------------------------------------------------------------- */
typedef enum { O_KEY, O_NOTE, O_LEVER, O_DOOR, O_DIARY, O_SHADOW } OType;
typedef struct {
    OType type;
    int id;
    float x, y, z;
    float sx, sy, sz;
    float r, g, b;
    int visible, done;
} Obj;
static Obj objs[16];
static int nobjs = 0;

static void addObj(OType t, int id, float cx, float cz, float y,
                   float sx, float sy, float sz,
                   float r, float g, float b, int vis)
{
    Obj *o = &objs[nobjs++];
    o->type = t; o->id = id;
    o->x = cx * CELL; o->z = cz * CELL; o->y = y;
    o->sx = sx; o->sy = sy; o->sz = sz;
    o->r = r; o->g = g; o->b = b;
    o->visible = vis; o->done = 0;
}

/* zonas que disparam falas do protagonista */
typedef struct { float cx, cz, r; const char *msg; int fired; } Trig;
static Trig trigs[] = {
    {10.5f, 7.5f, 1.5f, "Este corredor... o Teo sempre assobiava por aqui. Hoje nao ouco nada. Ele deve estar no outro setor.", 0},
    {14.5f, 5.5f, 1.5f, "A sala do gerador. O Teo adorava reclamar daquela coisa. Se eu religar, ele volta mais rapido, tenho certeza.", 0},
    {14.5f, 9.5f, 1.5f, "Os arquivos. O Teo organizava tudo por cor. Ele e tao certinho... Onde sera que ele se enfiou?", 0},
    {24.5f, 7.5f, 1.8f, "Que frio. A comida ficava aqui. Teo disse que ainda sobrava bastante. Por que minha boca tem esse gosto estranho?", 0},
};
#define NTRIG ((int)(sizeof(trigs) / sizeof(trigs[0])))

static void say(const char *text)
{
    printf("\n%s\n", text);
    fflush(stdout);
    snprintf(statusMsg, sizeof statusMsg, "%s", text);
    msgTimer = 7.0f;
}

/* ---------------------------------------------------------------
 * COLISAO
 * --------------------------------------------------------------- */
static int solidAt(float x, float z)
{
    int c = (int)floorf(x / CELL), r = (int)floorf(z / CELL);
    if (c < 0 || r < 0 || c >= MW || r >= MH) return 1;
    char t = map[r][c];
    return t == '#' || t == 'D' || t == 'E' || t == 'X' || t == 'b' || t == 'G' || t == 'T';
}
static int blocked(float x, float z)
{
    const float R = 0.35f;
    return solidAt(x - R, z - R) || solidAt(x + R, z - R) ||
           solidAt(x - R, z + R) || solidAt(x + R, z + R);
}

/* ---------------------------------------------------------------
 * INTERACAO
 * --------------------------------------------------------------- */
static int findTarget(void)
{
    float best = 1e9f;
    int bi = -1;
    float fx = cosf(yaw), fz = sinf(yaw);
    for (int i = 0; i < nobjs; i++) {
        Obj *o = &objs[i];
        if (o->type == O_SHADOW || o->done) continue;
        float dx = o->x - px, dz = o->z - pz;
        float d = sqrtf(dx * dx + dz * dz);
        if (d > 2.8f) continue;
        float dot = d > 0.01f ? (dx * fx + dz * fz) / d : 1.0f;
        if (dot < 0.5f && d > 1.0f) continue;
        if (d < best) { best = d; bi = i; }
    }
    return bi;
}

static const char *promptFor(OType t)
{
    switch (t) {
    case O_KEY:   return "[E] Pegar cartao de acesso";
    case O_DOOR:  return "[E] Abrir porta";
    case O_NOTE:  return "[E] Ler papel";
    case O_LEVER: return "[E] Puxar disjuntor";
    case O_DIARY: return "[E] Ler o diario";
    default:      return "";
    }
}

static void readNote(int id)
{
    switch (id) {
    case 0:
        say("Bilhete: \"Marcos, fui checar o setor B. Volto logo. Nao saia sozinho. - Teo\"  (a tinta parece estranhamente fresca)");
        break;
    case 1:
        say("Bilhete: \"O gerador so liga se os disjuntores forem puxados na ordem: AZUL, VERMELHO, VERDE. Nao erre. - Teo\"");
        break;
    case 2:
        say("Papel amassado: \"Camara fria - 1a metade do codigo: 4 7 _ _\"");
        objective = "Achar a outra metade do codigo da camara fria";
        break;
    case 3:
        say("Pagina de manual: \"Camara fria - 2a metade do codigo: _ _ 2 1\"");
        objective = "Digitar o codigo no teclado da porta no fim do corredor";
        break;
    }
}

static void pullLever(Obj *o)
{
    if (o->id == LEVER_ORDER[leverSeq]) {
        o->done = 1;
        leverSeq++;
        if (leverSeq == 3) {
            power = 1;
            map[8][14] = '.'; /* abre a porta eletrica do arquivo */
            for (int i = 0; i < nobjs; i++)
                if (objs[i].type == O_SHADOW) objs[i].visible = 1;
            say("CLUNK! As luzes tremem e o gerador ronca. Uma porta se abriu ao sul, no corredor.");
            objective = "Explorar a area que ganhou energia";
        } else {
            say("Clique metalico. O disjuntor travou na posicao.");
        }
    } else {
        for (int i = 0; i < nobjs; i++)
            if (objs[i].type == O_LEVER) objs[i].done = 0;
        leverSeq = 0;
        say("FZZZT! Faisca! Ordem errada, os disjuntores voltaram ao inicio.");
    }
}

static void ending(void)
{
    state = 1;
    printf("\n=====================================================\n");
    printf(" DIARIO DO MARCOS - ABRIGO 7\n");
    printf("=====================================================\n");
    printf(" Dia 23: A comida acabou. O Teo disse que ia dar um jeito.\n");
    printf(" Dia 30: O Teo parou de se mexer. Eu disse a mim mesmo que ele estava dormindo.\n");
    printf(" Dia 33: A fome nao e mais uma fome. E uma voz.\n");
    printf(" Dia 34: Levei o Teo para a camara fria. Para conservar.\n");
    printf(" Dia 41: Eu nao estou sozinho. O Teo saiu para o setor B.\n");
    printf("         Ele deixa bilhetes para mim. Eu escrevo todos eles.\n");
    printf("\n A letra tremida nos bilhetes... sempre foi a minha.\n");
    printf(" Nao existe setor B.\n");
    printf(" O Teo nunca saiu do abrigo.\n");
    printf("\n                       F I M\n");
    printf("=====================================================\n");
    printf(" (ESC para sair)\n");
    fflush(stdout);
    snprintf(statusMsg, sizeof statusMsg, "FIM - O Teo nunca saiu do abrigo. (ESC para sair)");
    msgTimer = 1e9f;
}

static void interact(int i)
{
    Obj *o = &objs[i];
    switch (o->type) {
    case O_KEY:
        hasKey = 1; o->visible = 0; o->done = 1;
        say("Peguei um cartao de acesso vermelho. Deve abrir a porta do dormitorio.");
        objective = "Abrir a porta vermelha do dormitorio";
        break;
    case O_DOOR:
        if (!hasKey) {
            say("Trancada. Ha um leitor de cartao vermelho ao lado.");
        } else {
            map[7][7] = '.'; o->done = 1;
            say("A porta destrava com um rangido. O corredor esta escuro.");
            objective = "Religar a energia (gerador ao norte)";
        }
        break;
    case O_NOTE:  readNote(o->id); break;
    case O_LEVER: pullLever(o);    break;
    case O_DIARY: o->done = 1; ending(); break;
    default: break;
    }
}

/* ---------------------------------------------------------------
 * CALLBACKS
 * --------------------------------------------------------------- */
static void mouse_cb(GLFWwindow *w, double x, double y)
{
    static int first = 1;
    static double lx, ly;
    (void)w;
    if (first) { lx = x; ly = y; first = 0; }
    yaw += (float)(x - lx) * 0.0022f;
    pitch += (float)(ly - y) * 0.0022f;
    lx = x; ly = y;
    if (pitch > 1.5f) pitch = 1.5f;
    if (pitch < -1.5f) pitch = -1.5f;
}

static void key_cb(GLFWwindow *w, int key, int sc, int action, int mods)
{
    (void)sc; (void)mods;
    if (action != GLFW_PRESS) return;
    if (key == GLFW_KEY_ESCAPE) glfwSetWindowShouldClose(w, 1);
    if (key == GLFW_KEY_BACKSPACE) { codeLen = 0; code[0] = 0; }
    if (key == GLFW_KEY_E && state == 0) {
        int t = findTarget();
        if (t >= 0) interact(t);
        else if (nearKeypad) say("Teclado numerico. Digite os 4 digitos do codigo.");
    }
}

static void char_cb(GLFWwindow *w, unsigned int c)
{
    (void)w;
    if (!nearKeypad || state != 0) return;
    if (c < '0' || c > '9' || codeLen >= 4) return;
    code[codeLen++] = (char)c;
    code[codeLen] = 0;
    if (codeLen == 4) {
        if (strcmp(code, CODE) == 0) {
            map[7][23] = '.';
            say("Bip! Tranca aberta. O ar gelado escapa pela fresta...");
            objective = "Entrar na camara fria";
        } else {
            say("BZZT. Codigo incorreto.");
        }
        codeLen = 0; code[0] = 0;
    }
}

/* ---------------------------------------------------------------
 * SHADERS
 * --------------------------------------------------------------- */
static const char *VS =
    "#version 330 core\n"
    "layout(location=0) in vec3 aPos;\n"
    "layout(location=1) in vec3 aNormal;\n"
    "uniform mat4 uModel, uView, uProj;\n"
    "out vec3 vWorld; out vec3 vNormal;\n"
    "void main(){\n"
    "  vec4 w = uModel * vec4(aPos, 1.0);\n"
    "  vWorld = w.xyz;\n"
    "  vNormal = mat3(transpose(inverse(uModel))) * aNormal;\n"
    "  gl_Position = uProj * uView * w;\n"
    "}\n";

static const char *FS =
    "#version 330 core\n"
    "in vec3 vWorld; in vec3 vNormal;\n"
    "out vec4 FragColor;\n"
    "uniform vec3 uColor, uCamPos, uCamDir, uTint;\n"
    "uniform float uAmbient, uFlash, uEmissive;\n"
    "void main(){\n"
    "  vec3 N = normalize(vNormal);\n"
    "  vec3 toL = uCamPos - vWorld;\n"
    "  float d = length(toL);\n"
    "  vec3 L = toL / d;\n"
    "  float diff = 0.3 + 0.7 * max(dot(N, L), 0.0);\n"
    "  float spot = smoothstep(0.80, 0.94, dot(-L, normalize(uCamDir)));\n"
    "  float att = 1.0 / (1.0 + 0.12 * d + 0.06 * d * d);\n"
    "  float light = uAmbient + uFlash * (0.25 * att + 0.9 * spot * att) * diff;\n"
    "  vec3 col = uColor * light * uTint + uColor * uEmissive;\n"
    "  FragColor = vec4(col * exp(-0.05 * d), 1.0);\n"
    "}\n";

static GLuint compile(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[1024]; glGetShaderInfoLog(s, 1024, NULL, log); fprintf(stderr, "Shader: %s\n", log); exit(1); }
    return s;
}

static GLint uModel, uView, uProj, uColor, uCamPos, uCamDir, uTint, uAmbient, uFlash, uEmissive;

/* cubo unitario centrado na origem: 36 vertices (pos + normal) */
static const float CUBE[] = {
    -.5f,-.5f,-.5f, 0,0,-1,   .5f,-.5f,-.5f, 0,0,-1,   .5f, .5f,-.5f, 0,0,-1,
     .5f, .5f,-.5f, 0,0,-1,  -.5f, .5f,-.5f, 0,0,-1,  -.5f,-.5f,-.5f, 0,0,-1,
    -.5f,-.5f, .5f, 0,0, 1,   .5f,-.5f, .5f, 0,0, 1,   .5f, .5f, .5f, 0,0, 1,
     .5f, .5f, .5f, 0,0, 1,  -.5f, .5f, .5f, 0,0, 1,  -.5f,-.5f, .5f, 0,0, 1,
    -.5f, .5f, .5f,-1,0,0,   -.5f, .5f,-.5f,-1,0,0,   -.5f,-.5f,-.5f,-1,0,0,
    -.5f,-.5f,-.5f,-1,0,0,   -.5f,-.5f, .5f,-1,0,0,   -.5f, .5f, .5f,-1,0,0,
     .5f, .5f, .5f, 1,0,0,    .5f, .5f,-.5f, 1,0,0,    .5f,-.5f,-.5f, 1,0,0,
     .5f,-.5f,-.5f, 1,0,0,    .5f,-.5f, .5f, 1,0,0,    .5f, .5f, .5f, 1,0,0,
    -.5f,-.5f,-.5f, 0,-1,0,   .5f,-.5f,-.5f, 0,-1,0,   .5f,-.5f, .5f, 0,-1,0,
     .5f,-.5f, .5f, 0,-1,0,  -.5f,-.5f, .5f, 0,-1,0,  -.5f,-.5f,-.5f, 0,-1,0,
    -.5f, .5f,-.5f, 0, 1,0,   .5f, .5f,-.5f, 0, 1,0,   .5f, .5f, .5f, 0, 1,0,
     .5f, .5f, .5f, 0, 1,0,  -.5f, .5f, .5f, 0, 1,0,  -.5f, .5f,-.5f, 0, 1,0,
};

static void box(float x, float y, float z, float sx, float sy, float sz,
                float r, float g, float b, float emissive)
{
    mat4 m;
    glm_mat4_identity(m);
    glm_translate(m, (vec3){x, y, z});
    glm_scale(m, (vec3){sx, sy, sz});
    glUniformMatrix4fv(uModel, 1, GL_FALSE, (float *)m);
    glUniform3f(uColor, r, g, b);
    glUniform1f(uEmissive, emissive);
    glDrawArrays(GL_TRIANGLES, 0, 36);
}

static int isWallCell(int c, int r)
{
    if (c < 0 || r < 0 || c >= MW || r >= MH) return 1;
    return map[r][c] == '#';
}

static void drawWorld(void)
{
    /* chao e teto */
    box(MW * CELL / 2, -0.05f, MH * CELL / 2, MW * CELL, 0.1f, MH * CELL, 0.22f, 0.22f, 0.24f, 0);
    box(MW * CELL / 2, WALL_H + 0.05f, MH * CELL / 2, MW * CELL, 0.1f, MH * CELL, 0.28f, 0.28f, 0.28f, 0);

    for (int r = 0; r < MH; r++) {
        for (int c = 0; c < MW; c++) {
            char t = map[r][c];
            float x = c * CELL + CELL / 2, z = r * CELL + CELL / 2;
            float v = 0.02f * ((c * 7 + r * 13) % 5);
            switch (t) {
            case '#':
                if (isWallCell(c - 1, r) && isWallCell(c + 1, r) &&
                    isWallCell(c, r - 1) && isWallCell(c, r + 1)) break; /* interna */
                box(x, WALL_H / 2, z, CELL, WALL_H, CELL, 0.42f + v, 0.45f + v, 0.42f + v, 0);
                break;
            case 'D': box(x, WALL_H / 2, z, CELL, WALL_H, CELL, 0.55f, 0.10f, 0.10f, 0.05f); break;
            case 'E': box(x, WALL_H / 2, z, CELL, WALL_H, CELL, 0.10f, 0.20f, 0.50f, power ? 0.0f : 0.05f); break;
            case 'X':
                box(x, WALL_H / 2, z, CELL, WALL_H, CELL, 0.35f, 0.35f, 0.40f, 0);
                box(x - CELL / 2 - 0.06f, 1.3f, z, 0.12f, 0.35f, 0.35f, 0.1f, 0.9f, 0.2f, 0.7f); /* teclado */
                break;
            case 'b': box(x, 0.5f, z, 1.6f, 1.0f, 1.6f, 0.35f, 0.22f, 0.12f, 0); break;
            case 'G': box(x, 0.9f, z, 1.8f, 1.8f, 1.8f, 0.20f, 0.30f, 0.20f, power ? 0.25f : 0.0f); break;
            case 'T': box(x, 0.45f, z, 1.8f, 0.9f, 1.8f, 0.50f, 0.50f, 0.55f, 0); break;
            default: break;
            }
        }
    }

    /* "restos" sobre a mesa da camara fria */
    float tx = 26.5f * CELL;
    box(tx, 1.00f, 6.5f * CELL - 0.2f, 0.60f, 0.25f, 0.45f, 0.35f, 0.02f, 0.02f, 0);
    box(tx + 0.2f, 1.00f, 7.5f * CELL - 0.1f, 0.70f, 0.20f, 0.50f, 0.30f, 0.02f, 0.02f, 0);
    box(tx - 0.2f, 1.00f, 8.5f * CELL - 0.2f, 0.50f, 0.25f, 0.40f, 0.38f, 0.03f, 0.03f, 0);
    box(tx + 0.3f, 0.98f, 6.5f * CELL + 0.5f, 0.12f, 0.12f, 0.70f, 0.85f, 0.85f, 0.78f, 0); /* ossos */
    box(tx - 0.3f, 0.98f, 7.5f * CELL + 0.7f, 0.12f, 0.12f, 0.60f, 0.85f, 0.85f, 0.78f, 0);
    box(tx + 0.1f, 1.15f, 7.5f * CELL - 0.8f, 0.45f, 0.30f, 0.45f, 0.15f, 0.15f, 0.17f, 0); /* panela */

    /* objetos */
    for (int i = 0; i < nobjs; i++) {
        Obj *o = &objs[i];
        if (!o->visible) continue;
        float em = 0.55f;
        float r = o->r, g = o->g, b = o->b;
        if (o->type == O_SHADOW) em = 0.0f;
        if (o->type == O_LEVER) {
            if (o->done) { r *= 1.0f; g *= 1.0f; b *= 1.0f; em = 1.0f; box(o->x, 0.9f, o->z, o->sx, 0.5f, o->sz, r, g, b, em); continue; }
        }
        box(o->x, o->y, o->z, o->sx, o->sy, o->sz, r, g, b, em);
    }
}

/* ---------------------------------------------------------------
 * MAIN
 * --------------------------------------------------------------- */
int main(void)
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    for (int r = 0; r < MH; r++) {
        if (strlen(LEVEL[r]) != MW) { fprintf(stderr, "Linha %d do mapa com tamanho errado\n", r); return 1; }
        strcpy(map[r], LEVEL[r]);
    }

    /* objetos: (tipo, id, celulaX, celulaZ, y, tamanho xyz, cor rgb, visivel) */
    addObj(O_NOTE,  0,  1.5f,  5.5f, 0.06f, 0.40f, 0.03f, 0.50f, 1.0f, 0.9f, 0.4f, 1);
    addObj(O_NOTE,  1,  3.5f,  9.5f, 0.06f, 0.40f, 0.03f, 0.50f, 1.0f, 0.9f, 0.4f, 1);
    addObj(O_KEY,   0,  6.5f,  5.5f, 0.15f, 0.30f, 0.10f, 0.50f, 0.9f, 0.1f, 0.1f, 1);
    addObj(O_DOOR,  0,  7.5f,  7.5f, 0.0f,  0, 0, 0, 0, 0, 0, 0);
    addObj(O_LEVER, 0, 12.5f, 1.08f, 1.2f, 0.25f, 0.90f, 0.25f, 0.9f, 0.1f, 0.1f, 1); /* vermelha */
    addObj(O_LEVER, 1, 14.5f, 1.08f, 1.2f, 0.25f, 0.90f, 0.25f, 0.1f, 0.9f, 0.1f, 1); /* verde    */
    addObj(O_LEVER, 2, 16.5f, 1.08f, 1.2f, 0.25f, 0.90f, 0.25f, 0.1f, 0.2f, 1.0f, 1); /* azul     */
    addObj(O_NOTE,  2, 17.5f,  4.5f, 0.06f, 0.40f, 0.03f, 0.50f, 1.0f, 0.9f, 0.4f, 1);
    addObj(O_NOTE,  3, 16.5f, 12.5f, 0.06f, 0.40f, 0.03f, 0.50f, 1.0f, 0.9f, 0.4f, 1);
    addObj(O_DIARY, 0, 28.5f,  7.5f, 0.06f, 0.50f, 0.03f, 0.60f, 1.0f, 0.3f, 0.3f, 1);
    addObj(O_SHADOW,0, 21.5f,  7.5f, 1.0f,  0.70f, 2.00f, 0.40f, 0.0f, 0.0f, 0.0f, 0);

    px = 3.5f * CELL; pz = 7.5f * CELL;

    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    GLFWwindow *win = glfwCreateWindow(1024, 640, "ABRIGO 7", NULL, NULL);
    if (!win) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) { fprintf(stderr, "Falha no GLAD\n"); return 1; }
    glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwSetCursorPosCallback(win, mouse_cb);
    glfwSetKeyCallback(win, key_cb);
    glfwSetCharCallback(win, char_cb);

    GLuint vs = compile(GL_VERTEX_SHADER, VS), fs = compile(GL_FRAGMENT_SHADER, FS);
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs); glAttachShader(prog, fs); glLinkProgram(prog);
    glDeleteShader(vs); glDeleteShader(fs);
    uModel = glGetUniformLocation(prog, "uModel");   uView = glGetUniformLocation(prog, "uView");
    uProj = glGetUniformLocation(prog, "uProj");     uColor = glGetUniformLocation(prog, "uColor");
    uCamPos = glGetUniformLocation(prog, "uCamPos"); uCamDir = glGetUniformLocation(prog, "uCamDir");
    uTint = glGetUniformLocation(prog, "uTint");     uAmbient = glGetUniformLocation(prog, "uAmbient");
    uFlash = glGetUniformLocation(prog, "uFlash");   uEmissive = glGetUniformLocation(prog, "uEmissive");

    GLuint vao, vbo;
    glGenVertexArrays(1, &vao); glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof CUBE, CUBE, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void *)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glEnable(GL_DEPTH_TEST);

    printf("=====================================================\n");
    printf("  ABRIGO 7\n");
    printf("  WASD mover | Shift correr | Mouse olhar | E interagir\n");
    printf("  Digite numeros perto de um teclado | ESC sair\n");
    printf("=====================================================\n");
    say("Acordei no chao do dormitorio. Minha cabeca doi. O abrigo esta em silencio... Teo? TEO! Ele deve ter ido buscar comida. Ele sempre volta.");

    double last = glfwGetTime();
    float walk = 0.0f;
    char lastTitle[320] = "";

    while (!glfwWindowShouldClose(win)) {
        double now = glfwGetTime();
        float dt = (float)(now - last);
        last = now;
        if (dt > 0.1f) dt = 0.1f;
        glfwPollEvents();

        /* movimento */
        if (state == 0) {
            float fx = cosf(yaw), fz = sinf(yaw);
            float rx = -fz, rz = fx;
            float mx = 0, mz = 0;
            if (glfwGetKey(win, GLFW_KEY_W) == GLFW_PRESS) { mx += fx; mz += fz; }
            if (glfwGetKey(win, GLFW_KEY_S) == GLFW_PRESS) { mx -= fx; mz -= fz; }
            if (glfwGetKey(win, GLFW_KEY_D) == GLFW_PRESS) { mx += rx; mz += rz; }
            if (glfwGetKey(win, GLFW_KEY_A) == GLFW_PRESS) { mx -= rx; mz -= rz; }
            float len = sqrtf(mx * mx + mz * mz);
            if (len > 0.001f) {
                float speed = (glfwGetKey(win, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) ? 4.5f : 2.6f;
                mx = mx / len * speed * dt; mz = mz / len * speed * dt;
                if (!blocked(px + mx, pz)) px += mx;
                if (!blocked(px, pz + mz)) pz += mz;
                walk += dt * speed;
            }
        }

        /* gatilhos de fala */
        for (int i = 0; i < NTRIG; i++) {
            Trig *t = &trigs[i];
            float dx = px - t->cx * CELL, dz = pz - t->cz * CELL;
            if (!t->fired && sqrtf(dx * dx + dz * dz) < t->r * CELL) { t->fired = 1; say(t->msg); }
        }

        /* sombra misteriosa no corredor */
        for (int i = 0; i < nobjs; i++) {
            Obj *o = &objs[i];
            if (o->type == O_SHADOW && o->visible) {
                float dx = px - o->x, dz = pz - o->z;
                if (sqrtf(dx * dx + dz * dz) < 7.0f) {
                    o->visible = 0;
                    say("Tinha alguem ali no fim do corredor... Teo? ...Nao. So minha cabeca pregando pecas.");
                }
            }
        }

        nearKeypad = (map[7][23] == 'X') &&
                     (hypotf(px - (23 * CELL - 0.06f), pz - (7.5f * CELL)) < 3.0f);
        int target = (state == 0) ? findTarget() : -1;

        if (msgTimer > 0 && state == 0) msgTimer -= dt;

        /* titulo da janela (texto de ajuda) */
        char title[320];
        if (state == 1)                 snprintf(title, sizeof title, "ABRIGO 7 | %s", statusMsg);
        else if (codeLen > 0)           snprintf(title, sizeof title, "ABRIGO 7 | Teclado: [%s]", code);
        else if (msgTimer > 0)          snprintf(title, sizeof title, "ABRIGO 7 | %s", statusMsg);
        else if (target >= 0)           snprintf(title, sizeof title, "ABRIGO 7 | %s", promptFor(objs[target].type));
        else if (nearKeypad)            snprintf(title, sizeof title, "ABRIGO 7 | Teclado numerico: digite 4 digitos");
        else                            snprintf(title, sizeof title, "ABRIGO 7 | Objetivo: %s", objective);
        if (strcmp(title, lastTitle) != 0) { glfwSetWindowTitle(win, title); strcpy(lastTitle, title); }

        /* iluminacao: lanterna com falhas */
        float t = (float)now;
        float flick = 1.0f;
        if (sinf(t * 23.0f) * sinf(t * 7.3f) * sinf(t * 3.1f) > 0.5f) flick = 0.15f;
        float ambient = 0.05f + (power ? 0.07f : 0.0f);
        vec3 tint = {1, 1, 1};
        if (state == 1) {
            float pulse = 0.5f + 0.5f * sinf(t * 3.0f);
            tint[0] = 1.8f; tint[1] = 0.35f; tint[2] = 0.35f;
            ambient = 0.10f + 0.10f * pulse;
            if (sinf(t * 31.0f) > 0.6f) flick *= 0.4f;
        }

        /* camera */
        int fbw, fbh; glfwGetFramebufferSize(win, &fbw, &fbh);
        if (fbh == 0) fbh = 1;
        float bob = sinf(walk * 3.2f) * 0.03f;
        vec3 eye = {px, EYE_H + bob, pz};
        vec3 front = {cosf(yaw) * cosf(pitch), sinf(pitch), sinf(yaw) * cosf(pitch)};
        vec3 center; glm_vec3_add(eye, front, center);
        mat4 view, proj;
        glm_lookat(eye, center, (vec3){0, 1, 0}, view);
        glm_perspective(glm_rad(70.0f), (float)fbw / (float)fbh, 0.05f, 100.0f, proj);

        glViewport(0, 0, fbw, fbh);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glUseProgram(prog);
        glUniformMatrix4fv(uView, 1, GL_FALSE, (float *)view);
        glUniformMatrix4fv(uProj, 1, GL_FALSE, (float *)proj);
        glUniform3fv(uCamPos, 1, eye);
        glUniform3fv(uCamDir, 1, front);
        glUniform3fv(uTint, 1, tint);
        glUniform1f(uAmbient, ambient);
        glUniform1f(uFlash, 1.6f * flick);
        glBindVertexArray(vao);
        drawWorld();

        glfwSwapBuffers(win);
    }

    glfwTerminate();
    return 0;
}
