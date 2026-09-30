// Sprite index + lazily loaded texture sheets.
//
// sprites.bin (little endian, written by tools/export_sprites.py):
//   "SPR2" u16 nsprites u16 ngroups u32 nframes u32 npieces u32 nmaps u32 ncells
//   groups[ngroups]   { char name[32]; u8 sprite_sheets; u8 tile_sheets; }
//   sprites[nsprites] SpriteInfo
//   frames[nframes]   { u32 first; u16 count; }   first piece (NORMAL) or map index (TILEMAP)
//   pieces[npieces]   { u8 sheet; u16 x, y, w, h; s16 dx, dy; }  pixel rect in sheet, offset in frame
//   maps[nmaps]       { s16 x0, y0; u16 cols, rows; u32 first_cell; }
//   cells[ncells]     u32: tile index | FLIP_H (bit 30) | FLIP_V (bit 31); 0xFFFFFFFF = empty
#include "sprite.h"

#include <math.h>

#include "loader.h"
#include "tile_shbin.h"

static void tiles_init(void);
static int sync_frames = 30;  // sheets load synchronously (level start), else in the background
static int tb_used;  // tiles written to the batcher's buffer this frame

#define TILE 16
#define TILES_PER_ROW (512 / TILE)  // tools/export_sprites.py MAX_TEX
#define TILES_PER_SHEET (TILES_PER_ROW * TILES_PER_ROW)
#define CELL_EMPTY 0xFFFFFFFFu
#define CELL_FLIP_H (1u << 30)
#define CELL_FLIP_V (1u << 31)
#define CELL_OPAQUE (1u << 29)  // no transparent pixel
#define CELL_INDEX(c) ((c) & 0x1FFFFFFFu)

// Keep this much linear memory free for citro2d buffers and one incoming sheet.
#define LINEAR_RESERVE (2u * 1024 * 1024)

typedef struct PACKED { char name[32]; u8 sprite_sheets; u8 tile_sheets; } GroupRec;
typedef struct PACKED { u32 first; u16 count; } FrameRec;
typedef struct PACKED { u8 sheet; u16 x, y, w, h; s16 dx, dy; } PieceRec;
typedef struct PACKED { s16 x0, y0; u16 cols, rows; u32 first_cell; } MapRec;

typedef struct {
    C3D_Tex texobj;
    C3D_Tex *tex;           // &texobj while loaded, else NULL
    u32 last_used;
    bool missing;
} Sheet;

typedef struct {
    char name[32];
    int nsprite, ntile;
    Sheet *sprite_sheets;
    Sheet *tile_sheets;
} Group;

static u8 *blob;
static int nsprites, ngroups;
static const SpriteInfo *sprites;
static const FrameRec *frames;
static const PieceRec *pieces;
static const MapRec *maps;
static const u32 *cells;
static Group *groups;
static u32 frame_counter = 1;

static Sheet *all_sheets[1024];

// masks.bin: "MSK1" u16 count, then per mask: u16 sprite, u8 per_frame, u16 w, u16 h,
// u16 nblobs, nblobs x ((w*h+7)/8) bytes, bits LSB-first, row-major.
typedef struct { u16 w, h, nblobs; u8 per_frame; const u8 *bits; } Mask;
static u8 *mask_blob;
static Mask *mask_by_sprite[SPR_COUNT];

static void masks_init(void)
{
    size_t size;
    mask_blob = read_file("romfs:/data/masks.bin", &size);
    if (!mask_blob || memcmp(mask_blob, "MSK1", 4)) return;
    const u8 *p = mask_blob + 4;
    int count = TAKE(&p, u16);
    for (int i = 0; i < count; i++) {
        int spr = TAKE(&p, u16);
        Mask *m = calloc(1, sizeof(Mask));
        m->per_frame = TAKE(&p, u8);
        m->w = TAKE(&p, u16);
        m->h = TAKE(&p, u16);
        m->nblobs = TAKE(&p, u16);
        m->bits = p;
        p += (size_t)m->nblobs * ((m->w * m->h + 7) / 8);
        if (spr < SPR_COUNT) mask_by_sprite[spr] = m;
    }
}

bool sprite_has_mask(int spr)
{
    return spr >= 0 && spr < SPR_COUNT && mask_by_sprite[spr];
}

bool sprite_mask_test(int spr, int frame, int lx, int ly)
{
    const Mask *m = mask_by_sprite[spr];
    if (lx < 0 || ly < 0 || lx >= m->w || ly >= m->h) return false;
    int blob = 0;
    if (m->per_frame && m->nblobs) {
        blob = frame % m->nblobs;
        if (blob < 0) blob += m->nblobs;
    }
    const u8 *bits = m->bits + (size_t)blob * ((m->w * m->h + 7) / 8);
    u32 i = (u32)ly * m->w + lx;
    return bits[i >> 3] & (1 << (i & 7));
}
static int all_sheet_count;

