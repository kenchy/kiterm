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
 * Inspired by launchpad code from Andy M. aka h1uke	h1ukeguy @ gmail.com
 * but there is barely any of the original code left here.
 */

/*
 * launchpad+terminal+webserver program for Kindle.
 * See top level README for details,
 * and config.c for the config file format.
 */

#define _GNU_SOURCE	/* asprintf */
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <libgen.h>	/* dirname */

#include <sys/stat.h>
#include <signal.h>

#ifdef __FreeBSD__
enum { EV_KEY = 1, };
struct input_event {
	struct timeval time;
	uint16_t type;
	uint16_t code;
	int32_t value;
};
#else
#include <linux/input.h>
#endif

#include "myts.h"
#include "config.h"
#include "dynstring.h"
#include "terminal.h"
#include "pixop.h"
#include "screen.h"

/*
 * Each key entry has a name, a type and one or two parameters.
 * The name is normally the ascii char (case sensitive)
 * or the key name (case insensitive).
 * The type is 0 for send, 1 for sendshift, 2 for sendfw, 3 for sym.
 * The code is the code to send, or the y steps for symbols.
 * Additionally, symbols need an x steps.
 */
enum k_type { KT_SEND = 0, KT_FW, KT_VOL, KT_SHIFT, KT_ALT, KT_SYM };
struct key_entry {
	char 	*name;	/* not null terminated */
	uint8_t namelen;
	uint8_t	type;	/* send, sendshift, sym */
	uint8_t	code;	/* or x-steps if sym */
	uint8_t ysteps;	/* if sym */
};

/* each I/O channel has different name and fd for input and output */
struct iodesc {
	char *namein;
	char *nameout;
	int fdin;
	int fdout;
};

/* support for multiple terminal sessions */
struct terminal {
	struct terminal *next;
	struct sess *the_shell;
	char name[0]; 	/* dynamically allocated */
};

/*
 * Overall state for the launchpad.
 * The destructor must:
 *	- preserve all terminal sessions
 *	- call cfg_free(db)
 *	- call cfg_read and init
 */
struct lp_state {
	/* e[] contains events sorted by name, with nentries entries.
	 * by_code[] is a direct access array to get an event by code,
	 * with pointers into e[]
	 */
	int		nentries;
	struct key_entry e[256];	/* table of in/out events */
	struct key_entry *by_code[256];	/* events by code */

	/* actions is a list of configured actions, pointing into the
	 * [actions] section of the db.
	 */
	struct config	*db;		/* the database */
	struct entry	*actions;	/* list of actions */
	char		*script_path;	/* where to look for scripts */

	int		xsym, ysym;	/* initial SYMBOL position (1, 1) */
	/* codes for various keys */
	int		fw_left, fw_right, fw_up, fw_down, fw_select;
	int		intro, trailer, del, sym;
	int		term_end, term_esc, term_shift, term_ctrl, term_sym;
	int		term_fn;

	int 		hot_interval;	/* duration of hot interval	*/
	int		key_delay;	/* inter-key delay		*/
	int		refresh_delay;	/* screen refresh delay		*/
	struct iodesc	kpad, fw, vol;	/* names and descriptors	*/

	/* dynamic state */

	/* fb, curterm, save_pixmap are either all set or all clear */
	struct terminal *curterm;	/* current session		*/
	fbscreen_t	*fb;		/* the framebuffer		*/
	dynstr		save_pixmap;	/* saved pixmap			*/

	/* various timeouts, nonzero if active */
	struct timeval	screen_due;	/* next screen refresh		*/
	struct timeval	hotkey_due;	/* end of hotkey mode		*/
	struct timeval	keys_due;	/* keys to send back to the kindle */
	dynstr		pending;	/* list of pending keypresses	*/

	volatile int	got_signal;	/* changed by the handler */
	int		hotkey_mode;
	int		hot_seq_len;
#define MAXSEQ 32
	uint8_t		hot_seq[MAXSEQ]; // input collected in hot mode
	uint8_t		hot_seq_dev[MAXSEQ]; // src dev for the above

	/* This area must be preserved on reinit */
	int		savearea[0];	/* area below preserved on reinit */
	struct terminal *allterm;	/* all terminal sessions	*/
	char		basedir[1024];
	char		*cfg_name;		/* points into basedir */
	int		verbose;
};


static struct lp_state lpad_desc;
static struct lp_state *lps;

static void capture_input(int capture);
/*
 * Debugging support to emulate events on the host.
 * 1: shift down, 2: shift up other chars are up+down
 */
int host_event(int fd, struct input_event *kbbuf, int l)
{
	char c, *pc;
	struct input_event *p = kbbuf;
	static int intro=0;
	static char map[] = /* same as Kindle keys */
	    "..1234567890....qwertyuiop....asdfghjkl.....zxcvbnm";
	int i = read(fd, &c, 1);

	memset(p, 0, l*2);
	if (i <= 0)
		return 0;
	if (isalpha(c))
		c = tolower(c);
	if (c == '1' && !intro) { /* introducer ON */
		*p++ = (struct input_event){ {0,0}, EV_KEY, lps->intro, 1 };
		intro = 1;
	} else if (c == '2' && intro) {
		*p++ = (struct input_event){ {0,0}, EV_KEY, lps->intro, 0 };
		intro = 0;
	} else if ( (pc = index(map, c)) ) {
		i = pc - map;
		DBG(2, "char %c maps to %d\n", c, i);
		*p++ = (struct input_event){ {0,0}, EV_KEY, i, 1 };
		*p++ = (struct input_event){ {0,0}, EV_KEY, i, 0 };
	}
	return (p - kbbuf) * sizeof(*p);	
}

