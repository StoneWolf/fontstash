//
// Copyright (c) 2009-2013 Mikko Mononen memon@inside.org
//
// This software is provided 'as-is', without any express or implied
// warranty.  In no event will the authors be held liable for any damages
// arising from the use of this software.
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
// 1. The origin of this software must not be misrepresented; you must not
//    claim that you wrote the original software. If you use this software
//    in a product, an acknowledgment in the product documentation would be
//    appreciated but is not required.
// 2. Altered source versions must be plainly marked as such, and must not be
//    misrepresented as being the original software.
// 3. This notice may not be removed or altered from any source distribution.
//

#ifndef FONS_H
#define FONS_H

#define FONS_INVALID -1

enum FONSflags {
	FONS_ZERO_TOPLEFT = 1,
	FONS_ZERO_BOTTOMLEFT = 2,
};

enum FONSaligg {
	// Horizontal align
	FONS_ALIGN_LEFT 	= 1<<0,	// Default
	FONS_ALIGN_CENTER 	= 1<<1,
	FONS_ALIGN_RIGHT 	= 1<<2,
	// Vertical align
	FONS_ALIGN_TOP 		= 1<<3,
	FONS_ALIGN_MIDDLE	= 1<<4,
	FONS_ALIGN_BOTTOM	= 1<<5,
	FONS_ALIGN_BASELINE	= 1<<6, // Default
};

struct FONSparams {
	int width, height;
	unsigned char flags;
	void* userPtr;
	int (*renderCreate)(void* uptr, int width, int height);
	void (*renderUpdate)(void* uptr, int* rect, const unsigned char* data);
	void (*renderDraw)(void* uptr, const float* verts, const float* tcoords, const unsigned int* colors, int nverts);
	void (*renderDelete)(void* uptr);
};

// Contructor and destructor.
struct FONScontext* fonsCreate(struct FONSparams* params);
void fonsDelete(struct FONScontext* s);

// Add fonts
int fonsAddFont(struct FONScontext* s, const char* path);
int fonsAddFontMem(struct FONScontext* s, unsigned char* data, int ndata, int freeData);

// State handling
void fonsPushState(struct FONScontext* s);
void fonsPopState(struct FONScontext* s);
void fonsClearState(struct FONScontext* s);

// State setting
void fonsSetSize(struct FONScontext* s, float size);
void fonsSetColor(struct FONScontext* s, unsigned int color);
void fonsSetSpacing(struct FONScontext* s, float spacing);
void fonsSetBlur(struct FONScontext* s, float blur);
void fonsSetAlign(struct FONScontext* s, int align);
void fonsSetFont(struct FONScontext* s, int font);

// Draw text
void fonsDrawText(struct FONScontext* s, float x, float y, const char* string, float* dx);

// Measure text
void fonsTextBounds(struct FONScontext* s, const char* string, float* width, float* bounds);
void fonsVertMetrics(struct FONScontext* s, float* ascender, float* descender, float* lineh);

// Draws the stash texture for debugging
void fonsDrawDebug(struct FONScontext* s, float x, float y);

#endif // FONS_H


#ifdef FONTSTASH_IMPLEMENTATION

#define STB_TRUETYPE_IMPLEMENTATION
static void* fons__tmpalloc(size_t size, void* up);
static void fons__tmpfree(void* ptr, void* up);
#define STBTT_malloc(x,u)    fons__tmpalloc(x,u)
#define STBTT_free(x,u)      fons__tmpfree(x,u)
#include "stb_truetype.h"

#ifndef FONS_SCRATCH_BUF_SIZE
#	define FONS_SCRATCH_BUF_SIZE 16000
#endif
#ifndef FONS_HASH_LUT_SIZE
#	define FONS_HASH_LUT_SIZE 256
#endif
#ifndef FONS_INIT_FONTS
#	define FONS_INIT_FONTS 4
#endif
#ifndef FONS_INIT_ROWS
#	define FONS_INIT_ROWS 64
#endif
#ifndef FONS_INIT_GLYPHS
#	define FONS_INIT_GLYPHS 256
#endif
#ifndef FONS_VERTEX_COUNT
#	define FONS_VERTEX_COUNT 1024
#endif
#ifndef FONS_MAX_STATES
#	define FONS_MAX_STATES 20
#endif