void sprites_init(void)
{
    size_t size;
    blob = read_file("romfs:/data/sprites.bin", &size);
    if (!blob || memcmp(blob, "SPR2", 4))
        fatal("sprites.bin missing or wrong version.\nRerun tools/export_sprites.py");

    const u8 *p = blob + 4;
    nsprites = TAKE(&p, u16);
    ngroups = TAKE(&p, u16);
    u32 nframes = TAKE(&p, u32), npieces = TAKE(&p, u32), nmaps = TAKE(&p, u32);
    (void)TAKE(&p, u32);  // ncells, implied by the file size

    groups = calloc(ngroups, sizeof(Group));
    for (int i = 0; i < ngroups; i++) {
        GroupRec g = TAKE(&p, GroupRec);
        memcpy(groups[i].name, g.name, 32);
        groups[i].name[31] = 0;
        groups[i].nsprite = g.sprite_sheets;
        groups[i].ntile = g.tile_sheets;
        groups[i].sprite_sheets = calloc(g.sprite_sheets + 1, sizeof(Sheet));
        groups[i].tile_sheets = calloc(g.tile_sheets + 1, sizeof(Sheet));
        for (int s = 0; s < g.sprite_sheets; s++) all_sheets[all_sheet_count++] = &groups[i].sprite_sheets[s];
        for (int s = 0; s < g.tile_sheets; s++) all_sheets[all_sheet_count++] = &groups[i].tile_sheets[s];
    }
    sprites = (const SpriteInfo *)p; p += nsprites * sizeof(SpriteInfo);
    frames = (const FrameRec *)p;    p += nframes * sizeof(FrameRec);
    pieces = (const PieceRec *)p;    p += npieces * sizeof(PieceRec);
    maps = (const MapRec *)p;        p += nmaps * sizeof(MapRec);
    cells = (const u32 *)p;
    if (nsprites != SPR_COUNT)
        fatal("sprites.bin has %d sprites, build expects %d.\nRebuild after exporting.", nsprites, SPR_COUNT);
    masks_init();
    tiles_init();
}

void sprites_exit(void)
{
    for (int i = 0; i < all_sheet_count; i++)
        if (all_sheets[i]->tex) linearFree(all_sheets[i]->tex->data);
    free(blob);
}

const SpriteInfo *sprite_info(int spr)
{
    return (spr >= 0 && spr < nsprites) ? &sprites[spr] : NULL;
}

float sprite_frame_step(int spr)
{
    const SpriteInfo *s = sprite_info(spr);
    if (!s) return 0;
    return s->speed_type == 0 ? s->speed / GAME_FPS : s->speed;
}

// ------------------------------------------------------------------ texture cache

u32 sprites_linear_free(void) { return linearSpaceFree(); }

int sprites_loaded_sheets(void)
{
    int n = 0;
    for (int i = 0; i < all_sheet_count; i++) n += all_sheets[i]->tex != NULL;
    return n;
}

int sprite_stat_quads, sprite_stat_switches;
static int stat_quads, stat_switches;
static C3D_Tex *stat_last_tex;

void sprites_frame_begin(void)
{
    frame_counter++;
    sprite_stat_quads = stat_quads;
    sprite_stat_switches = stat_switches;
    stat_quads = stat_switches = 0;
    stat_last_tex = NULL;
    tb_used = 0;
    if (sync_frames > 0) sync_frames--;
}

static bool evict_one(void)
{
    // Oldest sheet not used this frame. Frames are drawn with C3D_FRAME_SYNCDRAW, so the GPU
    // has finished the previous frame before this one starts using textures.
    Sheet *victim = NULL;
    for (int i = 0; i < all_sheet_count; i++) {
        Sheet *s = all_sheets[i];
        if (s->tex && s->last_used < frame_counter && (!victim || s->last_used < victim->last_used))
            victim = s;
    }
    if (!victim) return false;
    linearFree(victim->tex->data);
    victim->tex = NULL;
    return true;
}

// Texture memory for a sheet, making room by evicting older sheets (game thread)
static void *tex_alloc(u32 size)
{
    void *m;
    while (!(m = linearAlloc(size)) && evict_one()) {}
    return m;
}

