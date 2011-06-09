/*
 * (C) 2010 Andy
 *
 * pixmap routines
 */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
//#include <linux/fb.h>
#include <sys/mman.h>
#include <signal.h>
#include <string.h>
#include <inttypes.h>

#include "pixop.h"

/*
 * XXX all the routines here should consider the rounding
 * due to pixmaps not using the full 8 bits.
 */

// fill rectangle in pixmap "dst" with color "color"
// bx, by = upper-left coord in dst pixmap (pixels)
// wd, height = width and height of rectangle (pixels)
int pix_fill(pixmap_t* dst, int bx, int by, int width, int height, int color)
{
	unsigned char *dp;
	int dst_stride;			// dst pixmap width in bytes
	int fill_stride;		// fill width in bytes
	int n;

	if (dst == NULL)
		return 0;
	c_truncate(&bx, &width, dst->width);
	c_truncate(&by, &height, dst->height);
	if (width <= 0 || height <= 0)
		return 0;
	dp = dst->surface;
	switch (dst->bpp) {
	case 4:	// 4 bits/pixel, 2 pixels per byte
	    color &= 0x0F;
	    color |= (color << 4);
	    dst_stride = (dst->width + 1)/2;
	    fill_stride = (width + 1)/2;
	    bx /= 2;			// first byte offset in row
	    dp += by*dst_stride;		// start row
	    dp += bx;			// first pixel to fill
	    break;
	default:
	    fprintf(stderr, "unsupported pix_fill depth %d\n",
			dst->bpp);
	    return 0;
	}
	for (n = 0; height-- > 0; dp += dst_stride, n += fill_stride)
		memset(dp, color, fill_stride);
	return width*height;		// num of pixels actually filled
}

/* transfer pixmap "src:sx,sy (width:height)" to pixmap "dst: dx, dy"
 * bg, if non-zero, is OR-ed to every byte in the dst region.
 */
int pix_blt(pixmap_t* dst, int dx, int dy,
	pixmap_t* src, int sx, int sy, int width, int height, int bg)
{
	unsigned char *dstp, *srcp;
	int dst_stride, src_stride;
	int i, j, n_bytes;

	if (dst == NULL || src == NULL)
		return 0;
	if (width < 0) {
		width = src->width;
		height = src->height;
	}

#ifdef DEBUG
	int s_dx = dx, s_dy = dy, s_sx = sx, s_sy = sy,
		s_width = width, s_height = height;
#define M(a)	do { if (a != s_ ## a)	\
		fprintf(stderr, #a " modified from %d to %d\n", s_ ## a, a); \
		while (0)
#else
#define M(a)
#endif

	c_truncate(&sx, &width, src->width);
	c_truncate(&sy, &height, src->height);
	c_truncate(&dx, &width, dst->width);
	c_truncate(&dy, &height, dst->height);

	M(dx); M(dy); M(sx); M(sy); M(width); M(height);

#ifdef DEBUG
	fprintf(stderr, "src: %dx%d dst: %dx%d\n",
		src->width, src->height, dst->width, dst->height);

	fprintf(stderr, "copy: %dx%d@%d+%d -> %d+%d\n\n",
		width, height, sx, sy, dx, dy);
#endif

	if (height <= 0 || width <= 0) {
		// fprintf(stderr, "aborting copy\n");
		return 0;
	}

	dst_stride = (dst->width*dst->bpp + 7)/8;	// dst width in bytes
	src_stride = (src->width*src->bpp + 7)/8;	// src width in bytes

	if (0 && dst->bpp == src->bpp) {
	    for (i = 8, j=1; i > 0; i >>= 1, j <<=1) {
		if (dst->bpp == i && dx % j == 0 && sx % j == 0)
			goto fast;
	    }
	}
#if 1
	/*
	 * Non optimized code, in case bpp or aligment do not match.
	 * Assumption: the sample on the left is on the LSbits of a byte.
	 *
	 * Algorithm: accumulate samples in a variable, then update destination.
	 * We keep two 16-bit windows for both src and dst.
	 * We need a byte and bit offset pointer in both.
	 * The bpp adaptation is done simply by scaling according to
	 * the difference in max values.
	 */
	
	/* base positions in bytes */
	dstp = dst->surface + dy*dst_stride + dx*dst->bpp/8;
	srcp = src->surface + sy*src_stride + sx*src->bpp/8;

	uint16_t srcmask = ( (1<<src->bpp) - 1);
	uint16_t dstmask = ( (1<<dst->bpp) - 1);

	for (i = 0; i < height; i++) {
	    uint8_t srcofs = (sx*dst->bpp % 8);
	    uint8_t dstofs = (dx*dst->bpp % 8);
	    uint8_t *sp = srcp, *dp = dstp;
	    uint16_t sd = sp[0] + (sp[1]<<8);
	    uint16_t dd = dp[0] + (dp[1]<<8);
	    for (j = 0; j < width; j++) {
		uint16_t x = (sd >> srcofs) & srcmask;
		// XXX potentially extend/reduce size
		if (src->bpp != dst->bpp) /* scale */
			x = x * ( (1<<dst->bpp) - 1) / ( (1<<src->bpp) - 1);
		/* clear destination and update */
		x |= bg;
		dd &= ~(dstmask << dstofs);
		dd |=  (x << dstofs);
		/* advance source */
		srcofs += src->bpp;
		if (srcofs >= 8) {
			srcofs -= 8;
			sp++;
			sd = (sd >>8) + (sp[1] << 8);
		}
		/* advance destination */
		dstofs += dst->bpp;
		if (dstofs >= 8) {
			dstofs -= 8;
			dp[0] = dd & 0xff; /* write back */
			dp++;
			dd = (dd >>8) + (dp[1] << 8);
		}
	    }
	    /* final write */
	    dp[0] = dd & 0xff;
	    dp[1] = (dd >> 8);
	    dstp += dst_stride;
	    srcp += src_stride;
	}
#endif

fast:
	/* if we get here, data is byte-aligned */
	dx = dx*dst->bpp/8;	// horz byte offset, truncated
	sx = sx*src->bpp/8;	// horz byte offset, truncated

	dstp = dst->surface + dy*dst_stride + dx;
	srcp = src->surface + sy*src_stride + sx;
	n_bytes = width *dst->bpp/8;	// truncated

	for (i = 0; i < height; i++) {
		memcpy(dstp, srcp, n_bytes);
		if (bg) {
			for (j=0; j< n_bytes; j++)
				dstp[j] |= bg;
		}
		dstp += dst_stride;
		srcp += src_stride;
	}
	return width*height;		// num of pixels transferred
}

void pix_invert(pixmap_t *p)
{
	int nbytes ;
	unsigned char *tmp ;

	if (!p)
		return;
	nbytes = (p->width+1 /2) * (p->height) ;
	for (tmp = p->surface ; nbytes--; tmp++)
		*tmp = ~(*tmp) ;
}

pixmap_t * pix_alloc(int w, int h)
{
	int size = ((w+1)/2)*h + sizeof(pixmap_t) ;
	pixmap_t * p = (pixmap_t *)calloc(1, size);
	if (p) {
		p->width = w ;
		p->height = h ;
		p->surface = (unsigned char *)(p + 1);
	}

	return p ;
}

void pix_free(pixmap_t *p)
{
	if (p)
		free(p) ;
}