static int is_kindle3(void)
{
	/* if all 3 /dev/input/event[012] are open then we are on Kindle3 */
	return (lps->kpad.fdin != -1 && lps->fw.fdin != -1 && lps->vol.fdin != -1);
}

/*
 * compare two key entries. Sorting is by length and then by string.
 * One-byte entries are case-sensitive, other are case-insensitive.
 * ecmp1() is used for the initial sorting (removing duplicates)
 * and breaks ties looking at the type (privileging short sequences)
 */
static int ecmp(const void *_l, const void *_r)
{
	const struct key_entry *l = _l, *r = _r;
	int d = l->namelen - r->namelen;
	
	if (d != 0)
		return d;
	return (l->namelen == 1) ? (int)l->name[0] - (int)r->name[0] :
		strncasecmp(l->name, r->name, l->namelen);
}

static int ecmp1(const void *_l, const void *_r)
{
	const struct key_entry *l = _l, *r = _r;
	int d = ecmp(l, r);
	return d ? d : l->type - r->type;
}

/*
 * maps a keydef to the correct key_entry, returns NULL if not found.
 */
static struct key_entry *lookup_key(const char *key, int len)
{
	struct key_entry l, *k;

	if (*key == ' ') {
		len = 0;
		key = "Space";
	}
	l.name = (char *)key;
	l.namelen = len ? len : strlen(key);
	k = bsearch(&l, lps->e, lps->nentries, sizeof(l), ecmp);
	if (!k)
		DBG(0, "entry '%.*s' not found\n", len, key);
	return k;
}

/*
 * Take an action entry and "compile" it, replacing the left size with
 * sequence of event codes, and setting len1 to the length.
 * The sequence on the right is left unchanged.
 */
static void compile_action(struct entry *k)
{
	char *src;
	uint8_t *dst;
	int len ;

	if (k->len1)	/* already compiled */
		return;
	dst = (uint8_t *)k->key;

	DBG(2, "sequence %s :\n", k->key);
	/*
	 * on the left hand side only entries of type KT_SEND are allowed,
	 * with a special case for single chars which are case-insensitive
	 */
	for (src = k->key; *src; src += len) {
		struct key_entry *e;
		char c = tolower(*src), *cp = src;	/* defaults... */
		len = strcspn(src, " \t");
		if (len == 0) {
			len = 1;
			continue;
		}
		if (len == 1)
			cp = &c;
		e = lookup_key(cp, len);
		if (e == NULL) {
			DBG(0, "%.*s key not found\n", len, src);
			break;
		}
		if (e->type != KT_SEND && e->type != KT_FW) {
			DBG(0, "%.*s key not valid as hotkey\n", len, src);
			break;
		}
		DBG(2, "\t%.*s becomes %d\n", len, src, e - lps->e);
		*dst++ = e->code;
	}
	k->len1 = dst - (uint8_t *)k->key;
	DBG(2, "\taction %s\n", k->value );
}

/*
 * Helper function that, given "N = a b c d ..." appends to lps.e[]
 * the key_entry sections for the keydefs on the right side.
 * lps.nentries points to the next free slot.
 */
static int build_seq(struct section *sec)
{
	const struct entry *k;
	struct key_entry *e = lps->e + lps->nentries;

	if (sec == NULL) {
		DBG(0, "section not found\n");
		return 1;
	}
	DBG(2, "exploring section %p\n", sec);
	for (k = cfg_find_entry(sec, NULL); k; k = k->next) {
		int l;
		char *s = k->key;
		DBG(2, "found %s = %s\n", k->key, k->value);
		e->type = KT_SEND;	/* normal */
		e->code = atoi(s);
		if ( index("sfv", *s) ) { /* shift fiveway volume */
			e->type = (*s == 's') ? KT_SHIFT : (*s == 'f' ? KT_FW : KT_VOL);
			e->code = atoi(s+1);
		} else if (!strncasecmp(s, "row", 3)) {
			e->type = KT_SYM; /* row */
			e->ysteps = atoi(s+3);
			e->code = 0;
		}
		s = k->value;
		while ( (l = strcspn(s, " \t")) || *s ) {
			if (l == 0) {
				s++;
				continue;
			}
			if (*s == '\\') {
				s++;
				l--;
			}
			e->name = s;
			e->namelen = l;
			e++;
			e[0] = e[-1];
			e->code++;	/* prepare next entry */
			s += l;
		}
	}
	lps->nentries = e - lps->e;
	DBG(1, "done %d entries\n", lps->nentries);
	return 0;
}

/* fetch a string, int or key name from the config file,
 * and store the value in dst. type can be
 *	's'	to store a string;
 *	'i'	to store an int;
 *	'k'	to store a keycode (lookup the value on the right);
 */
static int setVal(struct section *sec, const char *key, int type, void *dst)
{
	struct key_entry *e;
	const struct entry *k = cfg_find_entry(sec, key);

	if (!k)
		return 1;

	switch (type) {
	case 's':	/* string */
		*(char **)dst = k->value;
		break;
	case 'i':	/* int */
		*(int *)dst = strtol(k->value, NULL, 0) ;
		break;
	case 'k':	/* keycode */
		e = lookup_key(k->value, strlen(k->value));
		if (e == NULL || (e->type == KT_ALT || e->type == KT_SHIFT)) {
			DBG(0, "Warning: no code for %s %s\n", key, k->value) ;
			return 1;
		}
		*(int *)dst = e->code ;
		break;
	}
	return 0;
}