static void sheet_set(Sheet *s, const LoaderTex *t)
{
    C3D_Tex *x = &s->texobj;
    memset(x, 0, sizeof *x);
    x->data = t->mem;
    x->fmt = t->format;
    x->size = t->size;
    x->width = t->width;
    x->height = t->height;
    x->param = GPU_TEXTURE_MODE(GPU_TEX_2D);
    C3D_TexSetFilter(x, GPU_NEAREST, GPU_NEAREST);
    C3D_TexSetWrap(x, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    s->tex = x;
}

static C3D_Tex *sheet_get(int group, bool tiles, int index)
{
    Group *g = &groups[group];
    if (index >= (tiles ? g->ntile : g->nsprite)) return NULL;
    Sheet *s = tiles ? &g->tile_sheets[index] : &g->sprite_sheets[index];
    s->last_used = frame_counter;
    if (s->tex) return s->tex;
    if (s->missing) return NULL;

    while (linearSpaceFree() < LINEAR_RESERVE && evict_one()) {}

    char path[64];
    snprintf(path, sizeof path, "romfs:/gfx/%s_%s%d.t3x", g->name, tiles ? "t" : "", index);
    // Level start: at once. In play: read and decompressed in the background, straight into the
    // texture memory (the game thread only allocates it); the sprite shows a frame or two later.
    LoaderTex t;
    int r = sync_frames > 0 ? loader_load_tex_now(path, &t, tex_alloc) : loader_fetch_tex(path, &t, tex_alloc);
    if (r == LOADER_MISSING) s->missing = true;  // only a file that does not exist is given up on
    if (r != LOADER_READY) {
        if (r != LOADER_PENDING) dbg_log("sheet %s: %s", path, r == LOADER_MISSING ? "missing" : "failed");
        return NULL;
    }
    sheet_set(s, &t);
    return s->tex;
}

void sprites_sync_load(int frames) { sync_frames = frames; }

void sprites_flush(void)
{
    for (int i = 0; i < all_sheet_count; i++) {
        Sheet *s = all_sheets[i];
        if (!s->tex) continue;
        linearFree(s->tex->data);
        s->tex = NULL;
    }
    loader_drop_textures();
}

void sprite_preload(int spr)
{
    const SpriteInfo *s = sprite_info(spr);
    if (!s || s->mode != SPRITE_NORMAL) return;
    for (int f = 0; f < s->frame_count; f++) {
        const FrameRec *fr = &frames[s->first_frame + f];
        for (u32 i = 0; i < fr->count; i++) sheet_get(s->group, false, pieces[fr->first + i].sheet);
    }
}
bool sprites_loading(void) { return sync_frames > 0; }

void sprites_preload_group(int group)
{
    if (group < 0 || group >= ngroups) return;
    Group *g = &groups[group];
    for (int i = 0; i < g->ntile; i++) sheet_get(group, true, i);
    for (int i = 0; i < g->nsprite; i++) sheet_get(group, false, i);
}

// ------------------------------------------------------------------ drawing

// Draw a pixel rect of a texture. (x, y) is where the rect's anchor (cx, cy, in
// unscaled rect pixels) lands; negative scales mirror.
static bool exact_tint;
void sprite_set_exact_tint(bool exact) { exact_tint = exact; }

static bool draw_rect_fast(C3D_Tex *tex, int sx, int sy, int sw, int sh, float x, float y, float cx, float cy,
                           float xs, float ys, float angle, u32 colour, float alpha);

static void draw_rect(C3D_Tex *tex, int sx, int sy, int sw, int sh, float x, float y,
                      float cx, float cy, float xs, float ys, float angle, u32 colour, float alpha)
{
    if (draw_rect_fast(tex, sx, sy, sw, sh, x, y, cx, cy, xs, ys, angle, colour, alpha)) return;
    // citro2d fallback (batch buffer full)
    Tex3DS_SubTexture sub = {
        .width = sw, .height = sh,
        .left = sx / (float)tex->width,
        .right = (sx + sw) / (float)tex->width,
        .top = 1.0f - sy / (float)tex->height,
        .bottom = 1.0f - (sy + sh) / (float)tex->height,
    };
    // Mirror by swapping texture coordinates. citro2d does not mirror quads with a
    // negative size (and a one-axis mirror would flip the winding anyway).
    // A vertical flip is done as a horizontal flip plus a 180 degree turn around the anchor:
    // swapping top/bottom would mark the subtexture as rotated (citro2d treats
    // top < bottom as Tex3DS's "rotated" flag) and draw it turned by 90 degrees.
    bool flip_v = ys < 0, flip_h = (xs < 0) != flip_v;
    if (xs < 0) xs = -xs;
    if (ys < 0) ys = -ys;
    if (flip_h) {
        float t = sub.left; sub.left = sub.right; sub.right = t;
        cx = sw - cx;
    }
    if (flip_v) angle += (float)M_PI;
    // Pixel-perfect: unscaled, unrotated quads land on whole pixels (texels map 1:1 to the
    // screen; a half-pixel position samples unevenly and shimmers while moving)
    if (xs == 1 && ys == 1 && (angle == 0 || angle == (float)M_PI)) {
        x = floorf(x + 0.5f);
        y = floorf(y + 0.5f);
    }
    stat_quads++;
    if (tex != stat_last_tex) {
        stat_switches++;
        stat_last_tex = tex;
    }
    C2D_Image img = { tex, &sub };
    C2D_DrawParams params = {
        .pos = { x, y, sw * xs, sh * ys },
        .center = { cx * xs, cy * ys },
        .depth = 0,
        .angle = angle,
    };
    // image_blend (GameMaker BGR). citro2d tints by lerping toward a colour; for greys that
    // is exact: tex * v == lerp(tex, black, 1 - v). Other colours are approximated half way.
    u32 rgb = colour & 0xFFFFFF;
    if (rgb == 0xFFFFFF && alpha >= 1.0f) {
        C2D_DrawImage(img, &params, NULL);
        return;
    }
    C2D_ImageTint tint;
    if (rgb == 0xFFFFFF) {
        C2D_PlainImageTint(&tint, C2D_Color32f(1, 1, 1, alpha), 0.0f);
    } else {
        float r = (rgb & 0xFF) / 255.0f, g = ((rgb >> 8) & 0xFF) / 255.0f, b = ((rgb >> 16) & 0xFF) / 255.0f;
        if (r == g && g == b) C2D_PlainImageTint(&tint, C2D_Color32f(0, 0, 0, alpha), 1.0f - r);
        else C2D_PlainImageTint(&tint, C2D_Color32f(r, g, b, alpha), exact_tint ? 1.0f : 0.5f);
    }
    C2D_DrawImage(img, &params, &tint);
}

static int frame_index(const SpriteInfo *s, float frame)
{
    int n = s->frame_count;
    if (n <= 0) return -1;
    int f = (int)floorf(frame);
    f %= n;
    if (f < 0) f += n;
    return s->first_frame + f;
}


// ------------------------------------------------------------------ quad batcher
// Every sprite and tile is drawn here, with a small shader of our own instead of citro2d's
// quad path: vertices are written straight into a buffer, consecutive quads on one texture
// become one draw call, and the colour multiplies the texture exactly (GameMaker's
// image_blend; citro2d can only lerp towards a colour). The batch stays open across draws and
// is closed (citro2d restored) by sprites_batch_end, which common.h calls before any citro2d
// drawing, so the drawing order is kept.

typedef struct { float x, y, u, v; u32 color; } QuadVtx;
#define TB_MAX 6144                       // quads per frame
static QuadVtx *tb_vtx;
static u16 *tb_idx;
static DVLB_s *tb_dvlb;
static shaderProgram_s tb_prog;
static int tb_loc_proj;
static C3D_AttrInfo tb_attr;
static C3D_BufInfo tb_buf;
static float scene_w = TOP_W, scene_h = TOP_H;
static bool mb_on;          // our shader is bound
static C3D_Tex *mb_tex;     // texture of the pending quads
static int mb_first;        // first pending quad

float sprites_scene_width(void) { return scene_w; }

void sprites_scene(float w, float h)
{
    sprites_batch_end();
    scene_w = w;
    scene_h = h;
}

static void tiles_init(void)
{
    tb_vtx = linearAlloc(TB_MAX * 4 * sizeof(QuadVtx));
    tb_idx = linearAlloc(TB_MAX * 6 * sizeof(u16));
    if (!tb_vtx || !tb_idx) {
        if (tb_vtx) linearFree(tb_vtx);
        if (tb_idx) linearFree(tb_idx);
        tb_vtx = NULL;
        tb_idx = NULL;
        return;
    }
    for (int t = 0; t < TB_MAX; t++) {
        u16 *i = tb_idx + t * 6, b = (u16)(t * 4);
        i[0] = b; i[1] = b + 1; i[2] = b + 2;
        i[3] = b + 2; i[4] = b + 1; i[5] = b + 3;
    }
    GSPGPU_FlushDataCache(tb_idx, TB_MAX * 6 * sizeof(u16));
    tb_dvlb = DVLB_ParseFile((u32 *)tile_shbin, tile_shbin_size);
    shaderProgramInit(&tb_prog);
    shaderProgramSetVsh(&tb_prog, &tb_dvlb->DVLE[0]);
    tb_loc_proj = shaderInstanceGetUniformLocation(tb_prog.vertexShader, "projection");
    AttrInfo_Init(&tb_attr);
    AttrInfo_AddLoader(&tb_attr, 0, GPU_FLOAT, 2);          // v0 position
    AttrInfo_AddLoader(&tb_attr, 1, GPU_FLOAT, 2);          // v1 texcoord
    AttrInfo_AddLoader(&tb_attr, 2, GPU_UNSIGNED_BYTE, 4);  // v2 colour
    BufInfo_Init(&tb_buf);
    BufInfo_Add(&tb_buf, tb_vtx, sizeof(QuadVtx), 3, 0x210);
}

static void mb_flush(void)
{
    int count = tb_used - mb_first;
    if (mb_on && mb_tex && count > 0) {
        C3D_TexBind(0, mb_tex);
        C3D_DrawElements(GPU_TRIANGLES, count * 6, C3D_UNSIGNED_SHORT, tb_idx + mb_first * 6);
        stat_switches++;
    }
    mb_first = tb_used;
}

void sprites_batch_end(void)
{
    if (!mb_on) return;
    mb_flush();
    mb_on = false;
    C2D_Prepare();
}

// Room for n quads on tex, our shader bound. False: draw with citro2d instead.
static bool mb_begin(C3D_Tex *tex, int n)
{
    if (!tb_vtx || tb_used + n > TB_MAX) {
        sprites_batch_end();
        return false;
    }
    if (!mb_on) {
        (C2D_Flush)();  // citro2d's queued quads first
        C3D_BindProgram(&tb_prog);
        C3D_SetAttrInfo(&tb_attr);
        C3D_SetBufInfo(&tb_buf);
        C3D_Mtx proj, view, mvp;
        Mtx_OrthoTilt(&proj, 0.0f, scene_w, scene_h, 0.0f, 1.0f, -1.0f, true);
        C2D_ViewSave(&view);
        Mtx_Multiply(&mvp, &proj, &view);
        C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, tb_loc_proj, &mvp);
        C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
        C3D_CullFace(GPU_CULL_NONE);
        C3D_TexEnv *env = C3D_GetTexEnv(0);
        C3D_TexEnvInit(env);
        C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, 0);
        C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);
        for (int st = 1; st < 6; st++) C3D_TexEnvInit(C3D_GetTexEnv(st));
        mb_on = true;
        mb_first = tb_used;
        mb_tex = tex;
    } else if (tex != mb_tex) {
        mb_flush();
        mb_tex = tex;
    }
    return true;
}

