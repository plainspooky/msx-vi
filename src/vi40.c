// vim:foldmethod=marker:foldlevel=0
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>

#include <vi.h>

#define MSXVI_VERSION "0.1.1"
#define MSXVI_TAB_STOP 8

#define SCREEN_WIDTH 40

#define FILE_BUFFER_LENGTH 1024
#define LINE_BUFFER_LENGTH 1024

#define TEXT2_COLOR_TABLE 0x00800


// External functions in iolib.rel
extern unsigned char inp (unsigned char);
extern void outp (unsigned char, unsigned char);

/*** data {{{ ***/

typedef struct erow {
  int size;
  int rsize;
  char *chars;
  char *render;
} erow;

struct editorConfig {
  int cx, cy;
  int rx;
  int rowoff;
  int coloff;
  int screenrows;
  int screencols;
  int numrows;
  erow *row;
  int dirty;
  char *filename;
  char statusmsg[SCREEN_WIDTH];
  char mode;
  char full_refresh;
  char welcome_msg;
  char msgbar_updated;
  char *search_pattern;
};

typedef struct {
  char hour;  /* Hours 0..23 */
  char min; /* Minutes 0..59 */
  char sec; /* Seconds 0..59 */
} TIME;

typedef struct {
  int year; /* Year 1980...2079 */
  char month; /* Month 1=Jan..12=Dec */
  char day; /* Day of the month 1...31 */
  char dow; /* On getdate() gets Day of week 0=Sun...6=Sat */
} DATE;

/*** end data }}} ***/

/*** global variables {{{ ***/

struct editorConfig E;


/*** end global variables }}} ***/

/*** prototypes {{{ ***/

void editorSetStatusMessage(const char *fmt, ...);
void editorRefreshScreen();
char *editorPrompt(char *prompt);
void quit_program(int exit_code);
void clear_inverted_area(void);
void editorDrawRowY(int y);

/*** end prototypes }}} ***/

/*** functions {{{ ***/

int isdigits(char *s) {
  while (*s != '\0') {
    if (!isdigit(*s++))
      return 0;
  }

  return 1;
}

// Inspired in busybox vi
int st_test(int type) {
  if (type == S_END_PUNCT) {
    return (ispunct(E.row[E.cy].chars[E.cx]));
  } else if (type == S_END_ALNUM) {
    return (isalnum(E.row[E.cy].chars[E.cx]) || E.row[E.cy].chars[E.cx] == '_');
  } else if (type == S_OVER_WS) {
    return (isspace(E.row[E.cy].chars[E.cx]));
  } else if (type == S_END_PUNCT) {
    return (ispunct(E.row[E.cy].chars[E.cx]));
  } else {
    return 0;
  }
}

void skip_thing(int dir, int type, int linecnt) {
  while (st_test(type)) {
    // If direction is forward and is end of line
    if (dir >= 0 && E.cx >= E.row[E.cy].rsize -1) {
      // If current line is lower than total number of lines
      if (E.cy < E.numrows - 1 && linecnt == 1) {
        E.cy++;
        E.cx = 0;
        break;
      } else {
        break;
      }
    // If direction is backwards and is beginning of line
    } else if (dir < 0 && E.cx <= 0) {
      // current line is not the first one
      if (E.cy > 0 && linecnt == 1) {
        E.cy--;
        E.cx = E.row[E.cy].rsize - 1;
        break;
      } else {
        E.cx = E.cx + linecnt * dir;
        break;
      }
    } else {
      E.cx = E.cx + dir;
    }
  }
}

// function (Register number, Data)
void VRegister(unsigned char reg, unsigned char data) {
  outp (0x99, data);
  outp (0x99, reg + 0x80);
} // --- is this function alread used???

// function (VMem Address, 0-Read / 1-Write)
void VMemadr(long addr, char rw) {
  unsigned char low, mid, hi;

  low = addr & 0xff;
  mid = (addr >> 8) & 0x3f;
  hi = (addr >> 14) & 0x7;

  // HI ADDR
  outp (0x99, hi);
  // R # 14
  outp (0x99, 0x8e);

  // LOW ADDR
  outp (0x99, low);
  // MID ADDR
  if (rw) {
    // WRITE
    outp (0x99, mid + 0x40);
  } else {
    // READ
    outp (0x99, mid);
  }
}

char dosver(void) __naked {
  __asm
    push ix

    ld c, DOSVER
    DOSCALL

    ld h, #0x00
    ld l, b

    pop ix
    ret
  __endasm;
}

char getchar(void) __naked {
  __asm
    push ix

    ld ix,CHGET
    BIOSCALL
    ld h, #0x00
    ld l,a ;reg to put a read character

    pop ix
    ret
  __endasm;
}

void putchar(char c) __naked {
  c;
  __asm
    push ix
    ld ix,#4
    add ix,sp

    ld a,(ix)
    ld ix,CHPUT
    BIOSCALL

    pop ix
    ret
  __endasm;
}

void putchar_vdp(char c, char x, char y) {
  __asm
    di
  __endasm;
  VMemadr(x + y*E.screencols, 1);
  outp(0x98, c);
  __asm
    ei
  __endasm;
}