/* set dst with the code of the key called 'key' */
static int setKey(const char *key, int *dst)
{
	struct key_entry *e = lookup_key(key, strlen(key));
	if (e)
		*dst = e->code;
	return (e ? 0 : 1);
}

/*
 * reinitialize.
 */
static int launchpad_init(char *path)
{
	struct section *sec ;
	int i;
	struct key_entry *e;

	memset(lps, 0, (char *)&lps->savearea - (char *)lps);
	/* load initial values */
	lps->script_path = "";
	lps->hot_interval = 700;
	lps->key_delay = 50;
	lps->refresh_delay = 100;
	lps->kpad.fdin = lps->fw.fdin = lps->vol.fdin = -1;
	if (path == NULL)
		path = lps->cfg_name;

	lps->db = cfg_read(path, lps->basedir, NULL);
	if ( lps->db == NULL) {
		DBG(0, "%s -- not found or bad\n", path) ;
		return -1 ;
	}
	/* load file identifiers */
	sec = cfg_find_section(lps->db, "Settings");
	if (!sec) {
		DBG(0, "section Settings not found\n") ;
		return -1;
	}
	/* load system-independent values */
	setVal(sec, "HotInterval", 'i', &lps->hot_interval);
	setVal(sec, "ScriptDirectory", 's', &lps->script_path);
	setVal(sec, "InterKeyDelay", 'i', &lps->key_delay);
	setVal(sec, "RefreshDelay", 'i', &lps->refresh_delay);
	setVal(sec, "KpadIn", 's', &lps->kpad.namein);
	setVal(sec, "KpadOut", 's', &lps->kpad.nameout);
	setVal(sec, "FwIn", 's', &lps->fw.namein);
	setVal(sec, "FwOut", 's', &lps->fw.nameout);
	setVal(sec, "VolIn", 's', &lps->vol.namein);
	setVal(sec, "VolOut", 's', &lps->vol.nameout);

	/* try open files so we know on what system we are */
	i = O_RDONLY | O_NONBLOCK;
	lps->kpad.fdin	= open(lps->kpad.namein, i);
	lps->fw.fdin	= open(lps->fw.namein, i);
	lps->vol.fdin	= open(lps->vol.namein, i);
	DBG(2, "open %s %s %s gives %d %d %d\n",
		lps->kpad.namein, lps->fw.namein, lps->vol.namein,
		lps->kpad.fdin, lps->fw.fdin, lps->vol.fdin);

	if (lps->kpad.fdin == -1 && lps->fw.fdin == -1 && lps->vol.fdin) {
		DBG(0, "no input available, exiting...\n") ;
		return -1;
	}
	i = O_WRONLY | O_NONBLOCK;
	lps->kpad.fdout	= open(lps->kpad.nameout, i) ;
	lps->fw.fdout	= open(lps->fw.nameout, i);
	lps->vol.fdout	= open(lps->vol.nameout, i);
	/* ignore errors on output */

	/* load keymap entries (system-dependent) */
	build_seq(cfg_find_section(lps->db, "inkeys"));
	build_seq(cfg_find_section(lps->db,
		is_kindle3() ? "inkeys-k3" : "inkeys-dx"));
	DBG(2, "sort sequences\n");

	/* sort using the extended comparison function */
	qsort(lps->e, lps->nentries, sizeof(*e), ecmp1);
	/* now remove duplicates, keeping the shortest sequences */
	for (e = lps->e, i = 0; i < lps->nentries - 1; i++, e++) {
	    if (ecmp(e, e+1) == 0) {
		    DBG(1, "dup %3d for ty %d code %3d y %3d l %d %.*s\n",
			i, e->type, e->code, e->ysteps,
			e->namelen, e->namelen, e->name);
		    e[1].type = 255;
	    }
	}
	/* second pass, bubble up all duplicate elements */
	for (e = lps->e, i = 0; i < lps->nentries; i++, e++) {
		if (e->type == 255) {
			lps->nentries--;
			*e = lps->e[lps->nentries];
		}
	}
	/* sort again, this time using ecmp */
	qsort(lps->e, lps->nentries, sizeof(*e), ecmp);

	/* build the 'by_code' array, used in output */
	memset(lps->by_code, 0, sizeof(lps->by_code));
	DBG(2, "--- dump events by name ---\n");
	for (e = lps->e, i = 0; i < lps->nentries; i++, e++) {
		if (e->type == KT_SEND || e->type == KT_FW)
			lps->by_code[e->code] = e;
		DBG(2, "%3d ty %d code %3d y %3d l %d %.*s\n",
				i, e->type, e->code, e->ysteps,
				e->namelen, e->namelen, e->name);
	}
	DBG(2, "--- debugging -- dump events by code ---\n");
	for (i=0; i < 256; i++) {
		e = lps->by_code[i];
		if (!e) continue;
		DBG(2, "%3d ty %d code %3d y %3d l %d %.*s\n",
				i, e->type, e->code, e->ysteps,
				e->namelen, e->namelen, e->name);
	}

	/* load parameters that depend on the key mapping */
	setVal(sec, "Introducer", 'k', &lps->intro);
	setVal(sec, "Trailer", 'k', &lps->trailer);

	setVal(sec, "TermEnd", 'k', &lps->term_end);
	setVal(sec, "TermEsc", 'k', &lps->term_esc);
	setVal(sec, "TermCtrl", 'k', &lps->term_ctrl);
	setVal(sec, "TermShift", 'k', &lps->term_shift);
	setVal(sec, "TermSym", 'k', &lps->term_sym);
	setVal(sec, "TermFn", 'k', &lps->term_fn);
	lps->xsym = 1;	/* position of initial symbol */
	lps->ysym = 1;

	setKey("Sym", &lps->sym);
	setKey("Left", &lps->fw_left);
	setKey("Right", &lps->fw_right);
	setKey("Up", &lps->fw_up);
	setKey("Down", &lps->fw_down);
	setKey("Select", &lps->fw_select);
	setKey("Del", &lps->del);

	/* translate actions into event sequences */
	sec = cfg_find_section(lps->db, "Actions");
	if (sec) {
		struct entry *k;
		k = lps->actions = (struct entry *)cfg_find_entry(sec, NULL);
		for (; k; k = k->next)
			compile_action(k);
	}
	return 0 ;
}

