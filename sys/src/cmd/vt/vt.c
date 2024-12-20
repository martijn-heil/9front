/*
 * Known bugs:
 *
 * 1. We don't handle cursor movement characters inside escape sequences.
 * 	That is, ESC[2C moves two to the right, so ESC[2\bC is supposed to back
 *	up one and then move two to the right.
 *
 * 2. We don't handle tabstops past nelem(tabcol) columns.
 *
 * 3. We don't respect requests to do reverse video for the whole screen.
 *
 * 4. We ignore the ESC#n codes, so that we don't do double-width nor 
 * 	double-height lines, nor the ``fill the screen with E's'' confidence check.
 *
 * 5. Cursor key sequences aren't selected by keypad application mode.
 *
 * 6. "VT220" mode (-2) currently just switches the default cursor key
 *	functions (same as -a); it's still just a VT100 emulation.
 *
 * 7. VT52 mode and a few other rarely used features are not implemented.
 */

#include <u.h>
#include <libc.h>
#include <draw.h>

#include "cons.h"

#include <ctype.h>

int	wraparound = 1;
int	originrelative = 0;
int	bracketed = 0;

int	tabcol[200];
char osc7cwd[WDIR];

struct funckey ansifk[] = {
	{ "up key",		"\033[A", },
	{ "down key",		"\033[B", },
	{ "left key",		"\033[D", },
	{ "right key",		"\033[C", },
	{ "F1",			"\033OP", },
	{ "F2",			"\033OQ", },
	{ "F3",			"\033OR", },
	{ "F4",			"\033OS", },
	{ "F5",			"\033OT", },
	{ "F6",			"\033OU", },
	{ "F7",			"\033OV", },
	{ "F8",			"\033OW", },
	{ "F9",			"\033OX", },
	{ "F10",		"\033OY", },
	{ "F11",		"\033OZ", },
	{ "F12",		"\033O1", },
	{ 0 },
};

struct funckey ansiappfk[] = {
	{ "up key",		"\033OA", },
	{ "down key",		"\033OB", },
	{ "left key",		"\033OD", },
	{ "right key",		"\033OC", },
	
	{ 0 },
};

struct funckey vt220fk[] = {
	{ "insert",		"\033[2~", },
	{ "delete",		"\033[3~", },
	{ "home",		"\033[1~", },
	{ "end",		"\033[4~", },
	{ "page up",		"\033[5~", },
	{ "page down",		"\033[6~", },

	{ "up key",		"\033[A", },
	{ "down key",		"\033[B", },
	{ "left key",		"\033[D", },
	{ "right key",		"\033[C", },

	{ "F1",			"\033OP", },
	{ "F2",			"\033OQ", },
	{ "F3",			"\033OR", },
	{ "F4",			"\033OS", },
	{ "F5",			"\033[15~", },
	{ "F6",			"\033[17~", },
	{ "F7",			"\033[18~", },
	{ "F8",			"\033[19~", },
	{ "F9",			"\033[20~", },
	{ "F10",		"\033[21~", },
	{ "F11",		"\033[23~", },
	{ "F12",		"\033[24~", },

	{ 0 },
};

struct funckey xtermfk[] = {
	{ "insert",		"\033[2~", },
	{ "delete",		"\033[3~", },
	{ "home",		"\033OH", },
	{ "end",		"\033OF", },
	{ "page up",		"\033[5~", },
	{ "page down",		"\033[6~", },

	{ "up key",		"\033[A", },
	{ "down key",		"\033[B", },
	{ "left key",		"\033[D", },
	{ "right key",		"\033[C", },

	{ "F1",			"\033OP", },
	{ "F2",			"\033OQ", },
	{ "F3",			"\033OR", },
	{ "F4",			"\033OS", },
	{ "F5",			"\033[15~", },
	{ "F6",			"\033[17~", },
	{ "F7",			"\033[18~", },
	{ "F8",			"\033[19~", },
	{ "F9",			"\033[20~", },
	{ "F10",		"\033[21~", },
	{ "F11",		"\033[23~", },
	{ "F12",		"\033[24~", },

	{ 0 },
};