// GameMaker colour (BGR) + alpha -> vertex colour (the GPU reads the bytes as R, G, B, A)
static inline u32 mb_color(u32 colour, float alpha)
{
    u32 a = (u32)(fminf(fmaxf(alpha, 0), 1) * 255.0f + 0.5f);
    return (a << 24) | (colour & 0xFFFFFF);
}

// One quad: the 4 corners (top-left, top-right, bottom-left, bottom-right) and texcoords
static inline void mb_quad(const float *xs, const float *ys, float u0, float v0, float u1, float v1, u32 color)
{
    QuadVtx *v = tb_vtx + tb_used * 4;
    v[0] = (QuadVtx){ xs[0], ys[0], u0, v0, color };
    v[1] = (QuadVtx){ xs[1], ys[1], u1, v0, color };
    v[2] = (QuadVtx){ xs[2], ys[2], u0, v1, color };
    v[3] = (QuadVtx){ xs[3], ys[3], u1, v1, color };
    tb_used++;
    stat_quads++;
}

typedef struct { float x, y; u32 cell; } TileRef;
static TileRef tb_list[TB_MAX];

// Draws the visible tiles of one tilemap frame. False when it cannot (full buffer): the
// caller falls back to citro2d.
static bool draw_tilemap_fast(const SpriteInfo *s, const MapRec *m, float left, float top, float tw, float th,
                              int c0, int c1, int r0, int r1, u32 colour, float alpha)
{
    if (!tb_vtx) return false;
    int n = 0;
    for (int r = r0; r < r1; r++) {
        const u32 *row = cells + m->first_cell + (u32)r * m->cols;
        for (int c = c0; c < c1; c++) {
            u32 cell = row[c];
            if (cell == CELL_EMPTY) continue;
            if (n >= TB_MAX) return false;
            tb_list[n++] = (TileRef){ left + c * tw, top + r * th, cell };
        }
    }
    if (!n) return true;
    if (tb_used + n > TB_MAX) return false;
    u32 color = mb_color(colour, alpha);

    // grouped by sheet (tiles of a layer never overlap, so their order is free)
    int done = 0;
    while (done < n) {
        u32 sheet = CELL_INDEX(tb_list[done].cell) / TILES_PER_SHEET;
        C3D_Tex *tex = sheet_get(s->group, true, (int)sheet);
        int k = done;
        if (tex && !mb_begin(tex, n - done)) return false;
        float iw = tex ? 1.0f / tex->width : 0, ih = tex ? 1.0f / tex->height : 0;
        for (int i = done; i < n; i++) {
            u32 cell = tb_list[i].cell, idx = CELL_INDEX(cell);
            if (idx / TILES_PER_SHEET != sheet) continue;
            TileRef t = tb_list[i];
            tb_list[i] = tb_list[k];  // move handled tiles to the front
            tb_list[k++] = t;
            if (!tex) continue;
            u32 local = idx % TILES_PER_SHEET;
            float sx = (local % TILES_PER_ROW) * TILE, sy = (local / TILES_PER_ROW) * TILE;
            float u0 = sx * iw, u1 = (sx + TILE) * iw;
            float v0 = 1.0f - sy * ih, v1 = 1.0f - (sy + TILE) * ih;
            if (cell & CELL_FLIP_H) { float q = u0; u0 = u1; u1 = q; }
            if (cell & CELL_FLIP_V) { float q = v0; v0 = v1; v1 = q; }
            float x0 = t.x, y0 = t.y, x1 = t.x + tw, y1 = t.y + th;
            const float qx[4] = { x0, x1, x0, x1 }, qy[4] = { y0, y0, y1, y1 };
            mb_quad(qx, qy, u0, v0, u1, v1, color);
        }
        done = k;
    }
    return true;
}