/* locate a valid hotkey sequence */
static const struct entry *find_action(uint8_t *pseq, int len)
{
	const struct entry *k;
	for (k = lps->actions; k; k = k->next) {
		if (len == k->len1 && !bcmp(k->key, pseq, len)) {
			DBG(2, "found action %s\n", k->value);
			return k;
		}
	}
	return NULL ;
}

static char *get_file_contents(const char *path)
{
	int fd ;
	char *p = NULL ;
	unsigned long flen ;

	fd = open(path, O_RDONLY);
	if (fd < 0) {
		DBG(0, "Can't open kindle script file %s\n", path);
		return NULL;
	}
	flen = lseek(fd, 0, SEEK_END) ;
	lseek(fd, 0, SEEK_SET) ;

	p = calloc(1, flen+1);
	if (p == NULL) {
		DBG(0, "Can't allocate %lu bytes\n", flen);
	} else if (read(fd, p, flen) != flen) {
		DBG(0, "Can't read %lu bytes from %s\n", flen, path);
		free(p);
		p = NULL;
	}
	close(fd);
	return p;
}

/* record additional entries to send to the kindle. They will
 * be actually sent later to avoid blocking.
 */
static void send_event(uint8_t code, uint8_t mode)
{
	ds_append(&lps->pending, &code, 1);
	ds_append(&lps->pending, &mode, 1);
}

/* the actual send_event handler */
void send_event1(uint8_t code, uint8_t mode)
{
	char tmpbuf[32];
	int len = sprintf(tmpbuf, "send%s %d\n",
		mode == KT_SHIFT ? "shift" : "", code) ;
	int fd = (mode == KT_FW) ? lps->fw.fdout :
		(mode == KT_VOL) ? lps->vol.fdout : lps->kpad.fdout ;
	if (fd < 0)
		return;
	DBG(1, "mode %d %s", mode, tmpbuf);
	write(fd, tmpbuf, len) ;
}

/*
 * Send a key-entry to the kindle, possibly expand the SYM entries
 */
static void send_key_entry(struct key_entry *e)
{
	int i;

	if (e == NULL)
		return;
	if (e->type != KT_SYM) {
		send_event(e->code, e->type);
		return;
	}
	DBG(2, "symbol %.*s at x %d y %d\n",
		e->namelen, e->name, e->code, e->ysteps);
	send_event(lps->sym, KT_SEND);
	i = e->code;
	if (i < lps->xsym) { /* left */
		while (i++ < lps->xsym)
			send_event(lps->fw_left, KT_FW);
	} else if (i > lps->xsym) {
		while (i-- > lps->xsym)
			send_event(lps->fw_right, KT_FW);
	}
	i = e->ysteps;
	if (i < lps->ysym) { /* up */
		while (i++ < lps->ysym)
			send_event(lps->fw_up, KT_FW);
	} else if (i > lps->ysym) {
		while (i-- > lps->ysym)
			send_event(lps->fw_down, KT_FW);
	}
	send_event(lps->fw_select, KT_FW);
	send_event(lps->sym, KT_SEND);
}

static void send_key(const char *p, int len)
{
	send_key_entry(lookup_key(p, len));
}

/* send an ascii string to the kindle */
static void send_ascii_string(const char *p, int len)
{
	DBG(2, "%.*s\n", len, p);
	for ( ; len > 0 && *p; len--, p++) {
		if (*p == '\\' && p[1]) {
			p++ ;
			len--;
		}
		send_key(p, 1);
	}
};

/*
 * send a sequence of keywords and strings
 * start with a DEL to enter input mode.
 */
static void process_script(char *p)
{
	int i, l ;

	DBG(1, "%s\n", p);
	send_event(lps->del, KT_SEND);
	for (; *p ; p += l) {
		/* get the symbol or string length if in quotes */
		if (*p == '"' || *p == '\'') {
			for (l=1; p[l] && p[l] != p[0]; l++) {
				if (p[l] == '\\' && p[l+1])
					l++;
			}
			if (p[l] == p[0])
				l++;
		} else
			l = strcspn(p, " \t\r\n");
		DBG(2, "substring %p %.*s\n", p, l, p);
		if (l == 0) {
			l = strspn(p, " \t\r\n");
			DBG(2, "whitespace %p %.*s\n", p, l, p);
			continue;
		}
		i = l;
		switch(*p) {
		case '\'':	/* apostrophe, ignore final char */
			i--;
			if (p[i] == '\'')
				i--;
			send_key(p+1, i);
			break ;
		case '"':
			i--;
			if (p[i] == '"')
				i--;
			send_ascii_string(p+1, i) ;
			break ;
		default:
			send_ascii_string(p, l) ;
			break ;
		}
	}
}