void initxt(char columns) __naked {
  columns;
  __asm
    push ix
    ld ix,#4
    add ix,sp

    ld a,(ix)
    ld (LINL40),a

    ld ix,INITXT
    BIOSCALL

    pop ix
    ret
  __endasm;
}

// https://stackoverflow.com/questions/252782/strdup-what-does-it-do-in-c
char *strdup (const char *s) {
  char *d = malloc (strlen (s) + 1);   // Space for length plus nul
  if (d == NULL) return NULL;          // No memory
  strcpy (d,s);                        // Copy the characters
  return d;                            // Return the new string
}

void gotoxy(char x, char y) {
  printf("\33Y%c%c", y + 32, x + 32);
}

void cls(void) __naked {
  __asm
    push ix
    cp a
    ld ix,CLS
    BIOSCALL
    pop ix
    ret
  __endasm;
}

int open(char *fn, char mode) __naked {
  fn;
  mode;
  __asm
    push ix
    ld ix,#4
    add ix,sp

    ld e,0(ix)
    ld d,1(ix)
    ld a,2(ix)
    ld c, OPEN
    DOSCALL

    cp #0
    jr z, open_noerror$
    ld h, #0xff
    ld l, a
    jp open_error$
  open_noerror$:
    ld h, #0x00
    ld l, b
  open_error$:
    pop ix
    ret
  __endasm;
}

int create(char *fn, char mode, char attributes) __naked {
  fn;
  mode;
  attributes;
  __asm
    push ix
    ld ix,#4
    add ix,sp

    ld e,0(ix)
    ld d,1(ix)
    ld a,2(ix)
    ld b,3(ix)
    ld c, CREATE
    DOSCALL

    cp #0
    jr z, open_noerror$
    ld h, #0xff
    ld l, a
    jp open_error$
  create_noerror$:
    ld h, #0x00
    ld l, b
  create_error$:
    pop ix
    ret
  __endasm;
}

int close(int fp) __naked {
  fp;
  __asm
    push ix
    ld ix,#4
    add ix,sp

    ld b,(ix)
    ld c, CLOSE
    DOSCALL

    ld h, #0x00
    ld l,a

    pop ix
    ret
  __endasm;
}

void exit(int code) __naked {
  code;
  __asm
    push ix
    ld ix,#4
    add ix,sp

    ld b,(ix)
    ld c, TERM
    DOSCALL

    pop ix
    ret
  __endasm;
}

void gtime(TIME* t) __naked {
  t;
  __asm
    push ix
    ld ix,#4
    add ix,sp

    ld l,0(ix)
    ld h,1(ix)
    ld d,2(ix)
    push hl

    ld c, GTIME
    DOSCALL

    pop ix

    ld 0(ix),h
    ld 1(ix),l
    ld 2(ix),d

    pop ix
    ret
  __endasm;
}

void gdate(DATE* d) __naked {
  d;
  __asm
    push ix
    ld ix,#4
    add ix,sp

    ld l,0(ix)
    ld h,1(ix)
    push hl

    ld c, GDATE
    DOSCALL

    pop ix

    ld 0(ix),l
    ld 1(ix),h
    ld 2(ix),d
    ld 3(ix),e
    ld 4(ix),a

    pop ix
    ret
  __endasm;
}

time_t _time() {
  TIME time;
  DATE date;

  gtime(&time);
  gdate(&date);
  /* XXX Calculating basic not acurate timestamp. */
  return (((( ((date.year-1970)*365) + (date.month * 30) + date.day ) * 24 + time.hour) * 60 + time.min) * 60 + time.sec);
}

void die(const char *s, ...) {
  va_list ap;
  va_start(ap, s);
  cls();
  gotoxy(0, 0);
  printf("*** ");
  vprintf(s, ap);
  printf("\r\n");
  clear_inverted_area();
  exit(1);
}

int getCursorPosition(int *rows, int *cols) {
  static char __at(0xF3DC) _CSRY;
  static char __at(0xF3DD) _CSRX;

  *rows = _CSRY;
  *cols = _CSRX;

  return 0;
}

int getWindowSize(int *rows, int *cols) {
  gotoxy(999, 999);
  return getCursorPosition(rows, cols);
}

/*** end functions }}} ***/

/*** graphic functions {{{ ***/