// A sprite rect (see draw_rect) through the batcher. False: use citro2d.
static bool smooth_next;          // sprite_draw_smooth: linear filtering for this draw
static C3D_Tex *smooth_tex[8];
static int smooth_count;

static bool draw_rect_fast(C3D_Tex *tex, int sx, int sy, int sw, int sh, float x, float y, float cx, float cy,
                           float xs, float ys, float angle, u32 colour, float alpha)
{
    if (smooth_next && smooth_count < 8) {
        C3D_TexSetFilter(tex, GPU_LINEAR, GPU_LINEAR);
        smooth_tex[smooth_count++] = tex;
    }
    if (!mb_begin(tex, 1)) return false;
    float iw = 1.0f / tex->width, ih = 1.0f / tex->height;
    float u0 = sx * iw, u1 = (sx + sw) * iw;
    float v0 = 1.0f - sy * ih, v1 = 1.0f - (sy + sh) * ih;
    // mirroring: swap texcoords and mirror the anchor
    if (xs < 0) { float q = u0; u0 = u1; u1 = q; xs = -xs; cx = sw - cx; }
    if (ys < 0) { float q = v0; v0 = v1; v1 = q; ys = -ys; cy = sh - cy; }
    float w = sw * xs, h = sh * ys, ax = cx * xs, ay = cy * ys;
    float lx[4] = { -ax, w - ax, -ax, w - ax }, ly[4] = { -ay, -ay, h - ay, h - ay };
    float qx[4], qy[4];
    if (angle == 0) {
        // pixel-perfect: unscaled quads start on whole pixels
        if (xs == 1 && ys == 1) {
            x = floorf(x - ax + 0.5f) + ax;
            y = floorf(y - ay + 0.5f) + ay;
        }
        for (int i = 0; i < 4; i++) {
            qx[i] = x + lx[i];
            qy[i] = y + ly[i];
        }
    } else {
        // clockwise radians around the anchor, like citro2d
        float c = cosf(angle), s = sinf(angle);
        for (int i = 0; i < 4; i++) {
            qx[i] = x + lx[i] * c - ly[i] * s;
            qy[i] = y + lx[i] * s + ly[i] * c;
        }
    }
    mb_quad(qx, qy, u0, v0, u1, v1, mb_color(colour, alpha));
    return true;
}