struct terminal *shell_find(const char *name);
//static void print_help(void);

static int execute_action(const struct entry *k)
{
	char *tmp = NULL, *p = k->value ;
	char *must_free = NULL ;
	int l;

	DBG(1, "%s\n", p);

	switch(*p) {
	case '!':	/* exec a shell command */
		if (!strncmp(p+1, "terminal", 8)) {
			struct terminal *t = shell_find(p+1);
			DBG(0, "start %s got %p\n", p+1, t);
			if (t == NULL)
				return 1;
			lps->fb = fb_open();	/* also mark terminal mode */
			if (lps->fb) {	/* if success, input is for us */
				pixmap_t *pix = &lps->fb->pixmap;
				int l = pix->width * pix->height * pix->bpp / 8;
				lps->curterm = t;
				ds_reset(lps->save_pixmap);
				ds_append(&lps->save_pixmap, pix->surface, l);
				//print_help();
				capture_input(1) ;
				// set a timeout to popup the terminal
				gettimeofday(&lps->screen_due, NULL);
			}
			return 0;
		}
		DBG(1, "call system %s\n", p+1);
		return system(p+1);

	case '@':	/* take keys from file, then as above */
		asprintf(&tmp, "%s/%s", lps->script_path, p+1);
		DBG(1, "opening %s\n", tmp);
		must_free = p = get_file_contents(tmp);
		if (!must_free)
			return 1;
		p = skipws(p);
		/* remove initial send_string and trailing DEL */
		if (strstr(p, "send_string") == p)
			p += strlen("send_string");
		trimws(p, NULL);
		l = strlen(p);
		if (!strcasecmp("'del'", p + l - 5))
			p[l-5] = '\0';
		process_script(p);
		break;

	case '#':	/* send keys as specified */
		process_script(p+1);
		break ;

	default:	/* send keys immediately */
		process_script(p) ;
		break ;
	}
	if (must_free)
		free(must_free);
	DBG(1, "sent %d keys\n", ds_len(lps->pending)/2);
	gettimeofday(&lps->keys_due, NULL);
	return 0 ;
}

/* block or unblock input events to the kindle.
 * If we detect the initiator, we block events, and release them
 * after we are done. Setting capture mode also creates a timeout
 * upon which we will unblock.
 */
static void capture_input(int capture)
{
	capture = capture ? 1 : 0 ; /* normalize */
	if (capture) {
		gettimeofday(&lps->hotkey_due, NULL);
		timeradd_ms(&lps->hotkey_due, lps->hot_interval, &lps->hotkey_due);
	} else {
		timerclear(&lps->hotkey_due);
	}
#ifndef __FreeBSD__
	if (lps->kpad.fdin != -1 && ioctl(lps->kpad.fdin, EVIOCGRAB, capture))
    		perror("Capture kbd input:");
	if (lps->fw.fdin != -1 && ioctl(lps->fw.fdin, EVIOCGRAB, capture))
    		perror("Capture fw input:");
	if (lps->vol.fdin != -1 && ioctl(lps->vol.fdin, EVIOCGRAB, capture))
    		perror("Capture k3_vol input:");
#endif
}

/* process a hotkey sequence if found */
static void call_hotkey(int mode)
{
	const struct entry *act ;
/* DS 2011-03-09
	int i ;
*/

	DBG(1, "%s\n", mode ? "direct" : "timeout");
	timerclear(&lps->hotkey_due);
	if (!lps->hotkey_mode)
		return;
	capture_input(0) ;
	lps->hotkey_mode = 0;
	if (lps->hot_seq_len == 0)
		return;

	ds_reset(lps->pending);
	act = find_action(lps->hot_seq, lps->hot_seq_len) ;
	if (act) {
		DBG(1, "found hotkey sequence for %s\n", act->value) ;
		execute_action(act);
	} else {
		DBG(0, "Unknown hotkey sequence entered len %d\n",
			lps->hot_seq_len) ;
		/* must send the kindle all keys we stole. */
/* DS 2011-03-09
		for (i = 0; i < lps->hot_seq_len; i++) {
			send_event(lps->hot_seq[i], lps->hot_seq_dev[i]);
		}
*/
	}
	lps->hot_seq_len = 0 ;
}

static void curterm_end(void)
{
	int l = ds_len(lps->save_pixmap);

	DBG(0, "exit from terminal mode\n");
	if (l && lps->fb) {
		pixmap_t *p = &lps->fb->pixmap;
		memcpy(p->surface, ds_data(lps->save_pixmap), l);
		ds_reset(lps->save_pixmap);
		fb_update_area(lps->fb, UMODE_PARTIAL, 0, 0, p->width, p->height, NULL);
	}
	fb_close(lps->fb);
	lps->curterm = NULL;
	lps->fb = NULL;
	capture_input(0);
}

/*
 * pass keys to the terminal code, conversion etc. will happen there
 */