char gmap[256] = {
	['_']	' ',	/* blank */
	['\\']	'*',	/* diamond */
	['a']	'X',	/* checkerboard */
	['b']	'\t',	/* HT */
	['c']	'\x0C',	/* FF */
	['d']	'\r',	/* CR */
	['e']	'\n',	/* LF */
	['f']	'o',	/* degree */
	['g']	'+',	/* plus/minus */
	['h']	'\n',	/* NL, but close enough */
	['i']	'\v',	/* VT */
	['j']	'+',	/* lower right corner */
	['k']	'+',	/* upper right corner */
	['l']	'+',	/* upper left corner */
	['m']	'+',	/* lower left corner */
	['n']	'+',	/* crossing lines */
	['o']	'-',	/* horiz line - scan 1 */
	['p']	'-',	/* horiz line - scan 3 */
	['q']	'-',	/* horiz line - scan 5 */
	['r']	'-',	/* horiz line - scan 7 */
	['s']	'-',	/* horiz line - scan 9 */
	['t']	'+',	/* |-   */
	['u']	'+',	/* -| */
	['v']	'+',	/* upside down T */
	['w']	'+',	/* rightside up T */
	['x']	'|',	/* vertical bar */
	['y']	'<',	/* less/equal */
	['z']	'>',	/* gtr/equal */
	['{']	'p',	/* pi */
	['|']	'!',	/* not equal */
	['}']	'L',	/* pound symbol */
	['~']	'.',	/* centered dot: · */
};

static void setattr(int argc, int *argv);
static void osc(void);

void
fixops(int *operand)
{
	if(operand[0] < 1)
		operand[0] = 1;
}