void color(char fg, char bg, char bd) __naked {
  fg;
  bg;
  bd;
  __asm
    push ix
    ld ix,#4
    add ix,sp

    ld a,0(ix)
    ld(#FORCLR),a
    ld a,1(ix)
    ld(#BAKCLR),a
    ld a,2(ix)
    ld(#BDRCLR),a

    ld ix,CHGCLR
    BIOSCALL

    pop ix
    ret
  __endasm;
}

void vdp_w(unsigned char data, char reg) __naked {
  reg;
  data;
  __asm
    push ix
    ld ix,#4
    add ix,sp

    ld b,0(ix)
    ld c,1(ix)
    ld ix,WRTVDP
    BIOSCALL

    pop ix
    ret
  __endasm;
}

void vpoke(unsigned int address, unsigned char value) __naked {
  address;
  value;
  __asm
    push ix
    ld ix,#4
    add ix,sp

    ld l,0(ix)
    ld h,1(ix)
    ld a,2(ix)
    ld ix,NWRVRM
    BIOSCALL

    pop ix
    ret
  __endasm;
}

unsigned char vpeek(unsigned int address) __naked {
  address;
  __asm
    push ix
    ld ix,#4
    add ix,sp

    ld l,0(ix)
    ld h,1(ix)
    ld ix,NRDVRM
    BIOSCALL

    ld h, #0x00
    ld l,a

    pop ix
    ret
  __endasm;
}

void set_blink_colors(char fg, char bg) {
  vdp_w(bg*16+fg, 12);
}

void set_inverted_area(void) {
  int i;

  for (i=0; i < (E.screenrows+2) * E.screencols/8; i++) vpoke(TEXT2_COLOR_TABLE+i, 0x00);
  for (i=0; i < E.screencols/8; i++) vpoke(TEXT2_COLOR_TABLE+i + (E.screencols/8*22), 0xff);
  vdp_w(0x4f, 12); // blink colors
  set_blink_colors(CUR_FORCLR, CUR_BAKCLR);
  vdp_w(0xf0, 13); // blink speed: stopped
}

void clear_inverted_area(void) {
  int n;

  // Set back blink table to blank
  for (n=0; n < (E.screenrows+2) * E.screencols/8; n++) vpoke(TEXT2_COLOR_TABLE+n, 0x00);
}

/*** end graphic functions }}} ***/

/*** row operations {{{ ***/

int editorRowCxToRx(erow *row, int cx) {
  int rx = 0;
  int j;
  for (j = 0; j < cx; j++) {
    if (row->chars[j] == '\t')
      rx += (MSXVI_TAB_STOP - 1) - (rx % MSXVI_TAB_STOP);
    rx++;
  }
  return rx;
}

int editorRowRxToCx(erow *row, int rx) {
  int cur_rx = 0;
  int cx;
  for (cx = 0; cx < row->size; cx++) {
    if (row->chars[cx] == '\t')
      cur_rx += (MSXVI_TAB_STOP - 1) - (cur_rx % MSXVI_TAB_STOP);
    cur_rx++;
    if (cur_rx > rx) return cx;
  }
  return cx;
}

void editorUpdateRow(erow *row) {
  int tabs = 0;
  int idx = 0;
  int j;

  for (j = 0; j < row->size; j++)
    if (row->chars[j] == '\t') tabs++;

  free(row->render);
  row->render = malloc(row->size + tabs*(MSXVI_TAB_STOP - 1) + 1);

  for (j = 0; j < row->size; j++) {
    if (row->chars[j] == '\t') {
      row->render[idx++] = ' ';
      while (idx % MSXVI_TAB_STOP != 0) row->render[idx++] = ' ';
    } else {
      row->render[idx++] = row->chars[j];
    }
  }
  row->render[idx] = '\0';
  row->rsize = idx;
}

void editorInsertRow(int at, char *s, size_t len) {

  // Update screen
  E.full_refresh = 1;

  if (at < 0 || at > E.numrows) return;
  E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));
  memmove(&E.row[at + 1], &E.row[at], sizeof(erow) * (E.numrows - at));

  E.row[at].size = len;
  E.row[at].chars = malloc(len + 1);
  memcpy(E.row[at].chars, s, len);
  E.row[at].chars[len] = '\0';

  E.row[at].rsize = 0;
  E.row[at].render = NULL;
  editorUpdateRow(&E.row[at]);
  E.numrows++;
  E.dirty++;
}

void editorFreeRow(erow *row) {
  free(row->render);
  free(row->chars);
}
void editorDelRow(int at) {
  // Update screen
  // TODO Only update required part of screen
  E.full_refresh = 1;

  if (at < 0 || at >= E.numrows) return;
  editorFreeRow(&E.row[at]);
  memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numrows - at - 1));
  E.numrows--;
  E.dirty++;
}

void editorRowInsertChar(erow *row, int at, int c) {
  if (at < 0 || at > row->size) at = row->size;
  row->chars = realloc(row->chars, row->size + 2);
  memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
  row->size++;
  row->chars[at] = c;
  editorUpdateRow(row);
  E.dirty++;
}

void editorRowAppendString(erow *row, char *s, size_t len) {
  row->chars = realloc(row->chars, row->size + len + 1);
  memcpy(&row->chars[row->size], s, len);
  row->size += len;
  row->chars[row->size] = '\0';
  editorUpdateRow(row);
  E.dirty++;
}

void editorRowDelChar(erow *row, int at) {
  if (at < 0 || at >= row->size) return;
  memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
  row->size--;
  editorUpdateRow(row);
  E.dirty++;
}

/*** end row operations }}} ***/

/*** editor operations {{{ ***/

void editorInsertChar(int c) {
  if (E.cy == E.numrows) {
    editorInsertRow(E.numrows, "", 0);
  }

  editorRowInsertChar(&E.row[E.cy], E.cx, c);
  // Update screen
  editorDrawRowY(E.cy);
  E.cx++;
}