// sprite_draw_part: only the frame's pixel columns [clip_l, clip_r) (tile aligned)
static int clip_l = -1000000, clip_r = 1000000;

static void draw_tilemap(const SpriteInfo *s, const MapRec *m, float x, float y, float xs, float ys, u32 colour, float alpha)
{
    // Tilemaps are level art: drawn unrotated. Cull to the top screen.
    float left = x - s->xorigin * xs + m->x0 * xs;
    float top = y - s->yorigin * ys + m->y0 * ys;
    float tw = TILE * xs, th = TILE * ys;
    if (xs == 1 && ys == 1) {  // whole pixels (parallax layers move by fractions)
        left = floorf(left + 0.5f);
        top = floorf(top + 0.5f);
    }
    if (tw <= 0 || th <= 0) return;
    int c0 = (int)floorf((0 - left) / tw), c1 = (int)ceilf((TOP_W - left) / tw);
    int r0 = (int)floorf((0 - top) / th), r1 = (int)ceilf((TOP_H - top) / th);
    if (c0 < 0) c0 = 0;
    if (r0 < 0) r0 = 0;
    if (c1 > m->cols) c1 = m->cols;
    if (r1 > m->rows) r1 = m->rows;
    if (clip_l > -1000000 && c0 < (clip_l - m->x0) / TILE) c0 = (clip_l - m->x0) / TILE;
    if (clip_r < 1000000 && c1 > (clip_r - m->x0) / TILE) c1 = (clip_r - m->x0) / TILE;
    if (c0 >= c1 || r0 >= r1) return;
    if (draw_tilemap_fast(s, m, left, top, tw, th, c0, c1, r0, r1, colour, alpha)) return;

    for (int r = r0; r < r1; r++) {
        const u32 *row = cells + m->first_cell + (u32)r * m->cols;
        for (int c = c0; c < c1; c++) {
            u32 cell = row[c];
            if (cell == CELL_EMPTY) continue;
            u32 idx = CELL_INDEX(cell);
            C3D_Tex *tex = sheet_get(s->group, true, idx / TILES_PER_SHEET);
            if (!tex) continue;
            u32 local = idx % TILES_PER_SHEET;
            float fx = (cell & CELL_FLIP_H) ? -1.0f : 1.0f;
            float fy = (cell & CELL_FLIP_V) ? -1.0f : 1.0f;
            // Flipped tiles are anchored at their centre so the flip stays in the cell.
            draw_rect(tex, (local % TILES_PER_ROW) * TILE, (local / TILES_PER_ROW) * TILE, TILE, TILE,
                      left + (c + 0.5f) * tw, top + (r + 0.5f) * th, TILE / 2.0f, TILE / 2.0f,
                      xs * fx, ys * fy, 0, colour, alpha);
        }
    }
}

