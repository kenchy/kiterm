/*
 * Copyright (C) 2010 Luigi Rizzo, Universita' di Pisa
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * $Id: cp437.c 7989 2010-12-06 13:47:53Z luigi $
 *
 * code to map cp437 into html entities
 */

#include "myts.h"
#include <ctype.h>
#include <stdlib.h>	/* strtol */

static char cp437[] = " \n\
0x00    0x0000 	&#32;        	NULL\n\
0x01  	0x263A	&#9786;	While Smiling Face\n\
0x02  	0x263B	&#9787;	Black Smiling Face\n\
0x03  	0x2665	&#9829;	Black Heart Suit\n\
0x04  	0x2666	&#9830;	Black Diamond Suit\n\
0x05  	0x2663	&#9827;	Black Club Suit\n\
0x06  	0x2660	&#9824;	Black Spades Suit\n\
0x07  	0x2022	&#8226;	Bullet\n\
0x08  	0x25D8	&#9688;	Inverse Bullet\n\
0x09  	0x25CB	&#9675;	White Circle\n\
0x0A  	0x25D9	&#9689;	Inverse White Circle\n\
0x0B  	0x2642	&#9794;	Male Sign\n\
0x0C  	0x2640	&#9792;	Female Sign\n\
0x0D  	0x266A	&#9834;	Eighth Note\n\
0x0E  	0x266B	&#9835;	Beamed Eighth Note\n\
0x0F  	0x263C	&#9788;	White Sun With Rays\n\
0x10  	0x25BA	&#9658;	Black Right-Pointing Pointer\n\
0x11  	0x25C4	&#9668;	Black Left-Pointing Pointer\n\
0x12  	0x2195	&#8597;	Up Down Arrow\n\
0x13  	0x203C	&#8252;	Double Exclamation Mark\n\
0x14  	0x00B6	&#182;	Pilcrow Sign\n\
0x15  	0x00A7	&#167;	Section Sign\n\
0x16  	0x25AC	&#9644;	Black Rectangle\n\
0x17  	0x21A8	&#8616;	Up Down Arrow With Base\n\
0x18  	0x2191	&#8593;	Upwards Arrow\n\
0x19  	0x2193	&#8595;	Downwards Arrow\n\
0x1A  	0x2192	&#8594;	Rightwards Arrow\n\
0x1B  	0x2190	&#8592;	Leftwards Arrow\n\
0x1C  	0x221F	&#8735;	Right Angle\n\
0x1D  	0x2194	&#8596;	Left Right Arrow\n\
0x1E  	0x25B2	&#9650;	Black Up-Pointing Triangle\n\
0x1F  	0x25BC	&#9660;	Black Down-Pointing Triangle\n\
0x7F  	0x2302	&#8962;	House\n\
0x80  	0x00C7	&#199;	Latin Capital Letter C With Cedilla\n\
0x81  	0x00FC	&#252;	Latin Small Letter U With Diaeresis\n\
0x82  	0x00E9	&#233;	Latin Small Letter E With Acute\n\
0x83  	0x00E2	&#226;	Latin Small Letter A With Circumflex\n\
0x84  	0x00E4	&#228;	Latin Small Letter A With Diaeresis\n\
0x85  	0x00E0	&#224;	Latin Small Letter A With Grave\n\
0x86  	0x00E5	&#229;	Latin Small Letter A With Ring Above\n\
0x87  	0x00E7	&#231;	Latin Small Letter C With Cedilla\n\
0x88  	0x00EA	&#234;	Latin Small Letter E With Circumflex\n\
0x89  	0x00EB	&#235;	Latin Small Letter E With Diaeresis\n\
0x8A  	0x00E8	&#232;	Latin Small Letter E With Grave\n\
0x8B  	0x00EF	&#239;	Latin Small Letter I With Diaeresis\n\
0x8C  	0x00EE	&#238;	Latin Small Letter I With Circumflex\n\
0x8D  	0x00EC	&#236;	Latin Small Letter I With Grave\n\
0x8E  	0x00C4	&#196;	Latin Capital Letter A With Diaeresis\n\
0x8F  	0x00C5	&#197;	Latin Capital Letter A With Ring Above\n\
0x90  	0x00C9	&#201;	Latin Capital Letter E With Acute\n\
0x91  	0x00E6	&#230;	Latin Small Ligature Ae\n\
0x92  	0x00C6	&#198;	Latin Capital Ligature Ae\n\
0x93  	0x00F4	&#244;	Latin Small Letter O With Circumflex\n\
0x94  	0x00F6	&#246;	Latin Small Letter O With Diaeresis\n\
0x95  	0x00F2	&#242;	Latin Small Letter O With Grave\n\
0x96  	0x00FB	&#251;	Latin Small Letter U With Circumflex\n\
0x97  	0x00F9	&#249;	Latin Small Letter U With Grave\n\
0x98  	0x00FF	&#255;	Latin Small Letter Y With Diaeresis\n\
0x99  	0x00D6	&#214;	Latin Capital Letter O With Diaeresis\n\
0x9A  	0x00DC	&#220;	Latin Capital Letter U With Diaeresis\n\
0x9B  	0x00A2	&#162;	Cent Sign\n\
0x9C  	0x00A3	&#163;	Pound Sign\n\
0x9D  	0x00A5	&#165;	Yen Sign\n\
0x9E  	0x20A7	&#8359;	Peseta Sign\n\
0x9F  	0x0192	&#402;	Latin Small Letter F With Hook\n\
0xA0  	0x00E1	&#225;	Latin Small Letter A With Acute\n\
0xA1  	0x00ED	&#237;	Latin Small Letter I With Acute\n\
0xA2  	0x00F3	&#243;	Latin Small Letter O With Acute\n\
0xA3  	0x00FA	&#250;	Latin Small Letter U With Acute\n\
0xA4  	0x00F1	&#241;	Latin Small Letter N With Tilde\n\
0xA5  	0x00D1	&#209;	Latin Capital Letter N With Tilde\n\
0xA6  	0x00AA	&#170;	Feminine Ordinal Indicator\n\
0xA7  	0x00BA	&#186;	Masculine Ordinal Indicator\n\
0xA8  	0x00BF	&#191;	Inverted Question Mark\n\
0xA9  	0x2310	&#8976;	Reversed Not Sign\n\
0xAA  	0x00AC	&#172;	Not Sign\n\
0xAB  	0x00BD	&#189;	Vulgar Fraction One Half\n\
0xAC  	0x00BC	&#188;	Vulgar Fraction One Quarter\n\
0xAD  	0x00A1	&#161;	Inverted Exclamation Mark\n\
0xAE  	0x00AB	&#171;	Left-Pointing Double Angle Quotation Mark\n\
0xAF  	0x00BB	&#187;	Right-Pointing Double Angle Quotation Mark\n\
0xB0  	0x2591	&#9617;	Light Shade\n\
0xB1  	0x2592	&#9618;	Medium Shade\n\
0xB2  	0x2593	&#9619;	Dark Shade\n\
0xB3  	0x2502	&#9474;	Box Drawings Light Vertical\n\
0xB4  	0x2524	&#9508;	Box Drawings Light Vertical And Left\n\
0xB5  	0x2561	&#9569;	Box Drawings Vertical Single And Left Double\n\
0xB6  	0x2562	&#9570;	Box Drawings Vertical Double And Left Single\n\
0xB7  	0x2556	&#9558;	Box Drawings Down Double And Left Single\n\
0xB8  	0x2555	&#9557;	Box Drawings Down Single And Left Double\n\
0xB9  	0x2563	&#9571;	Box Drawings Double Vertical And Left\n\
0xBA  	0x2551	&#9553;	Box Drawings Double Vertical\n\
0xBB  	0x2557	&#9559;	Box Drawings Double Down And Left\n\
0xBC  	0x255D	&#9565;	Box Drawings Double Up And Left\n\
0xBD  	0x255C	&#9564;	Box Drawings Up Double And Left Single\n\
0xBE  	0x255B	&#9563;	Box Drawings Up Single And Left Double\n\
0xBF  	0x2510	&#9488;	Box Drawings Light Down And Left\n\
0xC0  	0x2514	&#9492;	Box Drawings Light Up And Right\n\
0xC1  	0x2534	&#9524;	Box Drawings Light Up And Horizontal\n\
0xC2  	0x252C	&#9516;	Box Drawings Light Down And Horizontal\n\
0xC3  	0x251C	&#9500;	Box Drawings Light Vertical And Right\n\
0xC4  	0x2500	&#9472;	Box Drawings Light Horizontal\n\
0xC5  	0x253C	&#9532;	Box Drawings Light Vertical And Horizontal\n\
0xC6  	0x255E	&#9566;	Box Drawings Vertical Single And Right Double\n\
0xC7  	0x255F	&#9567;	Box Drawings Vertical Double And Right Single\n\
0xC8  	0x255A	&#9562;	Box Drawings Double Up And Right\n\
0xC9  	0x2554	&#9556;	Box Drawings Double Down And Right\n\
0xCA  	0x2569	&#9577;	Box Drawings Double Up And Horizontal\n\
0xCB  	0x2566	&#9574;	Box Drawings Double Down And Horizontal\n\
0xCC  	0x2560	&#9568;	Box Drawings Double Vertical And Right\n\
0xCD  	0x2550	&#9552;	Box Drawings Double Horizontal\n\
0xCE  	0x256C	&#9580;	Box Drawings Double Vertical And Horizontal\n\
0xCF  	0x2567	&#9575;	Box Drawings Up Single And Horizontal Double\n\
0xD0  	0x2568	&#9576;	Box Drawings Up Double And Horizontal Single\n\
0xD1  	0x2564	&#9572;	Box Drawings Down Single And Horizontal Double\n\
0xD2  	0x2565	&#9573;	Box Drawings Down Double And Horizontal Single\n\
0xD3  	0x2559	&#9561;	Box Drawings Up Double And Right Single\n\
0xD4  	0x2558	&#9560;	Box Drawings Up Single And Right Double\n\
0xD5  	0x2552	&#9554;	Box Drawings Down Single And Right Double\n\
0xD6  	0x2553	&#9555;	Box Drawings Down Double And Right Single\n\
0xD7  	0x256B	&#9579;	Box Drawings Vertical Double And Horizontal Single\n\
0xD8  	0x256A	&#9578;	Box Drawings Vertical Single And Horizontal Double\n\
0xD9  	0x2518	&#9496;	Box Drawings Light Up And Left\n\
0xDA  	0x250C	&#9484;	Box Drawings Light Down And Right\n\
0xDB  	0x2588	&#9608;	Full Block\n\
0xDC  	0x2584	&#9604;	Lower Half Block\n\
0xDD  	0x258C	&#9612;	Left Half Block\n\
0xDE  	0x2590	&#9616;	Right Half Block\n\
0xDF  	0x2580	&#9600;	Upper Half Block\n\
0xE0  	0x03B1	&#945;	Greek Small Letter Alpha\n\
0xE1  	0x00DF	&#223;	Greek Capital Letter Beta / Latin Small Letter Esset\n\
0xE2  	0x0393	&#915;	Greek Capital Letter Gamma\n\
0xE3  	0x03C0	&#960;	Greek Small Letter Pi\n\
0xE4  	0x03A3	&#931;	Greek Capital Letter Sigma\n\
0xE5  	0x03C3	&#963;	Greek Small Letter Sigma\n\
0xE6  	0x00B5	&#181;	Greek Small Letter Mu\n\
0xE7  	0x03C4	&#964;	Greek Small Letter Tau\n\
0xE8  	0x03A6	&#934;	Greek Capital Letter Phi\n\
0xE9  	0x0398	&#920;	Greek Capital Letter Theta\n\
0xEA  	0x03A9	&#937;	Greek Capital Letter Omega\n\
0xEB  	0x03B4	&#948;	Greek Small Letter Delta\n\
0xEC  	0x221E	&#8734;	Infinity\n\
0xED  	0x03C6	&#966;	Greek Small Letter Phi\n\
0xEE  	0x03B5	&#949;	Greek Small Letter Epsilon\n\
0xEF  	0x2229	&#8745;	Intersection\n\
0xF0  	0x2261	&#8801;	Identical To\n\
0xF1  	0x00B1	&#177;	Plus-Minus Sign\n\
0xF2  	0x2265	&#8805;	Greater-Than Or Equal To\n\
0xF3  	0x2264	&#8804;	Less-Than Or Equal To\n\
0xF4  	0x2320	&#8992;	Top Half Integral\n\
0xF5  	0x2321	&#8993;	Bottom Half Integral\n\
0xF6  	0x00F7	&#247;	Division Sign\n\
0xF7  	0x2248	&#8776;	Almost Equal To\n\
0xF8  	0x00B0	&#176;	Degree Sign\n\
0xF9  	0x2219	&#8729;	Bullet Operator\n\
0xFA  	0x00B7	&#183;	Middle Dot\n\
0xFB  	0x221A	&#8730;	Square Root\n\
0xFC  	0x207F	&#8319;	Superscript Latin Small Letter N\n\
0xFD  	0x00B2	&#178;	Superscript Two\n\
0xFE  	0x25A0	&#9632;	Black Square\n\
0xFF   	0x00A0	&#160;	&nbsp; No-Break Space\n\
";

#include <stdio.h>
#include <string.h>
char *map437[256];
int build_map437(int argc, char *argv[])
{
    char *a, *b = cp437;
    DBG(2, "cp437 is %s\n", b);
    while ( (a = strsep(&b, "\n")) ) {
	int i = 0;
	char *v[3];
	v[0] = v[1] = v[2] = "-none-";
	for (i=0; a && i < 3;) {
	    char *c = strsep(&a, " \t");
	    if (!c)
		break;
	    if (!c[0] || (i == 2 && c[0] != '&'))
		continue;
	    v[i++] = c;
	}
	i = strtol(v[0], NULL, 0);
	if (isalnum(i) || i == 32)
	    v[2] = NULL;
	map437[i] = v[2];
	DBG(2, "fields %s %s\n", v[0], v[2] ? v[2] : "same");
    }
    map437[8] = map437[9] = map437[10] = map437[13] =
    map437[27] = map437[127] = NULL;
    return 0;
}
