/*
 * MyTerm -- Simple vt100 terminal on top of SDL
 *
 * Copyright (C) 2013 Ivo van Poorten <ivopvp@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/* Information on vt100: Digital VT100 User Guide http://www.vt100.net/ */

#include "myterm.h"

#define OUTPUT(str,len)  write(fd_parent, str, len)

static SDL_Rect srcrect = { 0, 0, CWIDTH, CHEIGHT },
                dstrect = { 0, 0, CWIDTH, CHEIGHT };

static int xpos, ypos, shift, ctrl, G0, G1, sgc, bold, inverse, wrap;
static int fd_parent, fd_child, bufsize;

static char *buf;

static SDL_Surface *display, *screen, *normal, *boldfont, *bg;

static void fatal(char *fmt, ...) {
    va_list ap;
    fprintf(stderr, "fatal: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    SDL_Quit();
    exit(1);
}

static void *xrealloc(void *ptr, size_t size) {
    void *p = realloc(ptr, size);
    if (!p) fatal("out of memory\n");
    return p;
}

static void blit_char(int c) {
    SDL_Surface *font = bold ? boldfont : normal;
    int x, y;

    x = c & 0x0f;
    y = c >> 4;

    if (inverse) y+=8;

    srcrect.x = x*CWIDTH;
    srcrect.y = y*CHEIGHT;
    dstrect.x = xpos*CWIDTH;
    dstrect.y = ypos*CHEIGHT;

    SDL_BlitSurface(font, &srcrect, screen, &dstrect);
}

static int readchar(void) {
    int numread;
    char c;

    while ((numread = read(fd_parent, &c, 1)) != 1)
        if (numread < 0) fatal("reading from pts\n");
    return c;
}

static void erase_till_bol(void) {
    SDL_Rect r = { 0, ypos*CHEIGHT, xpos*CWIDTH+CWIDTH, CHEIGHT };
    SDL_FillRect(screen, &r, 0);
}

static void erase_till_eol(void) {
    SDL_Rect r = { xpos*CWIDTH, ypos*CHEIGHT, WIDTH - xpos*CWIDTH, CHEIGHT };
    SDL_FillRect(screen, &r, 0);
}

static void erase_line(void) {
    SDL_Rect r = { 0, ypos*CHEIGHT, WIDTH, CHEIGHT };
    SDL_FillRect(screen, &r, 0);
}

static void erase_till_beg_of_screen(void) {
    erase_till_bol();
    if (ypos) {
        int savey = ypos;
        while (--ypos >= 0) erase_line();
        ypos = savey;
    }
}

static void erase_till_end_of_screen(void) {
    erase_till_eol();
    if (ypos < THEIGHT-1) {
        int savey = ypos;
        while (++ypos < THEIGHT) erase_line();
        ypos = savey;
    }
}

static void erase_screen(void) {
    SDL_Rect r = { 0, 0, WIDTH, HEIGHT };
    SDL_FillRect(screen, &r, 0);
}

static void do_esc(void) {
    int pos, v1, v2;
    char c, *p;

    c = readchar();
    switch(c) {
    case '[':   // CSI - Control Sequence Initiator 
        // read up to first char in range 0x40-0x7e, which is the command
        pos = 0;
        while (1) {
            c = readchar();
            if (c >= 0x40 && c <= 0x7e) break;
            if (c == 0030 || c == 0032) return;     // CAN and SUB
            buf[pos++] = c;
            if (pos == bufsize) {
                bufsize *= 2;
                buf = xrealloc(buf, bufsize);
            }
        }
        buf[pos] = '\0';

        // execute command

        pos = 0;
        v1 = atoi(buf);
        switch(c) {
        case 'm':               // SGR - Set Graphics Rendition
            bold = inverse = 0;
            p = strchr(buf, ';');
            while (p && *p == ';') {
                p++;
                v1 = atoi(p);
                if (v1 == 1) bold = 1;
                else if (v1 == 7) inverse = 1;
                p = strchr(p, ';');
            }
            break;

        case 'l':               // RM - Reset Mode
        case 'h':               // SM - Set Mode
            p = buf;
            while (p && *p++ == '?') {
                v1 = atoi(p);
                v2 = (c == 'l');
                if (v1 == 7) wrap = v2;
                p = strchr(p, ';');
                if (p) p++;
            }
            break;

        case 'A':   if (!v1) v1 = 1;    ypos -= v1;     break;  // CUU
        case 'B':   if (!v1) v1 = 1;    ypos += v1;     break;  // CUD
        case 'C':   if (!v1) v1 = 1;    xpos += v1;     break;  // CUF
        case 'D':   if (!v1) v1 = 1;    xpos -= v1;     break;  // CUB

        case 'K':               // EL - Erase in Line
                 if (v1 == 0) erase_till_eol();
            else if (v1 == 1) erase_till_bol();
            else              erase_line();
            break;
        case 'J':               // ED - Erase in Display
                 if (v1 == 0) erase_till_end_of_screen();
            else if (v1 == 1) erase_till_beg_of_screen();
            else              erase_screen();
            break;

        case 'H':               // CUP - Cursor Position
        case 'f':               // HVP - Horizontal and Vertical Position
            v2 = 0;
            p = strchr(buf, ';');
            if (p) {
                v2 = atoi(p+1);
            }
            if (!v1) v1 = 1;
            if (!v2) v2 = 1;
            ypos = v1-1;
            xpos = v2-1;
            break;

        default:
            fprintf(stderr, "unhandled escape: %c (%s)\n", c, buf);
            break;
        }
        break;

    case 'E':   xpos = 0;           // NEL - Next Line, fall-through to IND
    case 'D':   ypos++;     break;  // IND - Index (next line on same position)
    case 'M':   ypos--;     break;  // RI - Reverse Index

    case 'c':   // RIS - Reset to Initial State
        xpos = ypos = G0 = G1 = sgc = bold = inverse = wrap = 0;
        erase_screen();
        break;

                // SCS - Select Character Set, only ascii or sgc
    case '(':   c = readchar();     G0 = !(c=='A'||c=='B');     break;
    case ')':   c = readchar();     G1 = !(c=='A'||c=='B');     break;

    default:
        fprintf(stderr, "unhandled escape: ESC %c\n", c);
        break;
    }
}

static void output_char(int c) {
    if (c<0040 || c>0176) {
        // VT100-ug Table 3-10 Control Characters
        switch(c) {
        case 0010:                      // BS   backspace, unless xpos==0
            if (xpos) xpos--;
            break;

        case 0013:                      // VT   interpreted as LF
        case 0014:                      // FF   interpreted as LF
        case 0012:  ypos++;     break;  // LF   linefeed
        case 0015:  xpos = 0;   break;  // CR   carriage return

        case 0011:                      // HT   tab
            xpos = (xpos&~7)+8;
            if (xpos >= TWIDTH) xpos = TWIDTH-1;
            break;

        case 0016:  sgc = G1;   break;  // SO   invoke G1 charset
        case 0017:  sgc = G0;   break;  // SI   invoke G0 charset

        case 0032:                      // SUB  interpreted as CAN
        case 0030:              break;  // CAN  cancel control sequence

        case 0033:  do_esc();   break;  // ESC  invoke control sequence

        case 0000:                      // NUL  ignored
        case 0005:                      // ENQ  xmit answerback message
        case 0007:                      // BEL  bell tone
        case 0021:                      // XON  resume transmission
        case 0023:                      // XOFF stop transmission
        case 0177:                      // DEL  ignored
        default:
            break;
        }
    } else {
        // VI100-ug Table 3-9 Special Graphics Characters
        if (sgc && c >= 0137 && c <=01760) c-=0137;

        blit_char(c);

        if (!(xpos == TWIDTH-1 && wrap)) xpos++;
    }

    if (xpos > TWIDTH) {
        xpos = 0;
        if (!wrap) ypos++;
    } else if (xpos < 0) xpos = 0;

    if (ypos < 0) {                 // XXX missing scroll down
        fprintf(stderr, "unhandled scroll down\n");
        ypos = 0;
    } else if (ypos >= THEIGHT) {   // scroll up
        SDL_Rect sr = {0, CHEIGHT, WIDTH, CHEIGHT };
        SDL_Rect dr = {0, 0,       WIDTH, CHEIGHT };

        ypos = THEIGHT-1;

        while (sr.y < CHEIGHT*THEIGHT) {
            SDL_BlitSurface(screen, &sr, screen, &dr);
            sr.y+=CHEIGHT;
            dr.y+=CHEIGHT;
        }

        SDL_FillRect(screen, &dr, 0);
    }
}

static SDL_Surface *load_optimized(char *filename) {
    SDL_Surface *i, *j = NULL;
    if ((i = IMG_Load(filename))) {
        j = SDL_DisplayFormat(i);
        SDL_FreeSurface(i);
    } else fatal("unable to load '%s'\n", filename);
    return j;
}

static SDL_Surface *empty_surface(int w, int h) {
    SDL_Surface *i, *j;
    i = SDL_CreateRGBSurface(SDL_HWSURFACE, w, h, BPP, 1, 2, 4, 8);
    j = SDL_DisplayFormat(i);
    SDL_FreeSurface(i);
    return j;
}

int main( int argc, char **argv) {
    SDL_Event event;
    pid_t childpid;
    int ascii = -1, numread, status, redraw = 1, disable_cursor = 0;
    char tempchar;
    char *bgfile = MYTERM_DATA BGFILE;

    argv++; argc--;

    while (argc && *argv[0] == '-') {
        if (!strcmp(*argv, "-bg")) {
            if (!--argc) fatal("option %s needs an operand\n");
            bgfile = *++argv;
        } else if (!strcmp(*argv, "-nc")) {
            disable_cursor = 1;
        } else if (!strcmp(*argv, "-h")) {
            fatal("usage: myterm [-bg file] [-nc] [-h] command [options]\n\n"
                  "\t-bg file\tset background image\n"
                  "\t-nc     \tdisable cursor\n"
                  "\t-h      \thelp\n\n");
        } else {
            fatal("unknown option: %s\n", *argv);
        }
        argc--;
        argv++;
    }

    if (argc < 1) fatal("need to specify a command to run\n");

    if ((fd_parent = posix_openpt(O_RDWR)) < 0)     fatal("posix_openpt()\n");
    if (grantpt(fd_parent))                         fatal("grantpt()\n");
    if (unlockpt(fd_parent))                        fatal("unlockpt()\n");

    fd_child = open(ptsname(fd_parent), O_RDWR);

    if ((childpid = fork()) < 0) fatal("cannot fork\n");

    fcntl(fd_parent, F_SETFL, O_NONBLOCK);

    if (!childpid) {
        close(fd_parent);

        close(0);       close(1);       close(2);
        dup(fd_child);  dup(fd_child);  dup(fd_child);

        close(fd_child);

        setsid();                   // session leader
        ioctl(0, TIOCSCTTY, 1);     // be controlling terminal

        setenv("TERM", "vt100", 1);
        setenv("COLUMNS", stringify(TWIDTH), 1);
        setenv("LINES", stringify(THEIGHT), 1);

        execvp(*argv, argv);

        fatal("exec()\n");
    } else close(fd_child);

    if (SDL_Init(SDL_INIT_EVERYTHING) < 0) fatal("sdl: init failed\n");

    if (!(display = SDL_SetVideoMode(WIDTH, HEIGHT, BPP,
                                            SDL_HWSURFACE|SDL_DOUBLEBUF)))
        fatal("sdl: couldn't get screen\n");

    SDL_ShowCursor(SDL_DISABLE);

    screen   = empty_surface(WIDTH, HEIGHT);

    normal   = load_optimized(MYTERM_DATA FONTFILE);
    boldfont = load_optimized(MYTERM_DATA BOLDFILE);
    bg       = load_optimized(bgfile);

    bufsize  = 2;
    buf      = xrealloc(NULL, bufsize);

    while(1) {
        ascii = -1;
        while(SDL_PollEvent(&event)) {
            int keysym = event.key.keysym.sym;

            switch(event.type) {
            case SDL_KEYDOWN:
                switch(keysym) {
                case SDLK_RCTRL:
                case SDLK_LCTRL:        ctrl = 1;       break;
                case SDLK_CAPSLOCK:
                case SDLK_RSHIFT:
                case SDLK_LSHIFT:       shift = !shift; break;
                case SDLK_RETURN:       ascii = 0012;   break;
                case SDLK_BACKSPACE:    ascii = 0010;   break;
                case SDLK_ESCAPE:       ascii = 0033;   break;
                case SDLK_TAB:          ascii = 0011;   break;

                case SDLK_F1:   OUTPUT("\033O", 2);     ascii = 'P';    break;
                case SDLK_F2:   OUTPUT("\033O", 2);     ascii = 'Q';    break;
                case SDLK_F3:   OUTPUT("\033O", 2);     ascii = 'R';    break;
                case SDLK_F4:   OUTPUT("\033O", 2);     ascii = 'S';    break;

                case SDLK_F5:   OUTPUT("\033[15", 4);   ascii = '~';    break;
                case SDLK_F6:   OUTPUT("\033[17", 4);   ascii = '~';    break;
                case SDLK_F7:   OUTPUT("\033[18", 4);   ascii = '~';    break;
                case SDLK_F8:   OUTPUT("\033[19", 4);   ascii = '~';    break;
                case SDLK_F9:   OUTPUT("\033[20", 4);   ascii = '~';    break;
                case SDLK_F10:  OUTPUT("\033[21", 4);   ascii = '~';    break;
                case SDLK_F11:  OUTPUT("\033[23", 4);   ascii = '~';    break;
                case SDLK_F12:  OUTPUT("\033[24", 4);   ascii = '~';    break;

                case SDLK_UP:       OUTPUT("\033O", 2); ascii = 'A';    break;
                case SDLK_DOWN:     OUTPUT("\033O", 2); ascii = 'B';    break;
                case SDLK_RIGHT:    OUTPUT("\033O", 2); ascii = 'C';    break;
                case SDLK_LEFT:     OUTPUT("\033O", 2); ascii = 'D';    break;

                default:
//                    fprintf(stderr,"down: %i\n", event.key.keysym.sym);
                    break;
                }

                if (keysym >= SDLK_SPACE && keysym < SDLK_DELETE)
                    ascii = keysym;

                // VT100-ug Table 3-2 Alphabetic Key Codes (w/ SHIFT)
                if (shift && ascii >= 0141 && ascii <= 0172)
                    ascii -= 0040;

                // VT100-ug Table 3-3 Nonalphabetic Key Codes (w/ SHIFT)
                if (shift) switch(ascii) {
                case 0054:  
                case 0056:
                case 0057:  ascii += 0020;  break;

                case 0061:
                case 0063:
                case 0064:
                case 0065:  ascii -= 0020;  break;

                case 0133:
                case 0134:
                case 0135:  ascii += 0040;  break;

                case 0047:  ascii  = 0042;  break;
                case 0055:  ascii  = 0137;  break;
                case 0060:  ascii  = 0051;  break;
                case 0062:  ascii  = 0100;  break;
                case 0066:  ascii  = 0136;  break;
                case 0067:  ascii  = 0046;  break;
                case 0070:  ascii  = 0052;  break;
                case 0071:  ascii  = 0050;  break;
                case 0073:  ascii  = 0072;  break;
                case 0075:  ascii  = 0053;  break;
                case 0140:  ascii  = 0176;  break;
                }

                // VT100-ug Table 3-5 Control Codes
                if (ctrl) {
                         if (ascii == 0040                 ) ascii  = 0;
                    else if (ascii >= 0101 && ascii <= 0136) ascii -= 0100;
                    else if (ascii >= 0141 && ascii <= 0176) ascii -= 0140;
                }
                break;

            case SDL_KEYUP:
                switch(keysym) {
                case SDLK_RCTRL:
                case SDLK_LCTRL:    ctrl = 0;           break;
                case SDLK_CAPSLOCK:
                case SDLK_RSHIFT:
                case SDLK_LSHIFT:   shift = !shift;     break;
                default:
//                    fprintf(stderr,"up: %i\n", event.key.keysym.sym);
                    break;
                }
                break;

            default:
                break;
            }
        }

        if (ascii >= 0) {
            tempchar = ascii;
            if (tempchar==0010) tempchar = 0177;    // send DEL instead of BS
            write(fd_parent, &tempchar, 1);
        }
        while ((numread = read(fd_parent, &tempchar, 1)) > 0) {
//            fprintf(stderr, "charread: %i (%c)\n", tempchar,
//                            isprint(tempchar) ? tempchar : '.');
            output_char(tempchar);
            redraw = 1;
        }

        if (waitpid(childpid, &status, WNOHANG)) break;

        if (redraw) {
            SDL_Rect cursor = {xpos*CWIDTH, ypos*CHEIGHT+CHEIGHT-2, CWIDTH, 1};
            SDL_BlitSurface(bg, NULL, display, NULL);
            SDL_SetColorKey(screen, SDL_SRCCOLORKEY, 0);    // black
            SDL_BlitSurface(screen, NULL, display, NULL);
            SDL_SetColorKey(screen, 0, 0);
            if (!disable_cursor) SDL_FillRect(display, &cursor, ~0);
            SDL_Flip(display);
            redraw = 0;
        }
        SDL_Delay(20);
    }

    SDL_Quit();
    return 0;
}
