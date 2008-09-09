/****************************************************************************
* This part has been adapted from HugoGC
* Original code by Softdev (@tehskeen.com)
* Adapted for Gnuboy Port by Eke-Eke (@tehskeen.com)
* ****************************************************************************/
/*****************************************************************************
 * IPL FONT Engine
 *
 * Based on Qoob MP3 Player Font
 * Added IPL font extraction
 *****************************************************************************/
#include "defs.h"
#include "font.h"
#include "menuback.h"

/*** Backdrop ***/
char backdrop[(640 * 480 * 2) + 32];

/* Backdrop Frame Width (to avoid writing outside of the background frame) */
u16 back_framewidth = 640;

typedef struct
{
  unsigned short font_type, first_char, last_char, subst_char, ascent_units, descent_units, widest_char_width,
                 leading_space, cell_width, cell_height;
  unsigned long texture_size;
  unsigned short texture_format, texture_columns, texture_rows, texture_width, texture_height, offset_charwidth;
  unsigned long offset_tile, size_tile;
} FONT_HEADER;

static unsigned char fontWork[ 0x20000 ] __attribute__((aligned(32)));
static unsigned char fontFont[ 0x40000 ] __attribute__((aligned(32)));
extern unsigned int *xfb[2];
extern int whichfb;

/****************************************************************************
 * YAY0 Decoding
 ****************************************************************************/
/* Yay0 decompression */
void yay0_decode(unsigned char *s, unsigned char *d)
{
	int i, j, k, p, q, cnt;

	i = *(unsigned long *)(s + 4);	  // size of decoded data
	j = *(unsigned long *)(s + 8);	  // link table
	k = *(unsigned long *)(s + 12);	 // byte chunks and count modifiers

	q = 0;					// current offset in dest buffer
	cnt = 0;				// mask bit counter
	p = 16;					// current offset in mask table

	unsigned long r22 = 0, r5;
	
	do
	{
		// if all bits are done, get next mask
		if(cnt == 0)
		{
			// read word from mask data block
			r22 = *(unsigned long *)(s + p);
			p += 4;
			cnt = 32;   // bit counter
		}
		// if next bit is set, chunk is non-linked
		if(r22 & 0x80000000)
		{
			// get next byte
			*(unsigned char *)(d + q) = *(unsigned char *)(s + k);
			k++, q++;
		}
		// do copy, otherwise
		else
		{
			// read 16-bit from link table
			int r26 = *(unsigned short *)(s + j);
			j += 2;
			// 'offset'
			int r25 = q - (r26 & 0xfff);
			// 'count'
			int r30 = r26 >> 12;
			if(r30 == 0)
			{
				// get 'count' modifier
				r5 = *(unsigned char *)(s + k);
				k++;
				r30 = r5 + 18;
			}
			else r30 += 2;
			// do block copy
			unsigned char *pt = ((unsigned char*)d) + r25;
			int i;
			for(i=0; i<r30; i++)
			{
				*(unsigned char *)(d + q) = *(unsigned char *)(pt - 1);
				q++, pt++;
			}
		}
		// next bit in mask
		r22 <<= 1;
		cnt--;

	} while(q < i);
}

void untile(unsigned char *dst, unsigned char *src, int xres, int yres)
{
	// 8x8 tiles
	int x, y;
	int t=0;
	for (y = 0; y < yres; y += 8)
		for (x = 0; x < xres; x += 8)
		{
			t = !t;
			int iy, ix;
			for (iy = 0; iy < 8; ++iy, src+=2)
			{
				unsigned char *d = dst + (y + iy) * xres + x;
				for (ix = 0; ix < 2; ++ix)
				{
					int v = src[ix];
					*d++ = ((v>>6)&3);
					*d++ = ((v>>4)&3);
					*d++ = ((v>>2)&3);
					*d++ = ((v)&3);
				}
			}
		}
}

int font_offset[256], font_size[256], fheight;
extern void __SYS_ReadROM(void *buf,u32 len,u32 offset);

void init_font(void)
{
	int i;

	__SYS_ReadROM((unsigned char *)&fontFont,0x3000,0x1FCF00);
	yay0_decode((unsigned char *)&fontFont, (unsigned char *)&fontWork);
	FONT_HEADER *fnt;

	fnt = ( FONT_HEADER * )&fontWork;

	untile((unsigned char*)&fontFont, (unsigned char*)&fontWork[fnt->offset_tile], fnt->texture_width, fnt->texture_height);

	for (i=0; i<256; ++i)
	{
		int c = i;

		if ((c < fnt->first_char) || (c > fnt->last_char)) c = fnt->subst_char;
		else c -= fnt->first_char;

		font_size[i] = ((unsigned char*)fnt)[fnt->offset_charwidth + c];

		int r = c / fnt->texture_columns;
		c %= fnt->texture_columns;
		font_offset[i] = (r * fnt->cell_height) * fnt->texture_width + (c * fnt->cell_width);
	}
	
	fheight = fnt->cell_height;
}

#define TRANSPARENCY (COLOR_WHITE)

unsigned int blit_lookup_inv[4] = {COLOR_BLACK, 0x6d896d77, 0xb584b57b, 0xff80ff80};
unsigned int blit_lookup[4] = {COLOR_WHITE, 0xb584b57b, 0x6d896d77, 0x258e2573};
unsigned int blit_lookup_norm[4] = {COLOR_WHITE, COLOR_BLACK, COLOR_BLACK, COLOR_BLACK};