static unsigned int fons__hashint(unsigned int a)
{
	a += ~(a<<15);
	a ^=  (a>>10);
	a +=  (a<<3);
	a ^=  (a>>6);
	a += ~(a<<11);
	a ^=  (a>>16);
	return a;
}

static int fons__mini(int a, int b)
{
	return a < b ? a : b;
}

static int fons__maxi(int a, int b)
{
	return a > b ? a : b;
}

struct FONSquad
{
	float x0,y0,s0,t0;
	float x1,y1,s1,t1;
};

struct FONSrow
{
	short x,y,h;
};

struct FONSglyph
{
	unsigned int codepoint;
	int index;
	int next;
	short size, blur;
	short x0,y0,x1,y1;
	short xadv,xoff,yoff;
};

struct FONSfont
{
	stbtt_fontinfo font;
	unsigned char* data;
	int dataSize;
	unsigned char freeData;
	float ascender;
	float descender;
	float lineh;
	struct FONSglyph* glyphs;
	int cglyphs;
	int nglyphs;
	int lut[FONS_HASH_LUT_SIZE];
};

struct FONSstate
{
	int font;
	int align;
	float size;
	unsigned int color;
	float blur;
	float spacing;
};

struct FONScontext
{
	struct FONSparams params;
	float itw,ith;
	unsigned char* texData;
	int dirtyRect[4];
	struct FONSrow* rows;
	int crows;
	int nrows;
	struct FONSfont** fonts;
	int cfonts;
	int nfonts;
	float verts[FONS_VERTEX_COUNT*2];
	float tcoords[FONS_VERTEX_COUNT*2];
	unsigned int colors[FONS_VERTEX_COUNT];
	int nverts;
	unsigned char scratch[FONS_SCRATCH_BUF_SIZE];
	int nscratch;
	struct FONSstate states[FONS_MAX_STATES];
	int nstates;
};

static void* fons__tmpalloc(size_t size, void* up)
{
	struct FONScontext* stash = (struct FONScontext*)up;
	if (stash->nscratch+(int)size > FONS_SCRATCH_BUF_SIZE)
		return NULL;
	unsigned char* ptr = stash->scratch + stash->nscratch;
	stash->nscratch += (int)size;
	return ptr;
}

static void fons__tmpfree(void* ptr, void* up)
{
	// empty
}

// Copyright (c) 2008-2010 Bjoern Hoehrmann <bjoern@hoehrmann.de>
// See http://bjoern.hoehrmann.de/utf-8/decoder/dfa/ for details.

#define FONS_UTF8_ACCEPT 0
#define FONS_UTF8_REJECT 12

static unsigned int fons__decutf8(unsigned int* state, unsigned int* codep, unsigned int byte)
{
	static const unsigned char utf8d[] = {
		// The first part of the table maps bytes to character classes that
		// to reduce the size of the transition table and create bitmasks.
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
		1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,  9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
		7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,  7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
		8,8,2,2,2,2,2,2,2,2,2,2,2,2,2,2,  2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
		10,3,3,3,3,3,3,3,3,3,3,3,3,4,3,3, 11,6,6,6,5,8,8,8,8,8,8,8,8,8,8,8,

		// The second part is a transition table that maps a combination
		// of a state of the automaton and a character class to a state.
		0,12,24,36,60,96,84,12,12,12,48,72, 12,12,12,12,12,12,12,12,12,12,12,12,
		12, 0,12,12,12,12,12, 0,12, 0,12,12, 12,24,12,12,12,12,12,24,12,24,12,12,
		12,12,12,12,12,12,12,24,12,12,12,12, 12,24,12,12,12,12,12,12,12,24,12,12,
		12,12,12,12,12,12,12,36,12,36,12,12, 12,36,12,12,12,12,12,36,12,36,12,12,
		12,36,12,12,12,12,12,12,12,12,12,12,
    };

	unsigned int type = utf8d[byte];

    *codep = (*state != FONS_UTF8_ACCEPT) ?
		(byte & 0x3fu) | (*codep << 6) :
		(0xff >> type) & (byte);

	*state = utf8d[256 + *state + type];
	return *state;
}

struct FONScontext* fonsCreate(struct FONSparams* params)
{
	struct FONScontext* stash = NULL;