void editorInsertNewline() {
  if (E.cx == 0) {
    editorInsertRow(E.cy, "", 0);
  } else {
    erow *row = &E.row[E.cy];
    editorInsertRow(E.cy + 1, &row->chars[E.cx], row->size - E.cx);
    row = &E.row[E.cy];
    row->size = E.cx;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
  }
  E.cy++;
  E.cx = 0;
}

void editorDelChar(char allow_delete_line) {
  erow *row;

  if (E.cy == E.numrows) return;
  if (E.cx == 0 && E.cy == 0) return;
  row = &E.row[E.cy];
  if (E.cx > 0) {
    editorRowDelChar(row, E.cx - 1);
    E.cx--;
  } else if (allow_delete_line) {
    E.cx = E.row[E.cy - 1].size;
    editorRowAppendString(&E.row[E.cy - 1], row->chars, row->size);
    editorDelRow(E.cy);
    E.cy--;
  }

  // Update screen
  editorDrawRowY(E.cy);
}

/*** editor operations }}} ***/

/*** file io {{{ ***/

int read(char* buf, unsigned int size, char fp) __naked {
  buf;
  size;
  fp;
  __asm
    push ix
    ld ix,#4
    add ix,sp

    ld e,0(ix)
    ld d,1(ix)
    ld l,2(ix)
    ld h,3(ix)
    ld b,4(ix)
    ld c, READ
    DOSCALL

    cp #0
    jr z, read_noerror$
    ld h, #0xFF
    ld l, #0xFF
  read_noerror$:
    pop ix
    ret
  __endasm;
}

int write(char* buf, unsigned int size, int fp) __naked {
  buf;
  size;
  fp;
  __asm
    push ix
    ld ix,#4
    add ix,sp

    ld e,0(ix)
    ld d,1(ix)
    ld l,2(ix)
    ld h,3(ix)
    ld b,4(ix)
    ld c, WRITE
    DOSCALL

    cp #0
    jr z, write_noerror$
    ld h, #0xFF
    ld l, #0xFF
  write_noerror$:
    pop ix
    ret
  __endasm;
}

void editorOpen(char *filename) {
  int fp;
  char buffer[FILE_BUFFER_LENGTH];
  char line_buffer[LINE_BUFFER_LENGTH];
  int bytes_read;
  int total_read;
  int n, m;

  free(E.filename);
  E.filename = strdup(filename);

  fp  = open(filename, O_RDONLY);
  if (fp < 0) {
    // Error is in the least significative byte of fp
    n = (fp >> 0) & 0xff;
    if (n == NOFIL) {
      E.filename = filename;
      return;
    } else if (n == DIRX) {
      die("Error opening file: Specified file is a directory.");
    } else if (n == IATTR) {
      die("Error opening file: Invalid attributes.");
    } else {
      die("Error opening file: Error code: 0x%X", n);
    }
  }

  m = 0; // Counter for line buffer
  total_read = 0;
  printf("\33x5\33Y6 \33KOpening file: %s", filename);
  while(1) {
    bytes_read = read(buffer, sizeof(buffer), fp);
    total_read = total_read + bytes_read;
    printf("\33Y7 %d bytes read...", total_read);

    for (n=0; n<bytes_read ;n++) {
      if (buffer[n] != '\n' && buffer[n] != '\r') {
        line_buffer[m] = buffer[n];
        m++;
      } else if (buffer[n] == '\n') {
        line_buffer[m] = buffer[n];
        line_buffer[m+1] = '\0';
        editorInsertRow(E.numrows, line_buffer, m);
        m = 0;
      }

      if (m >= LINE_BUFFER_LENGTH) {
        die("Error opening file: line too long.");
      }
    }

    if (bytes_read < sizeof(buffer)) {
      // EOF
      break;
    }
  }

  printf("\33Y6 \33KDone!");
  close(fp);
  E.dirty = 0;
}

int editorSave(char *filename) {
  int fp;
  char line_buffer[LINE_BUFFER_LENGTH];
  int n;
  int bytes_written;
  int total_written;

  if (filename == NULL) {
    editorSetStatusMessage("No current filename");
    return 1;
  }

  free(E.filename);
  E.filename = strdup(filename);

  printf("\33x5\33Y7 \33K");
  total_written = 0;
  // Using create instead of open so the file size is updated when saving
  fp = create(filename, O_RDWR, 0x00);
  if (fp < 0) {
    // Error is in the least significative byte of fp
    n = (fp >> 0) & 0xff;
    if (n == NOFIL) {
      editorSetStatusMessage("Error saving to disk: File does not exist.");
      fp = create(filename, O_RDWR, 0x00);
      if (fp < 0) {
        editorSetStatusMessage("Error saving to disk: Error code: 0x%X", n);
        return 1;
      }
    } else if (n == DIRX) {
      editorSetStatusMessage("Error saving to disk: Specified file is a directory.");
      return 1;
    } else if (n == IATTR) {
      editorSetStatusMessage("Error saving to disk: Invalid attributes.");
      return 1;
    } else {
      editorSetStatusMessage("Error saving to disk: Error code: 0x%X", n);
      return 1;
    }
  }
  for (n=0; n < E.numrows; n++) {
    strcpy(line_buffer, E.row[n].chars);
    line_buffer[E.row[n].size] = '\r';
    line_buffer[E.row[n].size+1] = '\n';
    bytes_written = write(line_buffer, E.row[n].size+2, fp);
    total_written = total_written + bytes_written;
    printf("\33Y7 %d bytes written to disk: %s", total_written, filename);
  }
  line_buffer[0] = 0x1a;
  write(line_buffer, 1, fp);
  close(fp);

  editorSetStatusMessage("%d bytes written to disk: %s Done!", total_written, filename);
  E.dirty = 0;
  return 0;
}