void sprite_draw(int spr, float frame, float x, float y, float xscale, float yscale,
                 float angle_deg, u32 colour, float alpha)
{
    const SpriteInfo *s = sprite_info(spr);
    if (!s || alpha <= 0) return;
    int fi = frame_index(s, frame);
    if (fi < 0) return;
    const FrameRec *f = &frames[fi];

    if (s->mode == SPRITE_TILEMAP) {
        draw_tilemap(s, &maps[f->first], x, y, xscale, yscale, colour, alpha);
        return;
    }
    if (s->mode != SPRITE_NORMAL) return;

    // GameMaker angles are counter-clockwise degrees; citro2d is clockwise radians.
    float angle = -angle_deg * (float)M_PI / 180.0f;
    for (u32 i = 0; i < f->count; i++) {
        const PieceRec *pc = &pieces[f->first + i];
        C3D_Tex *tex = sheet_get(s->group, false, pc->sheet);
        if (!tex) continue;
        draw_rect(tex, pc->x, pc->y, pc->w, pc->h, x, y,
                  s->xorigin - pc->dx, s->yorigin - pc->dy, xscale, yscale, angle, colour, alpha);
    }
}

bool sprite_tiled_covers(int spr, float frame, float x, float y, bool htile, bool vtile, float view_w, float view_h)
{
    const SpriteInfo *s = sprite_info(spr);
    if (!s || s->mode != SPRITE_TILEMAP || s->width == 0 || s->height == 0) return false;
    int fi = frame_index(s, frame);
    if (fi < 0) return false;
    const MapRec *m = &maps[frames[fi].first];
    float w = s->width, h = s->height, cw = m->cols * TILE, ch = m->rows * TILE;
    // every repeat must be solid edge to edge, a single copy must span the view
    if (htile ? (m->x0 != 0 || cw < w) : (x + m->x0 > 0 || x + m->x0 + cw < view_w)) return false;
    if (vtile ? (m->y0 != 0 || ch < h) : (y + m->y0 > 0 || y + m->y0 + ch < view_h)) return false;
    float x0 = x, y0 = y;
    if (htile) { x0 = fmodf(x, w); if (x0 > 0) x0 -= w; }
    if (vtile) { y0 = fmodf(y, h); if (y0 > 0) y0 -= h; }
    for (float yy = y0; yy < (vtile ? view_h : y0 + 1); yy += h)
        for (float xx = x0; xx < (htile ? view_w : x0 + 1); xx += w) {
            float left = xx + m->x0, top = yy + m->y0;
            int c0 = (int)floorf(-left / TILE), c1 = (int)ceilf((view_w - left) / TILE);
            int r0 = (int)floorf(-top / TILE), r1 = (int)ceilf((view_h - top) / TILE);
            if (c0 < 0) c0 = 0;
            if (r0 < 0) r0 = 0;
            if (c1 > m->cols) c1 = m->cols;
            if (r1 > m->rows) r1 = m->rows;
            for (int r = r0; r < r1; r++) {
                const u32 *row = cells + m->first_cell + (u32)r * m->cols;
                for (int c = c0; c < c1; c++)
                    if (row[c] == CELL_EMPTY || !(row[c] & CELL_OPAQUE)) return false;
            }
        }
    return true;
}