	// Allocate memory for the font stash.
	stash = (struct FONScontext*)malloc(sizeof(struct FONScontext));
	if (stash == NULL) goto error;
	memset(stash, 0, sizeof(struct FONScontext));

	stash->params = *params;

	if (stash->params.renderCreate(stash->params.userPtr, stash->params.width, stash->params.height) == 0) goto error;

	// Allocate space for rows
	stash->rows = (struct FONSrow*)malloc(sizeof(struct FONSrow) * FONS_INIT_ROWS);
	if (stash->rows == NULL) goto error;
	memset(stash->rows, 0, sizeof(struct FONSrow) * FONS_INIT_ROWS);
	stash->crows = FONS_INIT_ROWS;
	stash->nrows = 0;

	// Allocate space for fonts.
	stash->fonts = (struct FONSfont**)malloc(sizeof(struct FONSfont*) * FONS_INIT_FONTS);
	if (stash->fonts == NULL) goto error;
	memset(stash->fonts, 0, sizeof(struct FONSfont*) * FONS_INIT_FONTS);
	stash->cfonts = FONS_INIT_FONTS;
	stash->nfonts = 0;

	// Create texture for the cache.
	stash->itw = 1.0f/stash->params.width;
	stash->ith = 1.0f/stash->params.height;
	stash->texData = (unsigned char*)malloc(stash->params.width * stash->params.height);
	if (stash->texData == NULL) goto error;
	memset(stash->texData, 0, stash->params.width * stash->params.height);

	stash->dirtyRect[0] = stash->params.width;
	stash->dirtyRect[1] = stash->params.height;
	stash->dirtyRect[2] = 0;
	stash->dirtyRect[3] = 0;

	fonsPushState(stash);
	fonsClearState(stash);

	return stash;

error:
	fonsDelete(stash);
	return NULL;
}

static struct FONSstate* fons__getState(struct FONScontext* stash)
{
	return &stash->states[stash->nstates-1];
}

void fonsSetSize(struct FONScontext* stash, float size)
{
	fons__getState(stash)->size = size;
}

void fonsSetColor(struct FONScontext* stash, unsigned int color)
{
	fons__getState(stash)->color = color;
}

void fonsSetSpacing(struct FONScontext* stash, float spacing)
{
	fons__getState(stash)->spacing = spacing;
}

void fonsSetBlur(struct FONScontext* stash, float blur)
{
	fons__getState(stash)->blur = blur;
}

void fonsSetAlign(struct FONScontext* stash, int align)
{
	fons__getState(stash)->align = align;
}

void fonsSetFont(struct FONScontext* stash, int font)
{
	fons__getState(stash)->font = font;
}

void fonsPushState(struct FONScontext* stash)
{
	if (stash->nstates >= FONS_MAX_STATES)
		return;
	if (stash->nstates > 0)
		memcpy(&stash->states[stash->nstates], &stash->states[stash->nstates-1], sizeof(struct FONSstate));
	stash->nstates++;
}

void fonsPopState(struct FONScontext* stash)
{
	if (stash->nstates <= 1)
		return;
	stash->nstates--;
}

void fonsClearState(struct FONScontext* stash)
{
	struct FONSstate* state = fons__getState(stash);
	state->size = 12.0f;
	state->color = 0xffffffff;
	state->font = 0;
	state->blur = 0;
	state->spacing = 0;
	state->align = FONS_ALIGN_LEFT | FONS_ALIGN_BASELINE;
}



int fonsAddFont(struct FONScontext* stash, const char* path)
{
	FILE* fp = 0;
	int dataSize = 0;
	unsigned char* data = NULL;

	// Read in the font data.
	fp = fopen(path, "rb");
	if (!fp) goto error;
	fseek(fp,0,SEEK_END);
	dataSize = (int)ftell(fp);
	fseek(fp,0,SEEK_SET);
	data = (unsigned char*)malloc(dataSize);
	if (data == NULL) goto error;
	fread(data, 1, dataSize, fp);
	fclose(fp);
	fp = 0;

	return fonsAddFontMem(stash, data, dataSize, 1);

error:
	if (data) free(data);
	if (fp) fclose(fp);
	return 0;
}

static void fons__freeFont(struct FONSfont* font)
{
	if (font == NULL) return;
	if (font->glyphs) free(font->glyphs);
	if (font->freeData && font->data) free(font->data); 
	free(font);
}

