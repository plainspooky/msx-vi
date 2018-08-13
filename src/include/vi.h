#define BACKSPACE   8
#define ESC         27
#define ARROW_RIGHT 28
#define ARROW_LEFT  29
#define ARROW_UP    30
#define ARROW_DOWN  31
#define PAGE_UP     -1 // MSX doesn't have this key
#define PAGE_DOWN   -2 // MSX doesn't have this key
#define HOME_KEY    11
#define END_KEY     -3 // MSX doesn't have this key
#define DEL_KEY     127

/* vi modes */
#define M_COMMAND 0
#define M_INSERT  1

/* skip thing movements */
#define FORWARD     1
#define BACKWARD    -1
#define S_BEFORE_WS 1
#define S_BEFORE_WS 1
#define S_TO_WS     2
#define S_OVER_WS   3
#define S_END_PUNCT 4
#define S_END_ALNUM 5

/* open/create flags */
#define  O_RDONLY   0x01
#define  O_WRONLY   0x02
#define  O_RDWR     0x00
#define  O_INHERIT  0x04

/* file attributes */
#define READ_ONLY    0x01
#define HIDDEN_FILE  0x02
#define SYSTEM_FILE  0x04
#define VOLUME_NAME  0x08
#define DIRECTORY    0x10
#define ARCHIVE_BIT  0x20
#define DEVICE_BIT   0x80

/* DOS calls */
#define TERM    #0x62
#define DOSVER  #0x6F
#define OPEN    #0x43
#define CREATE  #0x44
#define CLOSE   #0x45
#define READ    #0x48
#define WRITE   #0x49
#define _EOF    0xC7
#define GTIME   #0x2C
#define GDATE   #0x2A

/* DOS errors */
#define NOFIL   0xD7
#define IATTR   0xCF
#define DIRX    0xCC

/* BIOS calls */
#define CHGET  #0x009F
#define CHPUT  #0x00A2
#define CALSLT #0x001C
#define EXPTBL #0xFCC1
#define POSIT  #0x00C6
#define INITXT #0x006C
#define CLS    #0x00C3
#define CHGCLR #0x0062
#define CHGMOD #0x005F
#define WRTVRM #0x004D
#define NRDVRM #0x0174
#define NWRVRM #0x0177
#define FILVRM #0x0056
#define DISSCR #0x0041
#define ENASCR #0x0044
#define WRTVDP #0x0047

/* Memory variables */
#define LINL40 0xF3AE
#define FORCLR 0xF3E9
#define BAKCLR 0xF3EA
#define BDRCLR 0xF3EB

/* Curremt system colors */
#define CUR_FORCLR (* (char *) FORCLR)
#define CUR_BAKCLR (* (char *) BAKCLR)
#define CUR_BDRCLR (* (char *) BDRCLR)

#define CTRL_KEY(k) ((k) & 0x1f)
#define DOSCALL  call 5
#define BIOSCALL ld iy,(EXPTBL-1)\
call CALSLT