static void process_term(struct input_event *ev, int mode)
{
	char k[16];
	struct key_entry *e = lps->by_code[ev->code];
	/* simulate ctrl, shift, sym keys */
	static int ctrl = 0;
	static int shift = 0;
	static int sym = 0;
	static int fn = 0;

	DBG(1, "process event %d %d e %p %.*s for terminal\n",
		ev->value, ev->code, e,
		e ? e->namelen : 3, e ? e->name : "---");
	if (e == NULL)	/* unknown event */
		return;
	memset(k, 0, sizeof(k));
	if (ev->value == 1 || ev->value == 2) { /* press */
#define E_IS(e, s) ((e)->namelen == strlen(s) && !strncasecmp((e)->name, s, (e)->namelen))
		if (ev->code == lps->term_end) // ignore here, handle on release
			return;
		if (ev->code == lps->term_shift)
			shift = 1;
		else if (ev->code == lps->term_ctrl)
			ctrl = 1;
		else if (ev->code == lps->term_sym)
			sym = 1;
		else if (ev->code == lps->term_fn)
			fn = 1;
		else if (ev->code == lps->term_esc)
			k[0] = 0x1b;	/* escape */
		else if (fn) {
			/* map chars into escape sequences */
			const char *fnk[] = {
				"q\e[[A",  // F1
				"w\e[[B",  // F2
				"e\e[[C",  // F3
				"r\e[[D",  // F4
				"t\e[[E",  // F5
				"y\e[17~", // F6
				"u\e[18~", // F7
				"i\e[19~", // F8
				"o\e[20~", // F9
				"p\e[21~", // F10
				"l\e[23~", // F11
				"D\e[24~", // F12
				NULL };
			int i;
			char c = ' ';
			if (e->namelen == 1)
				c = e->name[0];
			else if (E_IS(e, "Del"))
				c = 'D';
			for (i = 0; fnk[i]; i++) {
				if (fnk[i][0] == c) {
					strcpy(k, fnk[i]+1);
					break;
				}
			}
			DBG(0, "function %s\n", k);
		} else if (sym) {
			/* translate. The first row in the table contains
			 * the base characters, and the other
			 * two are the mappings with SYM and SYM-SHIFT
			 */
			const char *t[] = { "qazuiopklD.SE",
				"`\t<-=[];'\\,./", "~\t>_+{}:\"|<>?" };
			char *p;
			if (e->namelen == 1)
				k[0] = e->name[0];
			else if (E_IS(e, "Del"))
				k[0] = 'D';
			else if (E_IS(e, "Sym"))
				k[0] = 'S';
			else if (E_IS(e, "Enter"))
				k[0] = 'E';
			p = index(t[0], k[0]);
			if (p == NULL)
				return; /* invalid */
			k[0] = t[shift+1][p - t[0]];
			if (k[0] == '\t' && shift)
				strcpy(k, "\e[Z"); // backtab
		} else if (e->namelen == 1) {
			k[0] = e->name[0];
			if (isalpha(k[0])) {
				if (shift) // shift overrides control
					k[0] += 'A' - 'a';
				else if (ctrl)
					k[0] += 1 - 'a';
			} else if (isdigit(k[0])) {
				if (shift) // shift overrides control
					k[0] = ")!@#$%^&*("[k[0] - '0'];
				else if (ctrl)
					k[0] += 1 - 'a';
			}
		} else if (E_IS(e, "Enter"))
			k[0] = 13;
		else if (E_IS(e, "Space"))
			k[0] = ' ';
		else if (E_IS(e, "Del"))
			k[0] = 0x7f;
		else if (E_IS(e, "Up"))	/* PgUp if shift pressed */
			strcpy(k, shift ? "\e[5~" : "\e[A");
		else if (E_IS(e, "Down")) /* PgDown if shift pressed */
			strcpy(k, shift ? "\e[6~" : "\e[B");
		else if (E_IS(e, "Right"))
			strcpy(k, "\e[C");
		else if (E_IS(e, "Left"))
			strcpy(k, "\e[D");
	} else if (ev->value == 0) { /* release */
		if (ev->code == lps->term_end) {
			curterm_end();
			return;
		}
		if (ev->code == lps->term_shift)
			shift = 0;
		else if (ev->code == lps->term_ctrl)
			ctrl = 0;
		else if (ev->code == lps->term_sym)
			sym = 0;
		else if (ev->code == lps->term_fn)
			fn = 0;
	}
	if (lps->curterm)
		term_keyin(lps->curterm->the_shell, k);
}


/*
 * print a buffer at x, y. If attr, use attributes array
 * Wrap after 'cols'.
 */
static void print_buf(int x0, int y0, int cols, int cur,
	const uint8_t *buf, int len, const uint8_t *attr, int bg0)
{
        int i, x = x0, y = y0;
        pixmap_t char_pixmap;

        for (i=0; i < len; i++) {
            unsigned char cc = buf[i];
            unsigned char bg = attr ? attr[i] : (bg0 << 2);
            bg = (bg & 0x38) >> 2; /* background color */

            bg = bg | (bg << 4);
	    if ( i == cur )
		bg |= 0x88;
	    get_char_pixmap(lps->fb->font, cc, &char_pixmap) ;
	    pix_blt(&lps->fb->pixmap, x, y,
			&char_pixmap, 0, 0, -1, -1, bg) ;
	    x += char_pixmap.width;
	    if ( (i+1) % cols == 0) {
		x = x0;
		y += char_pixmap.height;
	    }
        }
	fb_update_area(lps->fb, UMODE_PARTIAL, x0, y0,
		cols*(char_pixmap.width), y - y0, NULL) ;
	DBG(2, "end\n");
}