static int fons__allocFont(struct FONScontext* stash)
{
	struct FONSfont** fonts = NULL;
	struct FONSfont* font = NULL;
	if (stash->nfonts+1 > stash->cfonts) {
		stash->cfonts *= 2;
		fonts = (struct FONSfont**)malloc(sizeof(struct FONSfont*) * stash->cfonts);
		if (fonts == NULL) goto error;
		memset(fonts, 0, sizeof(struct FONSfont*) * stash->cfonts);
		if (stash->nfonts > 0)
			memcpy(fonts, stash->fonts, sizeof(struct FONSfont*));

		free(stash->fonts);
		stash->fonts = fonts;
	}
	font = (struct FONSfont*)malloc(sizeof(struct FONSfont));
	if (font == NULL) goto error;
	memset(font, 0, sizeof(struct FONSfont));

	font->glyphs = (struct FONSglyph*)malloc(sizeof(struct FONSglyph) * FONS_INIT_GLYPHS);
	if (font->glyphs == NULL) goto error;
	font->cglyphs = FONS_INIT_GLYPHS;
	font->nglyphs = 0;
	
	stash->fonts[stash->nfonts++] = font;
	return stash->nfonts-1;

error:
	fons__freeFont(font);

	return FONS_INVALID;
}

int fonsAddFontMem(struct FONScontext* stash, unsigned char* data, int dataSize, int freeData)
{
	int i, ascent, descent, fh, lineGap;
	struct FONSfont* font;

	int idx = fons__allocFont(stash);
	if (idx == FONS_INVALID)
		return FONS_INVALID;

	font = stash->fonts[idx];

	// Init hash lookup.
	for (i = 0; i < FONS_HASH_LUT_SIZE; ++i)
		font->lut[i] = -1;

	// Read in the font data.
	font->dataSize = dataSize;
	font->data = data;
	font->freeData = freeData;

	// Init stb_truetype
	stash->nscratch = 0;
	font->font.userdata = stash;
	if (!stbtt_InitFont(&font->font, font->data, 0)) goto error;

	// Store normalized line height. The real line height is got
	// by multiplying the lineh by font size.
	stbtt_GetFontVMetrics(&font->font, &ascent, &descent, &lineGap);
	fh = ascent - descent;
	font->ascender = (float)ascent / (float)fh;
	font->descender = (float)descent / (float)fh;
	font->lineh = (float)(fh + lineGap) / (float)fh;

	return idx;

error:
	fons__freeFont(font);
	stash->nfonts--;
	return FONS_INVALID;
}

static struct FONSrow* fons__allocRow(struct FONScontext* stash)
{
	struct FONSrow* rows = NULL;
	if (stash->nrows+1 > stash->crows) {
		stash->crows *= 2;
		rows = (struct FONSrow*)malloc(sizeof(struct FONSrow) * stash->crows);
		if (rows == NULL) return NULL;
		memset(rows, 0, sizeof(struct FONSrow) * stash->crows);
		if (stash->nrows > 0)
			memcpy(rows, stash->rows, sizeof(struct FONSrow) * stash->nrows);
		free(stash->rows);
		stash->rows = rows;
	}
	stash->nrows++;
	return &stash->rows[stash->nrows-1];
}

static struct FONSglyph* fons__allocGlyph(struct FONSfont* font)
{
	struct FONSglyph* glyphs = NULL;
	if (font->nglyphs+1 > font->cglyphs) {
		font->cglyphs *= 2;
		glyphs = (struct FONSglyph*)malloc(sizeof(struct FONSglyph) * font->cglyphs);
		if (glyphs == NULL) return NULL;
		memset(glyphs, 0, sizeof(struct FONSglyph) * font->cglyphs);
		if (font->nglyphs > 0)
			memcpy(glyphs, font->glyphs, sizeof(struct FONSglyph) * font->nglyphs);
		free(font->glyphs);
		font->glyphs = glyphs;
	}
	font->nglyphs++;
	return &font->glyphs[font->nglyphs-1];
}


// Based on Exponential blur, Jani Huhtanen, 2006

#define APREC 16
#define ZPREC 7