void
emulate(void)
{
	Rune buf[BUFS+1];
	int i;
	int n;
	int c;
	int operand[10];
	int noperand;
	int savex, savey, saveattr, saveisgraphics;
	int isgraphics;
	int g0set, g1set;

	isgraphics = 0;
	g0set = 'B';	/* US ASCII */
	g1set = 'B';	/* US ASCII */
	savex = savey = 0;
	yscrmin = 0;
	yscrmax = ymax;
	saveattr = 0;
	saveisgraphics = 0;
	/* set initial tab stops to DEC-standard 8-column spacing */
	for(c=0; (c+=8)<nelem(tabcol);)
		tabcol[c] = 1;

	for (;;) {
		if (y > ymax) {
			x = 0;
			newline();
		}
		buf[0] = get_next_char();
		buf[1] = '\0';
		switch(buf[0]) {

		case '\000':
		case '\001':
		case '\002':
		case '\003':
		case '\004':
		case '\005':
		case '\006':
			goto Default;

		case '\007':		/* bell */
			ringbell();
			break;

		case '\010':		/* backspace */
			if (x > 0)
				--x;
			break;

		case '\011':		/* tab to next tab stop; if none, to right margin */
			for(c=x+1; c<nelem(tabcol) && !tabcol[c]; c++)
				;
			if(c < nelem(tabcol))
				x = c;
			else
				x = xmax;
			break;

		case '\012':		/* linefeed */
		case '\013':
		case '\014':
			newline();
			if (ttystate[cs->raw].nlcr)
				x = 0;
			break;

		case '\015':		/* carriage return */
			x = 0;
			if (ttystate[cs->raw].crnl)
				newline();
			break;

		case '\016':	/* SO: invoke G1 char set */
			isgraphics = (isdigit(g1set));
			break;
		case '\017':	/* SI: invoke G0 char set */
			isgraphics = (isdigit(g0set));
			break;

		case '\020':	/* DLE */
		case '\021':	/* DC1 */
		case '\022':	/* XON */
		case '\023':	/* DC3 */
		case '\024':	/* XOFF */
		case '\025':	/* NAK */
		case '\026':	/* SYN */
		case '\027':	/* ETB */
		case '\030':	/* CAN: cancel escape sequence, display checkerboard (not implemented) */
		case '\031':	/* EM */
		case '\032':	/* SUB: same as CAN */
			goto Default;
;
		/* ESC, \033, is handled below */
		case '\034':	/* FS */
		case '\035':	/* GS */
		case '\036':	/* RS */
		case '\037':	/* US */
			break;
		case '\177':	/* delete: ignored */
			break;

		case '\033':
			switch(get_next_char()){
			/*
			 * 1 - graphic processor option on (no-op; not installed)
			 */
			case '1':
				break;

			/*
			 * 2 - graphic processor option off (no-op; not installed)
			 */
			case '2':
				break;

			/*
			 * 7 - save cursor position.
			 */
			case '7':
				savex = x;
				savey = y;
				saveattr = attr;
				saveisgraphics = isgraphics;
				break;

			/*
			 * 8 - restore cursor position.
			 */
			case '8':
				x = savex;
				y = savey;
				attr = saveattr;
				isgraphics = saveisgraphics;
				break;

			/*
			 * c - Reset terminal.
			 */
			case 'c':
				cursoron = 1;
				ttystate[cs->raw].nlcr = 0;
				break;

			/*
			 * D - active position down a line, scroll if at bottom margin.
			 * (Original VT100 had a bug: tracked new-line/line-feed mode.)
			 */
			case 'D':
				if(++y > yscrmax) {
					y = yscrmax;
					scroll(yscrmin+1, yscrmax+1, yscrmin, yscrmax);
				}
				break;

			/*
			 * E - active position to start of next line, scroll if at bottom margin.
			 */
			case 'E':
				x = 0;
				if(++y > yscrmax) {
					y = yscrmax;
					scroll(yscrmin+1, yscrmax+1, yscrmin, yscrmax);
				}
				break;

			/*
			 * H - set tab stop at current column.
			 * (This is cursor home in VT52 mode (not implemented).)
			 */
			case 'H':
				if(x < nelem(tabcol))
					tabcol[x] = 1;
				break;

			/*
			 * M - active position up a line, scroll if at top margin..
			 */
			case 'M':
				if(--y < yscrmin) {
					y = yscrmin;
					scroll(yscrmin, yscrmax, yscrmin+1, yscrmin);
				}
				break;

			/*
			 * Z - identification.  the terminal
			 * emulator will return the response
			 * code for a generic VT100.
			 */
			case 'Z':
			Ident:
				sendnchars(7, "\033[?1;2c");	/* VT100 with AVO option */
				break;

			/*
			 * < - enter ANSI mode
			 */
			case '<':
				break;

			/*
			 * > - set numeric keypad mode on (not implemented)
			 */
			case '>':
				break;

			/*
			 * = - set numeric keypad mode off (not implemented)
			 */
			case '=':
				break;

			/*
			 * # - Takes a one-digit argument
			 */
			case '#':
				switch(get_next_char()){
				case '3':		/* Top half of double-height line */
				case '4':		/* Bottom half of double-height line */
				case '5':		/* Single-width single-height line */
				case '6':		/* Double-width line */
				case '7':		/* Screen print */
				case '8':		/* Fill screen with E's */
					break;
				}
				break;

			/*
			 * ( - switch G0 character set
			 */
			case '(':
				g0set = get_next_char();
				break;

			/*
			 * - switch G1 character set
			 */
			case ')':
				g1set = get_next_char();
				break;

			/*
			 * Received left bracket.
			 */
			case '[':
				/*
				 * A semi-colon or ? delimits arguments.
				 */
				memset(operand, 0, sizeof(operand));
				operand[0] = number(buf, &i);
				noperand = 1;
				while(buf[0] == ';' || buf[0] == '?'){
					if(noperand < nelem(operand))
						operand[noperand++] = number(buf, nil);
					else
						number(buf, nil);
				}

				/*
				 * do escape2 stuff
				 */
				switch(buf[0]){
					/*
					 * c - same as ESC Z: what are you?
					 */
					case 'c':
						goto Ident;

					/*
					 * g - various tabstop manipulation
					 */
					case 'g':
						switch(operand[0]){
						case 0:	/* clear tab at current column */
							if(x < nelem(tabcol))
								tabcol[x] = 0;
							break;
						case 3:	/* clear all tabs */
							memset(tabcol, 0, sizeof tabcol);
							break;
						}
						break;

					/*
					 * l - clear various options.
					 */
					case 'l':
						if(noperand == 1){
							switch(operand[0]){	
							case 20:	/* set line feed mode */
								ttystate[cs->raw].nlcr = 1;
								break;
							case 30:	/* screen invisible (? not supported through VT220) */
								break;
							}
						}else while(--noperand > 0){
							switch(operand[noperand]){
							case 1:	/* set cursor keys to send ANSI functions: ESC [ A..D */
								appfk = nil;
								break;
							case 2:	/* set VT52 mode (not implemented) */
								break;
							case 3:	/* set 80 columns */
								setdim(-1, 80);
								break;
							case 4:	/* set jump scrolling */
								break;
							case 5:	/* set normal video on screen */
								break;
							case 6:	/* set origin to absolute */
								originrelative = 0;
								x = y = 0;
								break;
							case 7:	/* reset auto-wrap mode */
								wraparound = 0;
								break;
							case 8:	/* reset auto-repeat mode */
								break;
							case 9:	/* reset interlacing mode */
								break;
							case 25:	/* text cursor off (VT220) */
								cursoron = 0;
								break;
							case 2004:	/* bracketed paste mode off */
								bracketed = 0;
								break;
							}
						}
						break;

					/*
					* s - some dec private stuff. actually [ ? num s, but we can't detect it.
					*/
					case 's':
						break;

					/*
					 * h - set various options.
					 */
					case 'h':
						if(noperand == 1){
							switch(operand[0]){
							default:
								break;
							case 20:	/* set newline mode */
								ttystate[cs->raw].nlcr = 0;
								break;
							case 30:	/* screen visible (? not supported through VT220) */
								break;
							}
						}else while(--noperand > 0){
							switch(operand[noperand]){
							default:
								break;
							case 1:	/* set cursor keys to send application function: ESC O A..D */
								appfk = ansiappfk;
								break;
							case 2:	/* set ANSI */
								break;
							case 3:	/* set 132 columns */
								setdim(-1, 132);
								break;
							case 4:	/* set smooth scrolling */
								break;
							case 5:	/* set screen to reverse video (not implemented) */
								break;
							case 6:	/* set origin to relative */
								originrelative = 1;
								x = 0;
								y = yscrmin;
								break;
							case 7:	/* set auto-wrap mode */
								wraparound = 1;
								break;
							case 8:	/* set auto-repeat mode */
								break;
							case 9:	/* set interlacing mode */
								break;
							case 25:	/* text cursor on (VT220) */
								cursoron = 1;
								break;
							case 2004:	/* bracketed paste mode on */
								bracketed = 1;
								break;
							}
						}
						break;

					/*
					 * m - change character attrs.
					 */
					case 'm':
						setattr(noperand, operand);
						break;

					/*
					 * n - request various reports
					 */
					case 'n':
						switch(operand[0]){
						case 5:	/* status */
							sendnchars(4, "\033[0n");	/* terminal ok */
							break;
						case 6:	/* cursor position */
							sendnchars(sprint((char*)buf, "\033[%d;%dR",
								originrelative ? y+1 - yscrmin : y+1, x+1), (char*)buf);
							break;
						}
						break;

					/*
					 * q - turn on list of LEDs; turn off others.
					 */
					case 'q':
						break;

					/*
					 * r - change scrolling region.  operand[0] is
					 * min scrolling region and operand[1] is max
					 * scrolling region.
					 */
					case 'r':
						yscrmin = 0;
						yscrmax = ymax;
						switch(noperand){
						case 2:
							yscrmax = operand[1]-1;
							if(yscrmax > ymax)
								yscrmax = ymax;
						case 1:
							yscrmin = operand[0]-1;
							if(yscrmin < 0)
								yscrmin = 0;
						}
						x = 0;
						y = yscrmin;
						break;

					/*
					 * x - report terminal parameters
					 */
					case 'x':
						sendnchars(20, "\033[3;1;1;120;120;1;0x");
						break;

					/*
					 * y - invoke confidence test
					 */
					case 'y':
						break;

					/*
					 * z - line spacing
					 */
					case 'z':
						break;

					/*
					 * A - cursor up.
					 */
					case 'e':
					case 'A':
						fixops(operand);
						y -= operand[0];
						if(y < yscrmin)
							y = yscrmin;
						olines -= operand[0];
						if(olines < 0)
							olines = 0;
						break;

					/*
					 * B - cursor down
					 */
					case 'B':
						fixops(operand);
						y += operand[0];
						if(y > yscrmax)
							y=yscrmax;
						break;
					
					/*
					 * C - cursor right
					 */
					case 'a':
					case 'C':
						fixops(operand);
						x += operand[0];
						/*
						 * VT-100-UG says not to go past the
						 * right margin.
						 */
						if(x > xmax)
							x = xmax;
						break;

					/*
					 * D - cursor left
					 */
					case 'D':
						fixops(operand);
						x -= operand[0];
						if(x < 0)
							x = 0;
						break;

					/*
					 *	G - cursor to column
					 */
					case '\'':
					case 'G':
						fixops(operand);
						x = operand[0] - 1;
						if(x > xmax)
							x = xmax;
						break;

					/*
					 * H and f - cursor motion.  operand[0] is row and
					 * operand[1] is column, origin 1.
					 */
					case 'H':
					case 'f':
						fixops(operand+1);
						x = operand[1] - 1;
						if(x > xmax)
							x = xmax;

						/* fallthrough */

					/*
					 * d - cursor to line n (xterm)
					 */
					case 'd':
						fixops(operand);
						y = operand[0] - 1;
						if(originrelative){
							y += yscrmin;
							if(y > yscrmax)
								y = yscrmax;
						}else{
							if(y > ymax)
								y = ymax;
						}
						break;

					/*
					 * J - clear some or all of the display.
					 */
					case 'J':
						switch (operand[0]) {
							/*
							 * operand 2:  whole screen.
							 */
							case 2:
								clear(0, 0, xmax+1, ymax+1);
								break;
							/*
							 * operand 1: start of screen to active position, inclusive.
							 */
							case 1:
								clear(0, 0, xmax+1, y);
								clear(0, y, x+1, y+1);
								break;
							/*
							 * Default:  active position to end of screen, inclusive.
							 */
							default:
								clear(x, y, xmax+1, y+1);
								clear(0, y+1, xmax+1, ymax+1);
								break;
						}
						break;

					/*
					 * K - clear some or all of the line.
					 */
					case 'K':
						switch (operand[0]) {
							/*
							 * operand 2: whole line.
							 */
							case 2:
								clear(0, y, xmax+1, y+1);
								break;
							/*
							 * operand 1: start of line to active position, inclusive.
							 */
							case 1:
								clear(0, y, x+1, y+1);
								break;
							/*
							 * Default: active position to end of line, inclusive.
							 */
							default:
								clear(x, y, xmax+1, y+1);
								break;
						}
						break;

					/*
					 *	P - delete character(s) from right of cursor (xterm)
					 */
					case 'P':
						fixops(operand);
						i = x + operand[0];
						shift(x, y, i, xmax+1 - i);
						clear(xmax-operand[0], y, xmax+1, y+1);
						break;

					/*
					 *	@ - insert blank(s) to right of cursor (xterm)
					 */
					case '@':
						fixops(operand);
						i = x + operand[0];
						shift(i, y, x, xmax+1 - i);
						clear(x, y, i, y+1);
						break;


					/*
					 *	X - erase character(s) at cursor and to the right (xterm)
					 */
					case 'X':
						fixops(operand);
						i = x + operand[0];
						clear(x, y, i, y+1);
						break;

					/*
					 * L - insert a line at cursor position (VT102 and later)
					 */
					case 'L':
						fixops(operand);
						for(i = 0; i < operand[0]; ++i)
							scroll(y, yscrmax, y+1, y);
						break;

					/*
					 * M - delete a line at cursor position (VT102 and later)
					 */
					case 'M':
						fixops(operand);
						for(i = 0; i < operand[0]; ++i)
							scroll(y+1, yscrmax+1, y, yscrmax);
						break;

					/*
					 * S,T - scroll up/down (xterm)
					 */
					case 'T':
						fixops(operand);
						for(i = 0; i < operand[0]; ++i)
							scroll(yscrmin, yscrmax, yscrmin+1, yscrmin);
						break;

					case 'S':
						fixops(operand);
						for(i = 0; i < operand[0]; ++i)
							scroll(yscrmin+1, yscrmax+1, yscrmin, yscrmin);
						break;

					case '=':	/* ? not supported through VT220 */
						number(buf, nil);
						switch(buf[0]) {
						case 'h':
						case 'l':
							break;
						}
						break;
					case '>':	/* Set/reset key modifier options (XTMODKEYS), xterm. */
						number(buf, nil);
						if(buf[0] != 'm' && buf[0] != 'M')
							number(buf, nil);
						break;
				}

				break;

			/*
			 * Collapse multiple '\033' to one.
			 */
			case '\033':
				peekc = '\033';
				break;

			/* OSC escape */
			case ']':
				osc();
				break;
			}
			break;

		default:		/* ordinary char */
Default:
			if(isgraphics && buf[0] < nelem(gmap) && gmap[buf[0]])
				buf[0] = gmap[buf[0]];

			/* line wrap */
			if (x > xmax){
				if(wraparound){
					newline();
					x = 0;
				}else{
					continue;
				}
			}
			n = 1;
			c = 0;
			while (!cs->raw && host_avail() && x+n<=xmax && n<BUFS
			    && (c = get_next_char())>=' ' && c<'\177') {
				buf[n++] = c;
				c = 0;
			}
			buf[n] = 0;
			drawstring(buf, n);
			x += n;
			peekc = c;
			break;
		}
	}
}