/*** file io }}} ***/

/*** find {{{ ***/

void editorFindCallback(char *query, signed char direction, char direction_mod) {
  char *match;
  erow *row;
  int i;
  int x;
  int current;
  static char current_direction = 1;

  if (direction != 0) {
    current_direction = direction;
  }

  if (query == NULL) {
    if (E.search_pattern == NULL) {
      return;
    }
  } else {
    free(E.search_pattern);
    E.search_pattern = strdup(query);
    free(query);
  }

  current = E.cy;
  for (i = 0; i < E.numrows; i++) {
    if (current <= -1) {
      current = E.numrows - 1;
      editorSetStatusMessage("search hit TOP, continuing at BOTTOM");
    } else if (current >= E.numrows) {
      current = 0;
      editorSetStatusMessage("search hit BOTTOM, continuing at TOP");
    }

    row = &E.row[current];
    match = strstr(row->render, E.search_pattern);

    if (match) {
      x = editorRowRxToCx(row, match - row->render);
      if (x > E.cx || E.cy != current) {
        E.cy = current;
        E.cx = x;
        return;
      }
    }
    current = current + current_direction * direction_mod;
  }
  editorSetStatusMessage("Pattern not found");
}

void editorFind(char c) {
  char *query = NULL;

  if (c == '/') {
    query = editorPrompt("/%s");
    editorFindCallback(query, 1, 1);
  } else if (c == '?') {
    query = editorPrompt("?%s");
    editorFindCallback(query, -1, 1);
  }
  if (query) {
    free(query);
  }
}

/*** find }}} ***/

/*** output {{{ ***/

void editorScroll() {
  E.rx = 0;
  if (E.cy < E.numrows) {
    E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
  }

  if (E.cy < E.rowoff) {
    E.rowoff = E.cy;
    E.full_refresh = 1;
  }
  if (E.cy >= E.rowoff + E.screenrows) {
    E.rowoff = E.cy - E.screenrows + 1;
    E.full_refresh = 1;
  }
  if (E.rx < E.coloff) {
    E.coloff = E.rx;
    E.full_refresh = 1;
  }
  if (E.rx >= E.coloff + E.screencols - 1) {
    E.coloff = E.rx - E.screencols + 2;
    E.full_refresh = 1;
  }
}

void editorDrawRow(int y) {
  int filerow;

  filerow = y + E.rowoff;

  if (strlen(E.row[filerow].render) > E.coloff) {
    // XXX Hardcoding columns -1 (79)
    printf("%.39s\33K", &E.row[filerow].render[E.coloff]);
  } else {
    printf("\33K");
  }

  if ((int)strlen(E.row[filerow].render) - (int)E.coloff >= (int)E.screencols) {
    putchar_vdp('$', 39, y);
  }
  gotoxy(0, y+1);
}

void editorDrawRowY(int y) {
  erow *row;

  row = &E.row[y];
  printf("\33x5\33Y%c%c", y - E.rowoff + 32, 32);

  editorDrawRow(y);

  if (E.cx >= E.row[y].size && E.mode == M_COMMAND)
    E.cx = E.row[y].size -1;
}

void editorDrawRows() {
  int y;
  char welcome[SCREEN_WIDTH];
  int len = 0;
  int filerow;

  for (y = 0; y < E.screenrows; y++) {
    filerow = y + E.rowoff;
    if (filerow >= E.numrows) {
      puts("~\33K\r");
    } else {
      editorDrawRow(y);
    }
  }

  if (E.welcome_msg && E.numrows == 0) {
    sprintf(welcome, "MSX vi -- version %s", MSXVI_VERSION);
    len = strlen(welcome);
    gotoxy((E.screencols - len) / 2, 7);
    printf("%s", welcome);

    sprintf(welcome, "by Carles Amigo (fr3nd)");
    len = strlen(welcome);
    gotoxy((E.screencols - len) / 2, 8);
    printf("%s", welcome);

    sprintf(welcome, "This is beta software and it may contain bugs.");
    len = strlen(welcome);
    gotoxy((E.screencols - len) / 2, 10);
    printf("%s", welcome);

    sprintf(welcome, "Use it at your own risk.");
    len = strlen(welcome);
    gotoxy((E.screencols - len) / 2, 11);
    printf("%s", welcome);

    E.welcome_msg = 0;
  }
}