static void fons__blurcols(unsigned char* dst, int w, int h, int dstStride, int alpha)
{
	int x, y;
	for (y = 0; y < h; y++) {
		int z = 0; // force zero border
		for (x = 1; x < w; x++) {
			z += (alpha * (((int)(dst[x]) << ZPREC) - z)) >> APREC;
			dst[x] = (unsigned char)(z >> ZPREC);
		}
		dst[w-1] = 0; // force zero border
		z = 0;
		for (x = w-2; x >= 0; x--) {
			z += (alpha * (((int)(dst[x]) << ZPREC) - z)) >> APREC;
			dst[x] = (unsigned char)(z >> ZPREC);
		}
		dst[0] = 0; // force zero border
		dst += dstStride;
	}
}

static void fons__blurrows(unsigned char* dst, int w, int h, int dstStride, int alpha)
{
	int x, y;
	for (x = 0; x < w; x++) {
		int z = 0; // force zero border
		for (y = dstStride; y < h*dstStride; y += dstStride) {
			z += (alpha * (((int)(dst[y]) << ZPREC) - z)) >> APREC;
			dst[y] = (unsigned char)(z >> ZPREC);
		}
		dst[(h-1)*dstStride] = 0; // force zero border
		z = 0;
		for (y = (h-2)*dstStride; y >= 0; y -= dstStride) {
			z += (alpha * (((int)(dst[y]) << ZPREC) - z)) >> APREC;
			dst[y] = (unsigned char)(z >> ZPREC);
		}
		dst[0] = 0; // force zero border
		dst++;
	}
}


static void fons__blur(struct FONScontext* stash, unsigned char* dst, int w, int h, int dstStride, int blur)
{
	if (blur < 1)
		return;
	// Calculate the alpha such that 90% of the kernel is within the radius. (Kernel extends to infinity)
	float sigma = (float)blur * 0.57735f; // 1 / sqrt(3)
	int alpha = (int)((1<<APREC) * (1.0f - expf(-2.3f / (sigma+1.0f))));
	fons__blurrows(dst, w, h, dstStride, alpha);
	fons__blurcols(dst, w, h, dstStride, alpha);
	fons__blurrows(dst, w, h, dstStride, alpha);
	fons__blurcols(dst, w, h, dstStride, alpha);
//	fons__blurrows(dst, w, h, dstStride, alpha);
//	fons__blurcols(dst, w, h, dstStride, alpha);
}