/*
 * static void print_help(void)
 *{
 * const uint8_t help[] = "\
 * === keyboard mapping =================================\
 *  !~   @    #    $    %    ^    &_   *+   ({   )}    Fn \
 *  1`   2    3    4    5    6    7-   8=   9[   0]       \
 *                                                        \
 *  <--                                 :    \"     |      \
 *  A->                                K;   L'  del\\      \
 *                                                        \
 *   >                                  <    >    ?       \
 *  Z<                                 .,  Sy.  En/       \
 *                                                        \
 *                                    Ctrl           Sym  ";
 *
 *    int l = sizeof(help);
 *    print_buf(20, 420, l/11, -1, help, l, NULL, 0x8);
 *}
 */
	
/*
 * update the screen. We know the state is 'modified' so we
 * don't need to read it, just notify it and fetch data.
 * XXX to optimize the code we could store a copy of the prev
 * window so we only update smaller regions of the screen.
 */
void process_screen(void)
{
	struct term_state st = { .flags = TS_MOD, .modified = 0};
	uint8_t *d;

#define XOFS 0	/* horizontal offset */
#define YOFS 0	/* vertical offset */

	timerclear(&lps->screen_due);
	if (!lps->curterm || !lps->fb)
		return;
	term_state(lps->curterm->the_shell, &st);
	d = (unsigned char *)st.data;

	print_buf(XOFS, YOFS, st.cols, st.cur, d, st.rows * st.cols,
		d + st.rows * st.cols, 0);
}
/*
 * Process an input event from the kindle. 'mode' is the source
 */
static void process_event(struct input_event *ev, int mode)
{
	static int npressed = 0 ;	/* keys currently pressed */
	static int got_intro = 0 ;	/* we saw the introducer */

	if (ev == NULL) { /* SYNC */
		npressed = 0;
		lps->hot_seq_len = 0 ;
		got_intro = 0 ;
		return;
	}

	if (ev->type != EV_KEY)
		return;
	DBG(2, "event ty %d val %d code %d got_intro %d npress %d seqlen %d\n",
		ev->type, ev->value, ev->code,
		got_intro, npressed, lps->hot_seq_len);
	/* ignore autorepeat events, ev->value == 2. */
	if (lps->fb) {
		process_term(ev, mode);
		return;
	}
	if (ev->value == 1) {	/* key pressed */
		npressed += 1 ;
		if (lps->hotkey_mode == 0) {
			got_intro = (npressed == 1 && ev->code == lps->intro);
		} else {
			if (ev->code == lps->trailer) { /* sequence complete, try it */
				call_hotkey(1);
			} else {
				int i = lps->hot_seq_len++;
				lps->hot_seq_dev[i] = mode ;
				lps->hot_seq[i] = ev->code ;
				if (i == MAXSEQ - 1)
					call_hotkey(1);
			}
		}
	} else if (ev->value == 0) {	/* key released */
		if (npressed > 0)	/* don't go out of sync */
			npressed --;
		if (npressed == 0 && ev->code == lps->intro && got_intro) {
			got_intro = 0;
			capture_input(1) ;	/* block input to the kindle */
			lps->hotkey_mode = 1;
			DBG(1, "-- start hotkey mode\n");
		}
	}
}

static void hup_handler(int x)
{
	lps->got_signal = 1 ; /* reinit */
}

static void int_handler(int x)
{
	lps->got_signal = 2 ; /* exit */
}

static void fd_close(int *fd)
{
	if (*fd == -1)
		return;
	close(*fd);
	*fd = -1;
}

static void free_terminals(void)
{
	struct terminal *t;

	curterm_end();

	while ( (t = lps->allterm) ) {
		lps->allterm = t->next;
		term_kill(t->the_shell, 9);
		free(t);
	}
}

void term_dead(struct sess *s)
{
	struct terminal **t, *cur;
	for (t = &lps->allterm; (cur = *t); t = &(*t)->next) {
		if (cur->the_shell != s)
			continue;
		DBG(0, "terminal %s is dead\n", cur->name);
		*t = cur->next;
		if (lps->curterm == cur)
			curterm_end();
		memset(cur, 0, sizeof(*cur));
		free(cur);
		return;
	}
	DBG(0, "could not find dead terminal %p\n", s);
}

/*
 * find or create a shell with the given name. The name string
 * is copied in the descriptor so it can be preserved on reboots.
 */
struct terminal *shell_find(const char *name)
{
	struct terminal *t;
	int l = strlen(name) + 1;

	for (t = lps->allterm; t; t = t->next) {
		if (!strcmp(name, t->name))
			return t;
	}
	t = calloc(1, sizeof(*t) + l);
	if (!t) {
		DBG(0, "could not allocate session for %s\n", name);
		return t;
	}
	strcpy(t->name, name);
	t->the_shell = term_new("/bin/sh", t->name, 50, 80, term_dead);
	if (!t->the_shell) {
		free(t);
		return NULL;
	}
	t->next = lps->allterm;
	lps->allterm  = t;
	return t;
}


/*
 * prepare for a restart or for final exiting
 */
static void launchpad_deinit(int restart)
{
	DBG(0, "called, restart %d\n", restart);
	lps->got_signal = 0 ;
	// XXX should remove the pending sessions from the scheduler ?
	curterm_end();

	lps->pending = ds_free(lps->pending);
	signal(SIGINT, SIG_DFL) ;
	signal(SIGTERM, SIG_DFL) ;
	signal(SIGHUP, SIG_DFL) ;

	if (!restart)
		free_terminals();
	lps->hotkey_mode = 0;
	fd_close(&lps->kpad.fdin);
	fd_close(&lps->fw.fdin);
	fd_close(&lps->vol.fdin);
	fd_close(&lps->kpad.fdout);
	fd_close(&lps->fw.fdout);
	fd_close(&lps->vol.fdout);
	if (lps->db) {
		cfg_free(lps->db) ;
		lps->db = NULL ;
	}
}