void editorDrawStatusBar() {
  char mode = '-';

  switch (E.mode) {
    case M_COMMAND:
      mode = '-';
      break;
    case M_INSERT:
      mode = 'I';
      break;
  }

  printf("\33Y%c%c%c %.20s %s %d/%d\33K",
    22 + 32, // Y
    32, // X
    mode,
    E.filename ? E.filename : "No file",
    E.dirty ? "[Modified]" : "",
    E.cy + 1, E.numrows);
}

void editorDrawMessageBar() {
  if (E.msgbar_updated) {
    E.msgbar_updated = 0;
    printf("\33Y%c%c%s\33K",
      23 + 32, // Y
      32, // X
      E.statusmsg);
    E.statusmsg[0] = '\0';
    E.msgbar_updated = 1;
  }
}

void editorRefreshScreen() {
  editorScroll();

  printf("\33x5\33H"); // Hide cursor and gotoxy 0,0

  if (E.full_refresh) {
    E.full_refresh = 0;
    editorDrawRows();
  }
  // TODO Only draw when required
  editorDrawStatusBar();
  editorDrawMessageBar();

  gotoxy(E.rx - E.coloff, E.cy - E.rowoff);

  printf("\33y5"); // Enable cursor

}

void editorSetStatusMessage(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsprintf(E.statusmsg, fmt, ap);
  va_end(ap);
  E.msgbar_updated = 1;
}
/*** output }}} ***/

/*** input {{{ ***/

void runCommand() {
  char *command;
  char *token;
  char f[13];
  char params[40];
  int n;

  command = editorPrompt(":%s");

  if (command != NULL && strlen(command) > 0) {
    if (isdigits(command)) {
        n = (int)atoi(command) - 1;
        if (n < 0) n = 0;
        if (n >= E.numrows) n = E.numrows - 1;
        E.cy = n;
        E.cx = 0;
      } else if (strcmp(command, "q") == 0) {
      if (E.dirty) {
        editorSetStatusMessage("No write since last change (:q! overrides)");
      } else {
        quit_program(0);
      }
    } else if (strcmp(command, "w") == 0) {
      editorSave(E.filename);
    } else if (strncmp(command, "w ", 2) == 0) {
      strncpy(f, command + 2, 12);
      editorSave(f);
    } else if (strcmp(command, "q!") == 0) {
      quit_program(0);
    } else if (strcmp(command, "wq") == 0 || strcmp(command, "x") == 0) {
      if (editorSave(E.filename) == 0) {
        quit_program(0);
      }
    } else if (strncmp(command, "color", 5) == 0) {
      // Setting default colors
      params[0] = CUR_FORCLR;
      params[1] = CUR_BAKCLR;
      params[2] = CUR_BDRCLR;

      n = 0;
      token = strtok(command, " ");
      while( token != NULL ) {
        token = strtok(NULL, " ");
        if (atoi(token) > 0 && atoi(token) <= 15) {
          params[n] = atoi(token);
        }
        n++;
      }
      color(params[0], params[1], params[2]);
      // set_blink_colors(params[0], params[1]);

    } else {
      editorSetStatusMessage("Not an editor command: %s", command);
    }
  } else {
    editorSetStatusMessage("");
  }
}

char *editorPrompt(char *prompt) {
  size_t bufsize = 128;
  char *buf = malloc(bufsize);
  size_t buflen = 0;
  int c;

  buf[0] = '\0';
  while (1) {
    editorSetStatusMessage(prompt, buf);
    editorRefreshScreen();
    c = getchar();
    if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
      if (buflen != 0) buf[--buflen] = '\0';
    } else if (c == ESC) {
      free(buf);
      return NULL;
    } else if (c == '\r') {
      if (buflen != 0) {
        editorSetStatusMessage("");
        return buf;
      } else {
        free(buf);
        return NULL;
      }
    } else if (!iscntrl(c) && c < 128) {
      if (buflen == bufsize - 1) {
        bufsize *= 2;
        buf = realloc(buf, bufsize);
      }
      buf[buflen++] = c;
      buf[buflen] = '\0';
    }
  }
}

void editorMoveCursor(char key) {
  int times;
  int rowlen;
  erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];

  switch (key) {
    case HOME_KEY:
      E.cx = 0;
      break;
    case END_KEY:
      if (E.cy < E.numrows)
        E.cx = E.row[E.cy].size;
      break;
    case PAGE_UP:
    case PAGE_DOWN:
      if (key == PAGE_UP) {
        E.cy = E.rowoff;
      } else if (key == PAGE_DOWN) {
        E.cy = E.rowoff + E.screenrows - 1;
        if (E.cy > E.numrows) E.cy = E.numrows;
      }

      times = E.screenrows;
      while (times--)
        editorMoveCursor(key == PAGE_UP ? ARROW_UP : ARROW_DOWN);
      break;
    case ARROW_LEFT:
      if (E.cx != 0) {
        E.cx--;
      } else if (E.cy > 0 && E.mode != M_COMMAND) {
        E.cy--;
        E.cx = E.row[E.cy].size;
      }
      break;
    case ARROW_RIGHT:
      if (row && E.cx < row->size) {
        E.cx++;
      } else if (E.mode == M_INSERT && E.cx == 0 && row->size == 0) {
        E.cx++;
      } else if (row && E.cx == row->size && E.mode != M_COMMAND) {
        E.cy++;
        E.cx = 0;
      }
      break;
    case ARROW_UP:
      if (E.cy != 0) {
        E.cy--;
      }
      break;
    case ARROW_DOWN:
      if (E.cy < E.numrows - 1) {
        E.cy++;
      }
      break;
  }

  row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
  rowlen = row ? row->size : 0;
  if (E.cx > rowlen) {
    E.cx = rowlen;
  }

  if (E.cx == rowlen && E.mode == M_COMMAND) {
    E.cx--;
  }

  if (E.cx < 0)
    E.cx = 0;
  if (E.cy < 0)
    E.cy = 0;

}