static struct FONSglyph* fons__getGlyph(struct FONScontext* stash, struct FONSfont* font, unsigned int codepoint,
										short isize, short iblur)
{
	int i, g, advance, lsb, x0, y0, x1, y1, gw, gh;
	float scale;
	struct FONSglyph* glyph = NULL;
	unsigned int h;
	float size = isize/10.0f;
	int rh, pad;
	struct FONSrow* br;

	if (isize < 2) return NULL;
	if (iblur > 20) iblur = 20;
	pad = iblur+1;

	// Reset allocator.
	stash->nscratch = 0;

	// Find code point and size.
	h = fons__hashint(codepoint) & (FONS_HASH_LUT_SIZE-1);
	i = font->lut[h];
	while (i != -1) {
		if (font->glyphs[i].codepoint == codepoint && font->glyphs[i].size == isize && font->glyphs[i].blur == iblur)
			return &font->glyphs[i];
		i = font->glyphs[i].next;
	}

	// Could not find glyph, create it.
	scale = stbtt_ScaleForPixelHeight(&font->font, size);
	g = stbtt_FindGlyphIndex(&font->font, codepoint);
	stbtt_GetGlyphHMetrics(&font->font, g, &advance, &lsb);
	stbtt_GetGlyphBitmapBox(&font->font, g, scale,scale, &x0,&y0,&x1,&y1);
	gw = x1-x0 + pad*2;
	gh = y1-y0 + pad*2;

	// Find row where the glyph can be fit.
	br = NULL;
	rh = (gh+7) & ~7;
	for (i = 0; i < stash->nrows; ++i) {
		int rmax = stash->rows[i].h, rmin = rmax - rmax/4;
		if (rh >= rmin && rh <= rmax && (stash->rows[i].x+gw+1) <= stash->params.width) {
			br = &stash->rows[i];
			break;
		}
	}

	// If no row found, add new.
	if (br == NULL) {
		short py = 0;
		// Check that there is enough space.
		if (stash->nrows > 0) {
			py = stash->rows[stash->nrows-1].y + stash->rows[stash->nrows-1].h+1;
			if (py+rh > stash->params.height)
				return NULL;
		}
		// Init and add row
		br = fons__allocRow(stash);
		if (br == NULL)
			return NULL;
		br->x = 0;
		br->y = py;
		br->h = rh;
	}

	// Init glyph.
	glyph = fons__allocGlyph(font);
	glyph->codepoint = codepoint;
	glyph->size = isize;
	glyph->blur = iblur;
	glyph->index = g;
	glyph->x0 = br->x;
	glyph->y0 = br->y;
	glyph->x1 = glyph->x0+gw;
	glyph->y1 = glyph->y0+gh;
	glyph->xadv = (short)(scale * advance * 10.0f);
	glyph->xoff = x0 - pad;
	glyph->yoff = y0 - pad;
	glyph->next = 0;

	// Advance row location.
	br->x += gw+1;

	// Insert char to hash lookup.
	glyph->next = font->lut[h];
	font->lut[h] = font->nglyphs-1;

	// Rasterize
	unsigned char* dst = &stash->texData[(glyph->x0+pad) + (glyph->y0+pad) * stash->params.width];
	stbtt_MakeGlyphBitmap(&font->font, dst, gw-pad*2,gh-pad*2, stash->params.width, scale,scale, g);

	/*
	// Debug code to color the glyph background
	int x,y;
	unsigned char* fdst = &stash->texData[glyph->x0 + glyph->y0 * stash->params.width];
	for (y = 0; y < gh; y++) {
		for (x = 0; x < gw; x++) {
			int a = (int)fdst[x+y*stash->params.width] + 20;
			if (a > 255) a = 255;
			fdst[x+y*stash->params.width] = a;
		}
	}
	*/

	// Blur
	if (iblur > 0) {
		stash->nscratch = 0;
		unsigned char* bdst = &stash->texData[glyph->x0 + glyph->y0 * stash->params.width];
		fons__blur(stash, bdst, gw,gh, stash->params.width, iblur);
	}

	stash->dirtyRect[0] = fons__mini(stash->dirtyRect[0], glyph->x0);
	stash->dirtyRect[1] = fons__mini(stash->dirtyRect[1], glyph->y0);
	stash->dirtyRect[2] = fons__maxi(stash->dirtyRect[2], glyph->x1);
	stash->dirtyRect[3] = fons__maxi(stash->dirtyRect[3], glyph->y1);

	return glyph;
}

static void fons__getQuad(struct FONScontext* stash, struct FONSfont* font,
						   struct FONSglyph* prevGlyph, struct FONSglyph* glyph,
						   float scale, float spacing, float* x, float* y, struct FONSquad* q)
{
	int rx,ry;
	if (prevGlyph) {
		float adv = stbtt_GetGlyphKernAdvance(&font->font, prevGlyph->index, glyph->index) * scale;
		*x += adv;
	}

	if (stash->params.flags & FONS_ZERO_TOPLEFT) {
		rx = (int)(*x + glyph->xoff);
		ry = (int)(*y + glyph->yoff);

		q->x0 = rx;
		q->y0 = ry;
		q->x1 = rx + glyph->x1 - glyph->x0;
		q->y1 = ry + glyph->y1 - glyph->y0;

		q->s0 = (glyph->x0) * stash->itw;
		q->t0 = (glyph->y0) * stash->ith;
		q->s1 = (glyph->x1) * stash->itw;
		q->t1 = (glyph->y1) * stash->ith;
	} else {
		rx = (int)(*x + glyph->xoff);
		ry = (int)(*y - glyph->yoff);

		q->x0 = rx;
		q->y0 = ry;
		q->x1 = rx + glyph->x1 - glyph->x0;
		q->y1 = ry - glyph->y1 + glyph->y0;

		q->s0 = (glyph->x0) * stash->itw;
		q->t0 = (glyph->y0) * stash->ith;
		q->s1 = (glyph->x1) * stash->itw;
		q->t1 = (glyph->y1) * stash->ith;
	}

	*x += glyph->xadv / 10.0f + spacing;
}