void blit_char(int x, int y, unsigned char c, unsigned int *lookup)
{
	unsigned char *fnt = ((unsigned char*)fontFont) + font_offset[c];
	int ay, ax;
	unsigned int llookup;

	for (ay=0; ay<fheight; ++ay)
	{
		int h = (ay + y) * 320;

		for (ax=0; ax<font_size[c]; ax++)
		{
			int v0 = fnt[ax];
			int p = h + (( ax + x ) >> 1);
			unsigned long o = xfb[whichfb][p];

			llookup = lookup[v0];

			if ((v0== 0) && (llookup == TRANSPARENCY))
				llookup = o;

			if ((ax+x) & 1)
			{
				o &= ~0x00FFFFFF;
				o |= llookup & 0x00FFFFFF;
			}
			else
			{
				o &= ~0xFF000000;
				o |= llookup & 0xFF000000;
			}

			xfb[whichfb][p] = o;
		}
		
		fnt += 512;
	}
}

u8 norm_blit = 1;

void write_font(int x, int y, const unsigned char *string)
{
	int ox = x;
	while (*string && (x < (ox + back_framewidth)))
	{
		blit_char(x, y, *string, norm_blit ? blit_lookup_norm : blit_lookup);
		x += font_size[*string];
		string++;
	}
}


void writex(int x, int y, int sx, int sy, const unsigned char *string, unsigned int *lookup)
{
	int ox = x;
	while ((*string) && ((x) < (ox + sx)))
	{
		blit_char(x, y, *string, lookup);
		x += font_size[*string];
		string++;
	}
	
	int ay;
	for (ay=0; ay<sy; ay++)
	{
		int ax;
		for (ax=x; ax<(ox + sx); ax += 2) xfb[whichfb][(ay+y)*320+ax/2] = lookup[0];
	}
}

void WriteCentre( int y, const unsigned char *string)
{
	int x, t;
	for (x=t=0; t<strlen(string); t++) x += font_size[string[t]];
	if (x>back_framewidth) x=back_framewidth;
	x = (640 - x) >> 1;
	write_font(x, y, string);
}


void WriteCentre_HL( int y, const unsigned char *string)
{
	int x,t,h;
    for (x=t=0; t<strlen(string); t++) x += font_size[string[t]];
	if (x>back_framewidth) x = back_framewidth;
	h = x;
	x = (640 - x) >> 1;
	writex(x, y, h, fheight, string, blit_lookup_inv);
}


/****************************************************************************
 *  Draw functions
 *
 ****************************************************************************/
void fntDrawHLine (int x1, int x2, int y, int color)
{
  int i;
  y = 320 * y;
  x1 >>= 1;
  x2 >>= 1;
  for (i = x1; i <= x2; i++) xfb[whichfb][y + i] = color;
}

void fntDrawVLine (int x, int y1, int y2, int color)
{
  int i;
  x >>= 1;
  for (i = y1; i <= y2; i++) xfb[whichfb][x + (640 * i) / 2] = color;
}

void fntDrawBox (int x1, int y1, int x2, int y2, int color)
{
  fntDrawHLine (x1, x2, y1, color);
  fntDrawHLine (x1, x2, y2, color);
  fntDrawVLine (x1, y1, y2, color);
  fntDrawVLine (x2, y1, y2, color);
}

void fntDrawBoxFilled (int x1, int y1, int x2, int y2, int color)
{
  int h;
  for (h = y1; h <= y2; h++) fntDrawHLine (x1, x2, h, color);
}

/****************************************************************************
 *  Display functions
 *
 ****************************************************************************/
u8 SILENT = 0;

void SetScreen ()
{
  VIDEO_SetNextFramebuffer (xfb[whichfb]);
  VIDEO_Flush ();
  VIDEO_WaitVSync ();
}

void ClearScreen ()
{
  whichfb ^= 1;
  memcpy (xfb[whichfb], &backdrop, 1280 * 480);
  back_framewidth = 640;
}

void WaitPrompt (char *msg)
{
  int quit = 0;

  if (SILENT) return;

  while (PAD_ButtonsDown(0) & PAD_BUTTON_A) {};
  while (!(PAD_ButtonsDown(0) & PAD_BUTTON_A) && (quit == 0))
  {
      ClearScreen();
      WriteCentre(254, msg);
      WriteCentre(254 + fheight, "Press A to Continue");
      SetScreen();
      while (!(PAD_ButtonsDown(0) & PAD_BUTTON_A));
  }
}

void ShowAction (char *msg)
{
  if (SILENT) return;

  ClearScreen();
  WriteCentre(254, msg);
  SetScreen();
}

void WaitButtonA ()
{
  while (PAD_ButtonsDown(0) & PAD_BUTTON_A) {};
  while (!(PAD_ButtonsDown(0) & PAD_BUTTON_A));
}

/****************************************************************************
 * Unpack Backdrop
 *
 * Called at startup to unpack our backdrop to a temporary 
 * framebuffer.
 ****************************************************************************/
void unpackBackdrop ()
{
  unsigned long res, inbytes, outbytes;

  inbytes = BACK_COMPRESSED;
  outbytes = BACK_RAW;
  res = uncompress ((char *) &backdrop[0], &outbytes, (char *) &menuback[0], inbytes);
  if (res != Z_OK) while (1);
}