void sprite_draw_tex(C3D_Tex *tex, int sx, int sy, int sw, int sh, float x, float y, float scale, u32 rgba)
{
    // rgba: a C2D_Color32 value (R in the low byte like GameMaker's BGR colours)
    draw_rect(tex, sx, sy, sw, sh, x, y, 0, 0, scale, scale, 0, rgba & 0xFFFFFF, (rgba >> 24) / 255.0f);
}

void sprite_draw_smooth(int spr, float frame, float x, float y, float xscale, float yscale, u32 colour, float alpha)
{
    // tiled sprites would show seams between tiles with linear filtering
    const SpriteInfo *s = sprite_info(spr);
    if (!s || s->mode != SPRITE_NORMAL) {
        sprite_draw(spr, frame, x, y, xscale, yscale, 0, colour, alpha);
        return;
    }
    // the filter is read when the draw is recorded: close the batch around it
    sprites_batch_end();
    smooth_next = true;
    smooth_count = 0;
    sprite_draw(spr, frame, x, y, xscale, yscale, 0, colour, alpha);
    sprites_batch_end();
    smooth_next = false;
    for (int i = 0; i < smooth_count; i++) C3D_TexSetFilter(smooth_tex[i], GPU_NEAREST, GPU_NEAREST);
}

void sprite_draw_sub(int spr, float frame, float x, float y, int sx0, int sy0, int sx1, int sy1, u32 colour,
                     float alpha)
{
    const SpriteInfo *s = sprite_info(spr);
    if (!s || s->mode != SPRITE_NORMAL || alpha <= 0) return;
    int fi = frame_index(s, frame);
    if (fi < 0) return;
    const FrameRec *f = &frames[fi];
    for (u32 i = 0; i < f->count; i++) {
        const PieceRec *pc = &pieces[f->first + i];
        int l = pc->dx > sx0 ? pc->dx : sx0, t = pc->dy > sy0 ? pc->dy : sy0;
        int r = pc->dx + pc->w < sx1 ? pc->dx + pc->w : sx1, b = pc->dy + pc->h < sy1 ? pc->dy + pc->h : sy1;
        if (r <= l || b <= t) continue;
        C3D_Tex *tex = sheet_get(s->group, false, pc->sheet);
        if (!tex) continue;
        draw_rect(tex, pc->x + (l - pc->dx), pc->y + (t - pc->dy), r - l, b - t, x - s->xorigin + l, y - s->yorigin + t,
                  0, 0, 1, 1, 0, colour, alpha);
    }
}

void sprite_draw_part(int spr, float frame, float x, float y, int src_l, int src_r)
{
    clip_l = src_l;
    clip_r = src_r;
    sprite_draw(spr, frame, x, y, 1, 1, 0, 0xFFFFFFFF, 1);
    clip_l = -1000000;
    clip_r = 1000000;
}

void sprite_draw_tiled(int spr, float frame, float x, float y, bool htile, bool vtile,
                       float view_w, float view_h, u32 colour)
{
    const SpriteInfo *s = sprite_info(spr);
    if (!s || s->width == 0 || s->height == 0) return;
    float w = s->width, h = s->height;
    // Start one repeat at or left of/above the screen edge.
    float x0 = x, y0 = y;
    if (htile) { x0 = fmodf(x, w); if (x0 > 0) x0 -= w; }
    if (vtile) { y0 = fmodf(y, h); if (y0 > 0) y0 -= h; }
    for (float yy = y0; yy < (vtile ? view_h : y0 + 1); yy += h)
        for (float xx = x0; xx < (htile ? view_w : x0 + 1); xx += w)
            if (xx + w > 0 && yy + h > 0 && xx < view_w && yy < view_h)
                sprite_draw(spr, frame, xx + s->xorigin, yy + s->yorigin, 1, 1, 0, colour, (colour >> 24) / 255.0f);
}