static void fons__flush(struct FONScontext* stash)
{
	// Flush texture
	if (stash->dirtyRect[0] < stash->dirtyRect[2] && stash->dirtyRect[1] < stash->dirtyRect[3]) {
		stash->params.renderUpdate(stash->params.userPtr, stash->dirtyRect, stash->texData);
		// Reset dirty rect
		stash->dirtyRect[0] = stash->params.width;
		stash->dirtyRect[1] = stash->params.height;
		stash->dirtyRect[2] = 0;
		stash->dirtyRect[3] = 0;
	}

	// Flush triangles
	if (stash->nverts > 0) {
		stash->params.renderDraw(stash->params.userPtr, stash->verts, stash->tcoords, stash->colors, stash->nverts);
		stash->nverts = 0;
	}
}

static inline void fons__vertex(struct FONScontext* stash, float x, float y, float s, float t, unsigned int c)
{
	stash->verts[stash->nverts*2+0] = x;
	stash->verts[stash->nverts*2+1] = y;
	stash->tcoords[stash->nverts*2+0] = s;
	stash->tcoords[stash->nverts*2+1] = t;
	stash->colors[stash->nverts] = c;
	stash->nverts++;
}

static float fons__getVertAlign(struct FONScontext* stash, struct FONSfont* font, int align, short isize)
{
	if (stash->params.flags & FONS_ZERO_TOPLEFT) {
		if (align & FONS_ALIGN_TOP) {
			return font->ascender * (float)isize/10.0f;
		} else if (align & FONS_ALIGN_MIDDLE) {
			return (font->ascender + font->descender) / 2.0f * (float)isize/10.0f;
		} else if (align & FONS_ALIGN_BASELINE) {
			return 0.0f;
		} else if (align & FONS_ALIGN_BOTTOM) {
			return font->descender * (float)isize/10.0f;
		}
	} else {
		if (align & FONS_ALIGN_TOP) {
			return -font->ascender * (float)isize/10.0f;
		} else if (align & FONS_ALIGN_MIDDLE) {
			return -(font->ascender + font->descender) / 2.0f * (float)isize/10.0f;
		} else if (align & FONS_ALIGN_BASELINE) {
			return 0.0f;
		} else if (align & FONS_ALIGN_BOTTOM) {
			return -font->descender * (float)isize/10.0f;
		}
	}
	return 0.0;
}

void fonsDrawText(struct FONScontext* stash,
				  float x, float y,
				  const char* s, float* dx)
{
	struct FONSstate* state = fons__getState(stash);
	unsigned int codepoint;
	unsigned int utf8state = 0;
	struct FONSglyph* glyph = NULL;
	struct FONSglyph* prevGlyph = NULL;
	struct FONSquad q;
	short isize = (short)(state->size*10.0f);
	short iblur = (short)state->blur;
	float scale;
	struct FONSfont* font;

	if (stash == NULL) return;
	if (state->font < 0 || state->font >= stash->nfonts) return;
	font = stash->fonts[state->font];
	if (!font->data) return;

	scale = stbtt_ScaleForPixelHeight(&font->font, (float)isize/10.0f);

	float width;
	// Align horizontally
	if (state->align & FONS_ALIGN_LEFT) {
		// empty
	} else if (state->align & FONS_ALIGN_RIGHT) {
		fonsTextBounds(stash, s, &width, NULL);
		x -= width;
	} else if (state->align & FONS_ALIGN_CENTER) {
		fonsTextBounds(stash, s, &width, NULL);
		x -= width * 0.5f;
	}
	// Align vertically.
	y += fons__getVertAlign(stash, font, state->align, isize);

	for (; *s; ++s) {
		if (fons__decutf8(&utf8state, &codepoint, *(const unsigned char*)s))
			continue;
		glyph = fons__getGlyph(stash, font, codepoint, isize, iblur);
		if (glyph) {
			fons__getQuad(stash, font, prevGlyph, glyph, scale, state->spacing, &x, &y, &q);

			if (stash->nverts+6 > FONS_VERTEX_COUNT)
				fons__flush(stash);

			fons__vertex(stash, q.x0, q.y0, q.s0, q.t0, state->color);
			fons__vertex(stash, q.x1, q.y1, q.s1, q.t1, state->color);
			fons__vertex(stash, q.x1, q.y0, q.s1, q.t0, state->color);

			fons__vertex(stash, q.x0, q.y0, q.s0, q.t0, state->color);
			fons__vertex(stash, q.x0, q.y1, q.s0, q.t1, state->color);
			fons__vertex(stash, q.x1, q.y1, q.s1, q.t1, state->color);
		}
		prevGlyph = glyph;
	}
	fons__flush(stash);
	if (dx) *dx = x;
}