static void
setattr(int argc, int *argv)
{
	int i;

	for(i=0; i<argc; i++) {
		switch(argv[i]) {
		case 0:
			attr = defattr;
			break;
		case 1:
			attr |= THighIntensity;
			break;		
		case 4:
			attr |= TUnderline;
			break;		
		case 5:
			attr |= TBlink;
			break;
		case 7:
			attr |= TReverse;
			break;
		case 8:
			attr |= TInvisible;
			break;
		case 22:
			attr &= ~THighIntensity;
			break;		
		case 24:
			attr &= ~TUnderline;
			break;		
		case 25:
			attr &= ~TBlink;
			break;
		case 27:
			attr &= ~TReverse;
			break;
		case 28:
			attr &= ~TInvisible;
			break;
		case 30:	/* black */
		case 31:	/* red */
		case 32:	/* green */
		case 33:	/* brown */
		case 34:	/* blue */
		case 35:	/* purple */
		case 36:	/* cyan */
		case 37:	/* white */
			attr = (attr & ~0xF000) | 0x1000 | (argv[i]-30)<<13;
			break;
		case 39:	/* default */
			attr &= ~0xF000;
			break;
		case 40:	/* black */
		case 41:	/* red */
		case 42:	/* green */
		case 43:	/* brown */
		case 44:	/* blue */
		case 45:	/* purple */
		case 46:	/* cyan */
		case 47:	/* white */
			attr = (attr & ~0x0F00) | 0x0100 | (argv[i]-40)<<9;
			break;
		case 49:	/* default */
			attr &= ~0x0F00;
			break;
		}
	}
}