void editorProcessKeypress() {
  char c;
  int n;

  c = getchar();

  if (E.mode == M_COMMAND) {
    switch (c) {
      case CTRL_KEY('d'):
        editorMoveCursor(PAGE_DOWN);
        break;
      case CTRL_KEY('u'):
        editorMoveCursor(PAGE_UP);
        break;
      case 'k':
        editorMoveCursor(ARROW_UP);
        break;
      case 'j':
        editorMoveCursor(ARROW_DOWN);
        break;
      case 'l':
        editorMoveCursor(ARROW_RIGHT);
        break;
      case 'h':
        editorMoveCursor(ARROW_LEFT);
        break;
      case ARROW_UP:
      case ARROW_DOWN:
      case ARROW_LEFT:
      case ARROW_RIGHT:
        editorMoveCursor(c);
        break;
      case CTRL_KEY('l'):
        E.full_refresh = 1;
        editorRefreshScreen();
        break;
      case ESC:
        printf("\a"); // BEEP
        break;
      case '0':
        editorMoveCursor(HOME_KEY);
        break;
      case '$':
        editorMoveCursor(END_KEY);
        break;
      case ':':
        runCommand();
        break;
      case '/': // Search word
        editorFind('/');
        break;
      case '?': // Search word backwards
        editorFind('?');
        break;
      case 'n': // Next search (forward)
        editorFindCallback(NULL, 0, 1);
        break;
      case 'N': // Previous search (backwards)
        editorFindCallback(NULL, 0, -1);
        break;
      case 'H': // Go to top of screen
        E.cx = 0;
        E.cy = E.rowoff;
        break;
      case 'M': // Go to the middle of screen
        E.cx = 0;
        n = E.rowoff + (E.screenrows - 2) / 2;
        if (n < E.numrows)
          E.cy = n;
        else
          if (E.numrows > 0)
            E.cy = E.numrows;
        break;
      case 'L': // Go to bottom of screen
        E.cx = 0;
        n = E.rowoff + E.screenrows - 1;
        if (n < E.numrows)
          E.cy = n;
        else
          if (E.numrows > 0)
            E.cy = E.numrows - 1;
        break;
      case 'g': // gg goes to beginning of file
        c = getchar();
        switch (c) {
          case 'g':
            E.cx = 0;
            E.cy = 0;
            break;
          default:
            editorSetStatusMessage("Command not implemented");
        }
        break;
      case 'G': // Go to the end of file
        E.cy = E.numrows - 1;
        editorMoveCursor(END_KEY);
        break;
      case 'd': // starts delete command
        c = getchar();
        switch (c) {
          case 'd': // delete line
            editorDelRow(E.cy);
            E.cx=0;
            break;
          case '0': // delete until the beginning of a line
            for (n=0; n<E.cx; n++)
              editorRowDelChar(&E.row[E.cy], 0);
            editorDrawRowY(E.cy);
            E.cx=0;
            break;
          case '$': // delete until the end of line
            for (n=E.row[E.cy].size; n>=E.cx; n--)
              editorRowDelChar(&E.row[E.cy], n);
            editorDrawRowY(E.cy);
            break;
          case 'G': // delete until end of file
            for (n=E.numrows; n>0; n--)
              editorDelRow(E.cy);
            break;
          default:
            editorSetStatusMessage("Command not implemented");
        }
        break;
      case 'r': // replace current char
        c = getchar();
        editorRowDelChar(&E.row[E.cy], E.cx);
        editorRowInsertChar(&E.row[E.cy], E.cx, c);
        editorDrawRowY(E.cy);
        break;
      case 'a':
        E.mode = M_INSERT;
        editorMoveCursor(ARROW_RIGHT);
        break;
      case 'A':
        E.mode = M_INSERT;
        editorMoveCursor(END_KEY);
        break;
      case 'i':
        E.mode = M_INSERT;
        break;
      case 'I':
        editorMoveCursor(HOME_KEY);
        E.mode = M_INSERT;
        break;
      case 'o':
        E.mode = M_INSERT;
        editorMoveCursor(HOME_KEY);
        editorInsertRow(E.cy+1, "", 0);
        editorMoveCursor(ARROW_DOWN);
        break;
      case 'O':
        E.mode = M_INSERT;
        editorMoveCursor(HOME_KEY);
        editorInsertRow(E.cy, "", 0);
        break;
      case 'x':
        E.cx++;
        editorDelChar(0);
        break;
      case 'X':
        editorDelChar(0);
        break;
      case 'w':
        if (E.row[E.cy].rsize == 0) {
          if (E.cy < E.numrows - 1) {
            E.cy++;
            E.cx=0;
          }
        } else if (isalnum(E.row[E.cy].chars[E.cx]) || E.row[E.cy].chars[E.cx] == '_') {
          skip_thing(FORWARD, S_END_ALNUM, 1);
        } else if (ispunct(E.row[E.cy].chars[E.cx])) {
          skip_thing(FORWARD, S_END_PUNCT, 1);
        }

        if (isspace(E.row[E.cy].chars[E.cx])) {
          skip_thing(FORWARD, S_OVER_WS, 1);
        }

        break;
      case 'b': // Back a word
      case 'e': // End of word
        if (c == 'e')
          n = FORWARD;
        else
          n = BACKWARD;

        // If direction is forward
        if (n == FORWARD) {
          // if it's not last line
          if (E.cy < E.numrows - 1) {

            // If Empty line or end of line
            if (E.row[E.cy].rsize == 0 || E.cx == E.row[E.cy].rsize -1) {
              E.cy++;
              E.cx=-1;
            }
          }
        // If direction is backward
        } else {
          // If it's not first line
          if (E.cy > 0) {
            // If Empty line or beginning of line
            if (E.row[E.cy].rsize == 0 || E.cx == 0) {
              E.cy--;
              E.cx = E.row[E.cy].rsize - 1;
            }
          }
        }

        E.cx += n;

        if (isspace(E.row[E.cy].chars[E.cx])) {
          skip_thing(n, S_OVER_WS, 0);
        }
        if (isalnum(E.row[E.cy].chars[E.cx]) || E.row[E.cy].chars[E.cx] == '_') {
          skip_thing(n, S_END_ALNUM, 0);
        } else if (ispunct(E.row[E.cy].chars[E.cx])) {
          skip_thing(n, S_END_PUNCT, 0);
        }

        if (n == BACKWARD && E.cx != 0)
          E.cx -= n;
        else if (n == FORWARD && E.cx != E.row[E.cy].rsize - 1)
          E.cx -= n;

        break;
      default:
        editorSetStatusMessage("Command not implemented");
        break;
    }
  } else if (E.mode == M_INSERT) {
    switch (c) {
      case '\r':
        editorInsertNewline();
        break;
      case BACKSPACE:
        editorDelChar(1);
        break;
      case DEL_KEY:
        E.cx++;
        editorDelChar(1);
        break;
      case ESC:
        E.mode = M_COMMAND;
        editorMoveCursor(ARROW_LEFT);
        break;
      case HOME_KEY:
      case END_KEY:
      case ARROW_UP:
      case ARROW_DOWN:
      case ARROW_LEFT:
      case ARROW_RIGHT:
        editorMoveCursor(c);
        break;
      default:
        editorInsertChar(c);
        break;
    }
  }
}