int launchpad_start(void);

/*
 * callback for select.
 * We have only one session so ignore _s
 */
int handle_launchpad(void *_s, struct cb_args *a)
{
	int fds[3] = { lps->kpad.fdin, lps->fw.fdin, lps->vol.fdin };
	int i, j, ev;

	DBG(2, "fds %d %d %d\n", lps->kpad.fdin, lps->fw.fdin, lps->vol.fdin);
	DBG(2, "term %p sh %p hotkey_due %d pend %d\n",
		lps->fb, lps->curterm,
		(int)lps->hotkey_due.tv_usec,
		ds_len(lps->pending) );
	if (lps->kpad.fdin < 0) { /* dead */
		if (a->run == 0)
			return 0;
		/* try to restart or terminate ? */
		launchpad_deinit(0);
		return 1;
	}
	if (a->run == 0) {
		/* create screen refresh timeout if needed */
		if (lps->fb && lps->curterm &&
			    term_state(lps->curterm->the_shell, NULL) &&
			    !timerisset(&lps->screen_due))
			timeradd_ms(&a->now, lps->refresh_delay, &lps->screen_due);
		/* Record pending timeouts */
		timersetmin(&a->due, &lps->screen_due);
		timersetmin(&a->due, &lps->hotkey_due);
		timersetmin(&a->due, &lps->keys_due);
		/* if we have keys to send, ignore input events */
		if (ds_len(lps->pending) > 0)
			return 0;
		for (i=0; i < 3; i++) {
			if (fds[i] >= 0)
				FD_SET(fds[i], a->r);
			if (fds[i] > a->maxfd)
				a->maxfd = fds[i];
		}
		return 1;
	}

	if (timerdue(&lps->hotkey_due, &a->now))
		call_hotkey(0);
	if (lps->got_signal == 1) {
		launchpad_deinit(1);
		launchpad_start();
		return 0;
	}
	if (lps->got_signal == 2) {
		launchpad_deinit(0);
		return 0;
	}
	ev = 0;
	if (ds_len(lps->pending) == 0) {
		struct input_event kbbuf[2];
		for (j = 0; j < sizeof(fds) / sizeof(fds[0]) ; j++) {
			int l = sizeof(struct input_event);
			int n;
			if (fds[j] < 0 || !FD_ISSET(fds[j], a->r))
				continue;
			ev = 1;	/* got an event */
			DBG(1, "reading on %d\n", fds[j]);
#ifdef __FreeBSD__
			n = host_event(fds[j], kbbuf, l);
#else
			n = read(fds[j], kbbuf, l* 2) ;
#endif
			DBG(2, "got %d bytes from %d\n", n, fds[j]);
			for (i = 0; i < 2 && n >= l; i++, n -= l) {
				process_event(kbbuf + i, j) ;
			}
		}
	}
	/* test pending again, could have been set above */
	if (ds_len(lps->pending) > 0) {
		if (timerdue(&lps->keys_due, &a->now)) {
			uint8_t *p = (uint8_t *)ds_data(lps->pending);
			struct timeval k = { 0, lps->key_delay * 1000 };
			send_event1(p[0], p[1]);
			ds_shift(lps->pending, 2);
			if (ds_len(lps->pending) > 0)
				timeradd(&a->now, &k, &lps->keys_due);
			else
				timerclear(&lps->keys_due);
			DBG(1, "sending key, left %d\n", ds_len(lps->pending)/2);
		}
		return 0;
	}
	if (timerdue(&lps->screen_due, &a->now)) {
		process_screen();
		return 0;
	}
	if (ev == 0) { /* timeout ? resync ? */
		process_event(NULL, 0);
	}
	return 0;
}

int launchpad_parse(int *ac, char *av[])
{
	int  i ;
	lps = __me.app->data;

	DBG(0, "Launchpad start routine\n");
	memset(lps, 0, sizeof(*lps));

	/* copy the full config name into lps->basedir */
	i = sizeof(lps->basedir) - strlen(".ini") - 1;
	if (readlink ("/proc/self/exe", lps->basedir, i) == -1)
		strncpy(lps->basedir, av[0], i) ;
	strcat(lps->basedir, ".ini");

	/* dirname() may or may not truncates the argument, so we
	 * enforce the truncation and append a '/'
	 */
	i = strlen( dirname(lps->basedir) );
	lps->basedir[i] = '\0';
	lps->cfg_name = lps->basedir + i + 1;
	lps->cfg_name = "launchpad.ini"; // XXX
	DBG(1, "inipath is %s ini_name %s\n",
		lps->basedir, lps->cfg_name);
	if (*ac > 2 && !strcmp(av[1], "--cfg"))
		lps->cfg_name = av[2];

	return 0;
}

int launchpad_start(void)
{
	new_sess(0, -2, handle_launchpad, NULL);
	signal(SIGINT, int_handler);
	signal(SIGTERM, int_handler);
	signal(SIGHUP, hup_handler);
	process_event(NULL, 0);	/* reset args */
	if (!launchpad_init(NULL))
		return 0;
	DBG(0, "init routine failed, exiting\n");
	launchpad_deinit(0) ;
	return 0 ;
}

struct app lpad = { .parse = launchpad_parse,
	.start = launchpad_start, .data = &lpad_desc};