void fonsDrawDebug(struct FONScontext* stash, float x, float y)
{
	int w = stash->params.width;
	int h = stash->params.height;
	if (stash->nverts+6 > FONS_VERTEX_COUNT)
		fons__flush(stash);

	fons__vertex(stash, x+0, y+0, 0, 0, 0xffffffff);
	fons__vertex(stash, x+w, y+h, 1, 1, 0xffffffff);
	fons__vertex(stash, x+w, y+0, 1, 0, 0xffffffff);

	fons__vertex(stash, x+0, y+0, 0, 0, 0xffffffff);
	fons__vertex(stash, x+0, y+h, 0, 1, 0xffffffff);
	fons__vertex(stash, x+w, y+h, 1, 1, 0xffffffff);
}

void fonsTextBounds(struct FONScontext* stash,
					const char* s,
					float* width, float* bounds)
{
	struct FONSstate* state = fons__getState(stash);
	unsigned int codepoint;
	unsigned int utf8state = 0;
	struct FONSquad q;
	struct FONSglyph* glyph = NULL;
	struct FONSglyph* prevGlyph = NULL;
	short isize = (short)(state->size*10.0f);
	short iblur = (short)state->blur;
	float scale;
	struct FONSfont* font;
	float x = 0, y = 0, minx, miny, maxx, maxy;

	if (stash == NULL) return;
	if (state->font < 0 || state->font >= stash->nfonts) return;
	font = stash->fonts[state->font];
	if (!font->data) return;

	scale = stbtt_ScaleForPixelHeight(&font->font, (float)isize/10.0f);

	// Align vertically.
	y += fons__getVertAlign(stash, font, state->align, isize);

	minx = maxx = x;
	miny = maxy = y;

	for (; *s; ++s) {
		if (fons__decutf8(&utf8state, &codepoint, *(const unsigned char*)s))
			continue;
		glyph = fons__getGlyph(stash, font, codepoint, isize, iblur);
		if (glyph) {
			fons__getQuad(stash, font, prevGlyph, glyph, state->spacing, scale, &x, &y, &q);
			if (q.x0 < minx) minx = q.x0;
			if (q.x1 > maxx) maxx = q.x1;
			if (q.y1 < miny) miny = q.y1;
			if (q.y0 > maxy) maxy = q.y0;
		}
		prevGlyph = glyph;
	}
	if (width) {
		*width = x;
	}

	// Align horizontally
	if (state->align & FONS_ALIGN_LEFT) {
		// empty
	} else if (state->align & FONS_ALIGN_RIGHT) {
		minx -= x;
		maxx -= x;
	} else if (state->align & FONS_ALIGN_CENTER) {
		minx -= x * 0.5f;
		maxx -= x * 0.5f;
	}

	if (bounds) {
		bounds[0] = minx;
		bounds[1] = miny;
		bounds[2] = maxx;
		bounds[3] = maxy;
	}
}

void fonsVertMetrics(struct FONScontext* stash,
					 float* ascender, float* descender, float* lineh)
{
	struct FONSstate* state = fons__getState(stash);
	if (stash == NULL) return;
	if (state->font < 0 || state->font >= stash->nfonts) return;
	struct FONSfont* font = stash->fonts[state->font];
	short isize = (short)(state->size*10.0f);
	if (!font->data) return;

	if (ascender)
		*ascender = font->ascender*isize/10.0f;
	if (descender)
		*descender = font->descender*isize/10.0f;
	if (lineh)
		*lineh = font->lineh*isize/10.0f;
}

void fonsDelete(struct FONScontext* stash)
{
	int i;
	if (stash == NULL) return;

	if (stash->params.renderDelete)
		stash->params.renderDelete(stash->params.userPtr);

	for (i = 0; i < stash->nfonts; ++i)
		fons__freeFont(stash->fonts[i]);

	if (stash->fonts) free(stash->fonts);
	if (stash->rows) free(stash->rows);
	if (stash->texData) free(stash->texData);
	free(stash);
}

#endif
