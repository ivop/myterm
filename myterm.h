#ifndef MYTERM_H
#define MYTERM_H

#define _XOPEN_SOURCE 600

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "SDL/SDL.h"
#include "SDL/SDL_image.h"

#define stringify(s) do_stringify(s)
#define do_stringify(s) #s

#ifndef MYTERM_DATA
#define MYTERM_DATA "/usr/share/myterm/"
#endif

#ifndef WIDTH
#define WIDTH  320
#endif

#ifndef HEIGHT
#define HEIGHT 240
#endif

#ifndef TWIDTH
#define TWIDTH 53
#endif

#ifndef THEIGHT
#define THEIGHT 24
#endif

#ifndef CWIDTH
#define CWIDTH 6
#endif

#ifndef CHEIGHT
#define CHEIGHT 10
#endif

#ifndef FONTFILE
#define FONTFILE "font6x10.png"
#endif

#ifndef BOLDFILE
#define BOLDFILE "bold6x10.png"
#endif

#ifndef BGFILE
#define BGFILE "background.png"
#endif

#ifndef BPP
#define BPP    16
#endif

#endif