/*** input }}} ***/

/*** main {{{ ***/

void init() {
  // Check MSX-DOS version >= 2
  if(dosver() < 2) {
    die("This program requires MSX-DOS 2 to run.");
  }

  // Set screen mode
  initxt(SCREEN_WIDTH);

  // Get window size
  if (getWindowSize(&E.screenrows, &E.screencols) == -1)
    die("ERROR: Could not get screen size");
  E.screenrows -= 2;

  // Set coords to 0,0
  gotoxy(0, 0);

  E.cx = 0;
  E.cy = 0;
  E.rx = 0;
  E.rowoff = 0;
  E.coloff = 0;
  E.numrows = 0;
  E.row = NULL;
  E.dirty = 0;
  E.filename = NULL;
  E.statusmsg[0] = '\0';
  E.mode = M_COMMAND;
  E.full_refresh = 1;
  E.welcome_msg = 1;
  E.msgbar_updated = 1;
  E.search_pattern = NULL;
}


void quit_program(int exit_code) {
  cls();
  clear_inverted_area();
  exit(exit_code);
}

void debug_keys(void) {
  char c;
  c = getchar();
  printf("%d\n\r", c);
}

int main(char **argv, int argc) {
  int i;
  char filename[13] ;
  *filename = '\0';

  /*while (1) debug_keys();*/

  init();

  for (i = 0; i < argc; i++) {
    if (argv[i][0] == '-') {
      switch (argv[i][1]) {
        /*case 'h':*/
          /* TODO */
          /*usage();*/
          /*break;*/
        default:
          die("ERROR: Parameter not recognized");
          break;
      }
    } else {
      strcpy(filename, argv[i]);
    }
  }

  // Set inverted text area
  // set_inverted_area();

  if (filename[0] != '\0') {
    editorOpen(filename);
  }

  while (1) {
    editorRefreshScreen();
    editorProcessKeypress();
  }
}

/*** main }}} ***/