static int
hexnib(char c)
{
	if(c >= 'a')
		return c - 'a' + 10;
	if(c >= 'A')
		return c - 'A' + 10;
	return c - '0';
}

// handle ESC], Operating System Command
static void
osc(void)
{
	Rune ch, buf[BUFS+1];
	int fd, osc, got, i;
	char *o, *s;
	osc = number(&ch, &got);

	if(got) {
		switch(osc) {
		case 0:
		case 1:
		case 2: /* set title */
			i = 0;

			while((ch = get_next_char()) != '\a') {
				if(i < nelem(buf) - 1) {
					buf[i++] = ch;
				}
			}
			buf[i] = 0;
			if((fd = open("/dev/label", OWRITE)) >= 0) {
				fprint(fd, "%S", buf);
				close(fd);
			}
			break;

		case 7: /* set pwd */
			i = 0;

			while((ch = get_next_char()) != '\033'){
				if(i < sizeof(osc7cwd)-UTFmax-1)
					i += runetochar(osc7cwd+i, &ch);
			}
			get_next_char();
			osc7cwd[i] = 0;

			/* file://hostname/path → /n/hostname/path */
			if(strncmp(osc7cwd, "file://", 7) == 0){
				osc7cwd[0] = '/';
				osc7cwd[1] = 'n';
				o = osc7cwd+2;
				s = osc7cwd+6;
				while(*s){
					if(*s == '%' && s[1] != 0 && s[2] != 0){
						*o++ = hexnib(s[1])<<4 | hexnib(s[2]);
						s += 3;
					}else
						*o++ = *s++;
				}
				*o = 0;
			}
			break;
		}
	}
}
