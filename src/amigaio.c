/*
 * Amigaio.c -- by Samuel DEVULDER inspired from atario.c 
 */

#include "ztypes.h"
#include "jzexe.h"

#ifndef HARD_COLORS
#define HARD_COLORS
#endif

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <strings.h>
#include <sys/signal.h>
// #include <unistd.h>
 
#include <exec/exec.h>
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/libraries.h>

#include <devices/conunit.h>

#include <libraries/dos.h>
#include <libraries/dosextens.h>

#ifndef __VBCC__
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/layers.h>
#else
#include <clib/intuition_proto.h>
#include <clib/dos_proto.h>
#include <clib/exec_proto.h>
#include <clib/exec_graphics.h>
#include <clib/layers_proto.h>
#endif

#define TITLE 		JZIPVER " ("JZIPRELDATE ") by " JZIPAUTHOR 

/* some of amigaos stuff */
#define CSI "\x9b"				/* https://wiki.amigaos.net/wiki/Console_Device */
static BPTR CONSOLE; 			/* console file */
static struct Window *con_Win; 
static UBYTE con_Raw, 			/* == 1 when "*" forced to raw */
			 con_BG,
             con_TxW, 
			 con_TxH, con_Fatal;
static UBYTE *con_Title; 		/* original con_Win title */
static struct TextFont *con_Font, *con_GfxFont, *newFont3(struct TextFont *wrc);
static struct ConUnit *con_Unit;
static UBYTE palette[8];
static UBYTE con_Buf[1024];
static int con_Buf_len;

static void init_console();
static void cleanup();		
static int current_attr;
// extern struct Library *SysBase;	

/* Kick 1.3 compatible SetMode:  https://github.com/alexalkis/sillychess/commit/c8b0c85b30cd53e037e9152af603ff9356ac54dd#diff-e88d359c5b1d713706e952fc902bad7019b1acb1a92a2ee81d8cfbb8b0d97bd7 
*/
static LONG SendPkt13(struct MsgPort * handler, LONG action, LONG arg1, LONG arg2, 
	LONG arg3, LONG arg4, LONG arg5, LONG arg6, LONG arg7)
{
    register struct Process        * proc;
    register struct StandardPacket * packet;
    register LONG                    res1;

    proc = (struct Process *)FindTask(NULL);

    if ((packet = (struct StandardPacket *)AllocMem( sizeof(struct StandardPacket),
                                MEMF_CLEAR | MEMF_PUBLIC)) ) {

        packet->sp_Msg.mn_Node.ln_Name        = (char *)&(packet->sp_Pkt);
        packet->sp_Pkt.dp_Link                = & packet->sp_Msg;
        packet->sp_Pkt.dp_Port                = & proc->pr_MsgPort;
        packet->sp_Pkt.dp_Type                = action;
		packet->sp_Pkt.dp_Arg1                = arg1;
		packet->sp_Pkt.dp_Arg2                = arg2;
		packet->sp_Pkt.dp_Arg3                = arg3;
		packet->sp_Pkt.dp_Arg4                = arg4;
		packet->sp_Pkt.dp_Arg5                = arg5;
		packet->sp_Pkt.dp_Arg6                = arg6;
		packet->sp_Pkt.dp_Arg7                = arg7;
		
        /*
         *        If the user has got a special Packet recieving
         *        routine, call it.
         */
        PutMsg (handler, & packet->sp_Msg);
        if (proc->pr_PktWait) {
            ( * ((struct Message (*) (void)) proc->pr_PktWait) ) ();
        } else {
            WaitPort (& proc->pr_MsgPort);
            GetMsg (& proc->pr_MsgPort);
        }

        /*
         *        Store the secondary result so that the program
         *        can get it with IoErr()
         */

        ((struct CommandLineInterface *)BADDR(proc->pr_CLI))->
                                cli_Result2 = packet->sp_Pkt.dp_Res2;

        res1 = packet->sp_Pkt.dp_Res1;

        FreeMem( (char *)packet, sizeof(struct StandardPacket) );
    } else {        /* No memory */
        res1 = 0;
        ((struct CommandLineInterface *)BADDR(proc->pr_CLI))->
                                cli_Result2 = ERROR_NO_FREE_STORE;
    }

    return(res1);
}

static BOOL SetMode13(BPTR file, LONG mode) /* kick 1.3 version */
{
	struct MsgPort *port = (struct MsgPort *)
               (((struct FileHandle *)BADDR(file))->fh_Type);
	return SendPkt13(port, ACTION_SCREEN_MODE, mode, 0,0,0,0,0,0)!=0;
}

static struct Window* GetWindow(BPTR file, struct ConUnit **unit)
{
    /* http://amigadev.elowar.com/read/ADCD_2.1/AmigaMail_Vol2_guide/node007C.html */
	/* http://amigadev.elowar.com/read/ADCD_2.1/AmigaMail_Vol2_guide/node0065.html */
	if(IsInteractive(file)) {
		struct MsgPort *port = (struct MsgPort *)
               (((struct FileHandle *)BADDR(file))->fh_Type);
		char buf[sizeof(struct InfoData)+3];
		struct InfoData *data = (struct InfoData*)BADDR(MKBADDR(&buf[3]));
		if(SendPkt13(port, 	ACTION_DISK_INFO, MKBADDR(data),0,0,0,0,0,0)) {
			if(unit) *unit = (struct ConUnit*)(data->id_InUse ? 
				((struct IOStdReq *) data->id_InUse)->io_Unit : 
				NULL);
			return (struct Window*)data->id_VolumeNode;
		}
	}
	return NULL;
}

static void open_console( )
{
	atexit(cleanup);

	if(!IsInteractive(Input())) goto fallback;
	if(!IsInteractive(Output())) goto fallback;
	
	CONSOLE = Open((UBYTE*)"*", MODE_OLDFILE);
	if(!CONSOLE) goto fallback;
	if(!IsInteractive(CONSOLE)) goto fallback;
	
	/* Change win title */
	con_Win = GetWindow(CONSOLE, &con_Unit);
	if(con_Win) {
		con_Title = con_Win->Title;
		SetWindowTitles(con_Win, (UBYTE*)TITLE, (void*)-1);
	}
	
	/* change to raw */
	if(SetMode13(CONSOLE, -1)) {
		con_Raw = 1;
	} else {
fallback:
		if(CONSOLE) Close(CONSOLE);
		/* Open raw console directly */
		CONSOLE = Open((UBYTE*)"RAW://640/-1/" TITLE "/CLOSE/SMART", MODE_OLDFILE);
		if(!CONSOLE) /* kick 1.3 version */
		CONSOLE = Open((UBYTE*)"RAW:0/0/640/200/" TITLE, MODE_OLDFILE);
		if(!CONSOLE) fatal("Can't open console"); 
	}
	
	if(!con_Win) con_Win = GetWindow(CONSOLE, &con_Unit);
	if(con_Win) {
		con_TxW = con_Win->RPort->TxWidth;
		con_TxH = con_Win->RPort->TxHeight;
		// if((con_GfxFont = newGfxFont(con_TxW, con_TxH)))
			// con_Font = con_Win->RPort->Font; //IFont;
	}
}

static struct TextFont *delFont3(struct TextFont *tf)
{
	if(tf) {
		UWORD numChar = 1 + tf->tf_HiChar - tf->tf_LoChar;
		UWORD w = tf->tf_XSize;
		UWORD h = tf->tf_YSize;
		UWORD modulo = ((w*numChar + 15)&-16)/8;
		
		FreeMem(tf->tf_CharLoc, 4*numChar); 
		FreeMem(tf->tf_CharData, modulo * h);
		FreeMem(tf, sizeof(struct TextFont));
		
		tf->tf_CharLoc  = NULL;
		tf->tf_CharData = NULL;
		tf = NULL;
	}
	return tf;
}

static struct TextFont *newFont3(struct TextFont *tpl)
{
	static UBYTE font3_data[] = {
	/* 32*/	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	/* 33*/	0x00,0x00,0x20,0x60,0xfe,0x60,0x20,0x00,
	/* 34*/	0x00,0x00,0x08,0x0c,0xfe,0x0c,0x08,0x00,
	/* 35*/	0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,
	/* 36*/	0x80,0x40,0x20,0x10,0x08,0x04,0x02,0x01,
	/* 37*/	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	/* 38*/	0x00,0x00,0x00,0x00,0xff,0x00,0x00,0x00,
	/* 39*/	0x00,0x00,0x00,0xff,0x00,0x00,0x00,0x00,
	/* 40*/	0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x08,
	/* 41*/	0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,
	/* 42*/	0x08,0x08,0x08,0xff,0x00,0x00,0x00,0x00,
	/* 43*/	0x00,0x00,0x00,0x00,0xff,0x08,0x08,0x08,
	/* 44*/	0x08,0x08,0x08,0x08,0x0f,0x08,0x08,0x08,
	/* 45*/	0x10,0x10,0x10,0x10,0xf0,0x10,0x10,0x10,
	/* 46*/	0x10,0x10,0x10,0x10,0x1f,0x00,0x00,0x00,
	/* 47*/	0x00,0x00,0x00,0x1f,0x10,0x10,0x10,0x10,
	/* 48*/	0x00,0x00,0x00,0xf8,0x08,0x08,0x08,0x08,
	/* 49*/	0x08,0x08,0x08,0x08,0xf8,0x00,0x00,0x00,
	/* 50*/	0x10,0x10,0x10,0x10,0x1f,0x20,0x40,0x80,
	/* 51*/	0x80,0x40,0x20,0x1f,0x10,0x10,0x10,0x10,
	/* 52*/	0x01,0x02,0x04,0xf8,0x08,0x08,0x08,0x08,
	/* 53*/	0x08,0x08,0x08,0x08,0xf8,0x04,0x02,0x01,
	/* 54*/	0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
	/* 55*/	0xff,0xff,0xff,0xff,0xff,0x00,0x00,0x00,
	/* 56*/	0x00,0x00,0x00,0xff,0xff,0xff,0xff,0xff,
	/* 57*/	0xf8,0xf8,0xf8,0xf8,0xf8,0xf8,0xf8,0xf8,
	/* 58*/	0x1f,0x1f,0x1f,0x1f,0x1f,0x1f,0x1f,0x1f,
	/* 59*/	0x08,0x08,0x08,0xff,0xff,0xff,0xff,0xff,
	/* 60*/	0xff,0xff,0xff,0xff,0xff,0x08,0x08,0x08,
	/* 61*/	0xf8,0xf8,0xf8,0xf8,0xff,0xf8,0xf8,0xf8,
	/* 62*/	0x1f,0x1f,0x1f,0x1f,0xff,0x1f,0x1f,0x1f,
	/* 63*/	0x1f,0x1f,0x1f,0x1f,0x1f,0x00,0x00,0x00,
	/* 64*/	0x00,0x00,0x00,0x1f,0x1f,0x1f,0x1f,0x1f,
	/* 65*/	0x00,0x00,0x00,0xf8,0xf8,0xf8,0xf8,0xf8,
	/* 66*/	0xf8,0xf8,0xf8,0xf8,0xf8,0x00,0x00,0x00,
	/* 67*/	0x1f,0x1f,0x1f,0x1f,0x1f,0x20,0x40,0x80,
	/* 68*/	0x80,0x40,0x20,0x1f,0x1f,0x1f,0x1f,0x1f,
	/* 69*/	0x01,0x02,0x04,0xf8,0xf8,0xf8,0xf8,0xf8,
	/* 70*/	0xf8,0xf8,0xf8,0xf8,0xf8,0x04,0x02,0x01,
	/* 71*/	0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	/* 72*/	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01,
	/* 73*/	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x80,
	/* 74*/	0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	/* 75*/	0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
	/* 76*/	0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,
	/* 77*/	0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,
	/* 78*/	0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
	/* 79*/	0x00,0xff,0x00,0x00,0x00,0x00,0xff,0x00,
	/* 80*/	0x00,0xff,0x80,0x80,0x80,0x80,0xff,0x00,
	/* 81*/	0x00,0xff,0xc0,0xc0,0xc0,0xc0,0xff,0x00,
	/* 82*/	0x00,0xff,0xe0,0xe0,0xe0,0xe0,0xff,0x00,
	/* 83*/	0x00,0xff,0xf0,0xf0,0xf0,0xf0,0xff,0x00,
	/* 84*/	0x00,0xff,0xf8,0xf8,0xf8,0xf8,0xff,0x00,
	/* 85*/	0x00,0xff,0xfc,0xfc,0xfc,0xfc,0xff,0x00,
	/* 86*/	0x00,0xff,0xfe,0xfe,0xfe,0xfe,0xff,0x00,
	/* 87*/	0x00,0xff,0xff,0xff,0xff,0xff,0xff,0x00,
	/* 88*/	0x00,0x01,0x01,0x01,0x01,0x01,0x01,0x00,
	/* 89*/	0x00,0x80,0x80,0x80,0x80,0x80,0x80,0x00,
	/* 90*/	0x81,0x42,0x24,0x18,0x18,0x24,0x42,0x81,
	/* 91*/	0x08,0x08,0x08,0x08,0xff,0x08,0x08,0x08,
	/* 92*/	0x18,0x3c,0xdb,0x18,0x18,0x18,0x18,0x00,
	/* 93*/	0x18,0x18,0x18,0x18,0xdb,0x3c,0x18,0x00,
	/* 94*/	0x18,0x3c,0xdb,0x18,0xdb,0x3c,0x18,0x00,
	/* 95*/	0xff,0x81,0x81,0x81,0x81,0x81,0x81,0xff,
	/* 96*/	0x3c,0x66,0x06,0x0c,0x18,0x00,0x18,0x00,
	/* 97*/	0xc4,0xa8,0x90,0xc0,0xa0,0x90,0x80,0x00,
	/* 98*/	0x60,0x50,0x48,0x70,0x48,0x50,0x60,0x00,
	/* 99*/	0x10,0x18,0x14,0x92,0x50,0x30,0x10,0x00,
	/*100*/	0x82,0xc6,0xaa,0x92,0xaa,0xc6,0x82,0x00,
	/*101*/	0x82,0xc6,0xaa,0x92,0x82,0x82,0x82,0x00,
	/*102*/	0x94,0xa8,0xd0,0xa0,0xc0,0x80,0x80,0x00,
	/*103*/	0x82,0x44,0x28,0x10,0x28,0x44,0x82,0x00,
	/*104*/	0xc2,0xa2,0xd2,0xaa,0x96,0x8a,0x86,0x00,
	/*105*/	0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x00,
	/*106*/	0x10,0x38,0x54,0x92,0x54,0x38,0x10,0x00,
	/*107*/	0x10,0x10,0x10,0x38,0x54,0x92,0x92,0x00,
	/*108*/	0x10,0x18,0x14,0x12,0x10,0x10,0x10,0x00,
	/*109*/	0xc6,0xaa,0x92,0xaa,0xc6,0x82,0x82,0x00,
	/*110*/	0x90,0x50,0x38,0x14,0x12,0x10,0x10,0x00,
	/*111*/	0xc4,0xac,0xd4,0xa8,0x90,0x80,0x80,0x00,
	/*112*/	0x80,0x80,0x80,0x90,0xa8,0xc4,0x82,0x00,
	/*113*/	0x40,0x40,0x40,0x78,0x44,0x44,0x44,0x00,
	/*114*/	0x60,0x50,0x48,0x50,0x60,0x50,0x48,0x00,
	/*115*/	0x40,0x44,0x4c,0x54,0x64,0x44,0x04,0x00,
	/*116*/	0x10,0x38,0x54,0x92,0x10,0x10,0x10,0x00,
	/*117*/	0x60,0x50,0x48,0x44,0x44,0x44,0x44,0x00,
	/*118*/	0x10,0xba,0x54,0x10,0x10,0x10,0x10,0x00,
	/*119*/	0x60,0x50,0x48,0x50,0x60,0x40,0x40,0x00,
	/*120*/	0x92,0x54,0x38,0x10,0x10,0x10,0x10,0x00,
	/*121*/	0xe0,0xd0,0xa8,0x94,0x9a,0x96,0x92,0x00,
	/*122*/	0x10,0x28,0x44,0x28,0x10,0x28,0x44,0x00,
	/*123*/	0xe7,0xc3,0x24,0xe7,0xe7,0xe7,0xe7,0xff,
	/*124*/	0xe7,0xe7,0xe7,0xe7,0x24,0xc3,0xe7,0xff,
	/*125*/	0xe7,0xc3,0x24,0xe7,0x24,0xc3,0xe7,0xff,
	/*126*/	0xc3,0x99,0xf9,0xf3,0xe7,0xff,0xe7,0xff,
	/*127*/ 0xFF,0x81,0x81,0x81,0x81,0x81,0x81,0xff};
	const UWORD numChar = 127-32+1, w = tpl->tf_XSize, h = tpl->tf_YSize;
	UWORD modulo = ((w*numChar + 15)&-16)/8;
	ULONG *charLoc = AllocMem(4*numChar, MEMF_CLEAR|MEMF_PUBLIC);
	UBYTE *charDat = AllocMem(modulo*h, MEMF_CLEAR|MEMF_PUBLIC);
	struct TextFont *ret = AllocMem(sizeof(struct TextFont), MEMF_CLEAR|MEMF_PUBLIC);
	UBYTE *xPos = malloc(w), *yPos = malloc(h);
	
	if(charLoc && charDat && ret && xPos && yPos) {
		UWORD c;
	
		ret->tf_YSize = h;
		ret->tf_Flags = FPF_DESIGNED|FPF_ROMFONT; // $41
		ret->tf_XSize = w;
		ret->tf_Baseline = tpl->tf_Baseline;
		ret->tf_LoChar = 32;
		ret->tf_HiChar = 126;
		ret->tf_Modulo = modulo;
		ret->tf_CharData = charDat;
		ret->tf_CharLoc  = charLoc;
		ret->tf_Accessors = 1;
		
		for(c=0; c<=w; ++c) xPos[c] = 255>>((c*8)/w);
		for(c=0; c<=h; ++c) yPos[c] = (c*8)/h;
		
		for(c=0; c<numChar; ++c) {
			UWORD j;
			
			charLoc[c] = w*(c*65536+1);
			
			for(j=0; j<h; ++j) {
				UWORD i = c*w;
				UBYTE *d = &charDat[modulo*j + i/8];
				UBYTE m = 128>>(i&7), v = 0;
		
				UWORD j0 = yPos[j];
				UWORD j1 = yPos[j+1] - j0; 
				for(j0 += c*8, j1 += j0 + !j1; j0<j1; ++j0) v |= font3_data[j0];
				
				// v = 0xAA>>(j&1);
				
				for(i=0; i<w; ++i) {
					UBYTE m0 = xPos[i];
					UBYTE m1 = xPos[i+1];
					UBYTE w  = m0 ^ m1; if(!w) w = m0 ^ (m0>>1);
					if(v & w) *d |= m;
					if(!(m>>=1)) ++d, m=128;
				}
			}
		}
		return ret;
	}
	if(xPos)    free(xPos);
	if(yPos)    free(yPos);
	if(ret)     FreeMem(ret, sizeof(struct TextFont));
	if(charDat) FreeMem(charDat, modulo*h);
	if(charLoc) FreeMem(charLoc, 4*numChar);
	return NULL;
}

/* getopt linkages */

extern int optind;
extern char *optarg;
extern ZINT16 default_fg, default_bg;

#ifdef HARD_COLORS
static ZINT16 current_fg = -1;
static ZINT16 current_bg = -1;
#endif

extern int hist_buf_size;
extern int use_bg_color;

/* new stuff for command editing */
int BUFFER_SIZE;
char *commands;
int space_avail;
static int ptr1, ptr2 = 0;
static int end_ptr = 0;
static int row, head_col;
// static int keypad_avail = 1;

/* done with editing global info */

static int current_row = 1;
static int current_col = 1;

static int saved_row;
static int saved_col;

static int cursor_saved = OFF;

// static char cmbuf[1024];
// static char *cmbufp;

static void display_string( char *s );

static void _ctrl_c( ) 
{
	raise(SIGINT);
}

static void _flush()
{
	int len = con_Buf_len;
	UBYTE *buf = con_Buf;
	while(len>0) {
		int i = Write(CONSOLE, buf, len);
		if(i<0) fatal("I/O error");
		buf += i; len -= i;
	}
	con_Buf_len = 0;
	
    if(SetSignal(0L, (SIGBREAKF_CTRL_C|SIGBREAKF_CTRL_D)) &
                     (SIGBREAKF_CTRL_C|SIGBREAKF_CTRL_D))
		_ctrl_c();
}

#if 1
#define dbg(x)	x
#else 
static void dbg(unsigned char c)
{
	if(c==0x9b) fprintf(stderr, "<CSI>");
	else if(c<32) fprintf(stderr, "<%d>", c);
	else fprintf(stderr, "%c", c);
}
#endif

#define _putc(c) do {							\
	dbg(con_Buf[con_Buf_len++] = (c));	\
	if(con_Buf_len == sizeof(con_Buf))	\
		_flush();	 							\
	} while(0)

static void _puts(char *buf)
{
	int i = con_Buf_len;
	while(*buf) {
		dbg(con_Buf[i++] = *buf++);
		if(i == sizeof(con_Buf)) {
			con_Buf_len = i;
			_flush();
			i = 0;
		}
	}
	con_Buf_len = i;
}

static void _putf(char *fmt, ...)
{
	char buf[256];
	va_list argptr;
	
	va_start(argptr,fmt);
	vsprintf(buf,fmt,argptr);
	va_end(argptr);
	
	_puts(buf);
}

static void _cursor(int on)
{
	_puts(on ? CSI "\x20\x70" : CSI "\x30\x20\x70");
	if(con_Win && SysBase->LibNode.lib_Version>=39) {
		if(on)
			SetWindowPointer(con_Win, TAG_DONE);
		else
			SetWindowPointer(con_Win, 
				WA_BusyPointer, TRUE, 
				WA_PointerDelay, TRUE, 
				TAG_DONE);
	}
}

static void _gotoxy(int row, int col)
{
	_putf(CSI "%d\x3B%d\x48", row, col);
}

static void _make_global_bg(int bg)
{
	if(use_bg_color && !monochrome) {
		bg = palette[default_bg = bg];
		con_BG = bg - '0';
		// fprintf(stderr, "glob bg: %c\n", bg);
		if(SysBase->LibNode.lib_Version>=36) {
			_putc(0x9B);
			_putc('>');
			_putc(bg);
			_putc('m');
		}
	}
}

static int _gets(unsigned char *buf, int len)
{
	int i = 0;
	do {
		int j = Read(CONSOLE, buf + i, len);
		if(j<0) fatal("Can't read console");
		i += j;
		len -= j;
	} while(len && WaitForChar(CONSOLE,0));
	// if(*buf==0x9B) {buf[i]=0; fprintf(stderr, "<CSI>%s\n", buf+1);}
	return i;
}

static void _flush_input( )
{
	_flush();
	while(WaitForChar(CONSOLE, 0)) 
		if(Read(CONSOLE, con_Buf, sizeof(con_Buf))<0)
			fatal("Can't flush input buffer");
}

#define MASK2(a,b) 		(((a)<<8)|(b))
#define MASK3(a,b,c)	((MASK2(a,b)<<8)|(c))
#define MASK4(a,b,c,d)	((MASK3(a,b,d)<<8)|(d))

static int get_winsize(int *screen_rows, int *screen_cols )
{
	UBYTE *s = con_Buf; int l,ret=0;
	_flush_input();	_puts(CSI "\x30\x20\x71"); _flush();
	s[l = _gets(s, sizeof(con_Buf)-1)] = 0;
	/* 9B 31 3B 31 3B <bottom margin> 3B <right margin> 72 */
	if(s[0]==0x9B && s[l-1]==0x72 
	&& MASK4(s[1],s[2],s[3],s[4]) == MASK4('1',';','1',';')) {
#define PARSE_INT(k) \
		s += k; l = 0; while(*s>='0' && *s<='9') l = l*10 + (*s++ - '0')
		PARSE_INT(5);
		if(*s==';') {
			if(screen_rows) {
				if(l!=*screen_rows) ret = 1;
				*screen_rows = l;
			}
			PARSE_INT(1);
			if(screen_cols) {
				if(l!=*screen_cols) ret = 1;
				*screen_cols = l;
			}
		}			
	}
	return ret;
}

static void _rst_cursor() {
	UBYTE *s = con_Buf; int l;
	_flush_input();	_puts(CSI "\x36\x6E"); _flush();
	s[l = _gets(s, sizeof(con_Buf)-1)] = 0;
	if(s[0]==0x9B && s[l-1]==0x52) {
		PARSE_INT(1);
		if(*s==';') {
			current_row = l;
			PARSE_INT(1);
			current_col = l;
		}
	}
}

static void _setFont(struct TextFont *tf)
{
	_flush();
	Forbid();
	SetFont(con_Win->RPort, con_Unit->cu_Font = con_Win->IFont = tf);
	Permit();
}

static int _getc(int timeout) 
{
	unsigned char *s = con_Buf; int c=0, l;

	_flush();
	if(timeout && !WaitForChar(CONSOLE,  timeout*100000)) 
		return -1;
		
again:
	/* get all available chars & nul terminate */
	s[l = _gets(s, sizeof(con_Buf)-1)] = 0;
	
	/* check Special Sequence */
	if(s[0]==0x9b && s[l-1]==0x7c) switch(MASK3(s[1],s[2],s[3])) {
		/* mouse click */
		case MASK3('2',';','0'):
			if(s[4]==';' && s[5]=='1' && con_TxW && con_TxH) {
				zword_t	h_mouse = get_word(H_MOUSE_POSITION_OFFSET);
				if(h_mouse > 0) {
					int x = con_Win->GZZMouseX/con_TxW + 1, 
						y = con_Win->GZZMouseY/con_TxH + 1;
					set_word(h_mouse + 2,x);
					set_word(h_mouse + 4,y);
					s[0] = 254; s[1] = 0; 
				}
			}
		break;
		
		/* close window */
		case MASK3('1','1',';'): 
			s[0] = 4; s[1] = 0;
		break;
			
		/* window resied */
		case MASK3('1','2',';'): {
			int old_rows = screen_rows, old_cols = screen_cols;
			int changed = get_winsize(&screen_rows, &screen_cols);
			/* walk arount SMART console not painting bottom & right part when resized */
			if(con_Win && con_Win->RPort) {
				struct RastPort *rp = con_Win->RPort;
				LONG x0 = con_Win->BorderLeft;
				LONG y0 = con_Win->BorderTop;
				LONG x1 = con_Win->Width  - con_Win->BorderRight - 1;
				LONG y1 = con_Win->Height - con_Win->BorderBottom - 1;
				_cursor(0); _flush();
				SetAPen(con_Win->RPort, con_BG);
				if(screen_rows>=old_rows && y0 + old_rows*con_TxH<=y1) 
					RectFill(rp, x0, y0 + old_rows*con_TxH, x1,y1);
				if(screen_cols>=old_cols && x0 + old_cols*con_TxW<=x1) 
					RectFill(rp, x0 + old_cols*con_TxW, y0, x1,y1);
				_cursor(1); _flush();
			}

			if(changed) {
				_rst_cursor();				
				set_byte( H_SCREEN_ROWS, screen_rows ); /* Screen dimension in characters */
				set_byte( H_SCREEN_COLUMNS, screen_cols );
				set_byte( H_SCREEN_RIGHT, screen_cols );
				set_byte( H_SCREEN_BOTTOM, screen_rows );
				if(h_type < V4)	z_show_status();
				// recup coord vraies du curseur
				s[0] = '\n'; s[1] = 0;
			} else goto again;
		break; }
	}
	
	/* pack all bytes into an int */
	while(*s) c = (c<<8) | *s++;
	
	/* framework expects \r */
	if(c=='\n') c='\r';
	
	/* ctrl-c / ctrl-d ==> bye */
	if(c==3 || c==4) _ctrl_c();
	
	return c;
}

#if 1
typedef long int fxfp;
#define fxfp_bits	13
static fxfp fxmul(fxfp a, fxfp b)
{
	return (a*b)>>fxfp_bits;
}
#define fx(x)	(fxfp)((x)*(1<<fxfp_bits)+.5)
#else
typedef double fxfp;
#define fxmul(x,y)	((x)*(y))
#define fx(x)		(x)
#endif

static fxfp __attribute__((regparm(2))) _col_dist(UWORD c1, UWORD c2)
{
/*
 perl -e 'for($i=0;$i<16;++$i) {$x = $i/15; if($x<.04045) {$y = $x/12.92;} else {$y = (($x + .055)/1.055)**2.4;} print $y,", ";}'
*/
	static fxfp lvl[] = {
		fx(0), 
		fx(0.00560539162420272), fx(0.0159962933655096), fx(0.0331047665708851), 
		fx(0.0578054301910672), fx(0.0908417111834077), fx(0.132868321553818),
		fx(0.184474994500441), fx(0.246201326707835), fx(0.318546778125092),
		fx(0.401977779832196), fx(0.49693299506087), fx(0.603827338855338),
		fx(0.723055128921969), fx(0.854992608124234), fx(1)};

	fxfp r1 = lvl[(c1&0xF00)>>8];
	fxfp g1 = lvl[(c1&0x0F0)>>4];
	fxfp b1 = lvl[(c1&0x00F)>>0];

	fxfp r2 = lvl[(c2&0xF00)>>8];
	fxfp g2 = lvl[(c2&0x0F0)>>4];
	fxfp b2 = lvl[(c2&0x00F)>>0];
	
	fxfp d = 0, b =0, t;
	t = r1 - r2; b += fxmul(t,fx(.2126)); d += fxmul(t,t);
	t = g1 - g2; b += fxmul(t,fx(.7152)); d += fxmul(t,t);
	t = b1 - b2; b += fxmul(t,fx(.0722)); d += fxmul(t,t);
	//d += fxmul(fx(3),fxmul(b,b));
	
	return d;
}

struct best_pal {
	fxfp d;
	UWORD colors[8];
	fxfp dist_mat[8][8];
};

static void __attribute__((regparm(2))) _find_rec(struct best_pal *bp, UWORD i)
{
	UWORD j;
	fxfp t = 0;
	for(j=i; j<8; ++j) t += bp->dist_mat[j][bp->colors[j]];
	if(bp->d<0 || t<bp->d) {
		if(--i) {
			UWORD u = bp->colors[i];
			for(j=i+1; j--;) {
				bp->colors[i] = bp->colors[j]; 
				bp->colors[j] = u;
				_find_rec(bp, i);
				bp->colors[j] = bp->colors[i];
			}
			bp->colors[i] = u; 
		} else {
			t += bp->dist_mat[0][bp->colors[0]];
			if(bp->d<0 || t<bp->d) {
				bp->d = t;
				for(j=8; j--;) palette[j] = '0' + bp->colors[j];
			}
		}
	}
}

static void _find_best_palette()
{
	struct ColorMap *cm;
	
	if(con_Win 
	&& con_Win->WScreen 
	&& (cm=con_Win->WScreen->ViewPort.ColorMap)) {
		struct best_pal bp;
		UWORD i, j, m = (1u<<con_Win->WScreen->BitMap.Depth) - 1;
		for(i=8;i--;) bp.colors[i] = GetRGB4(cm, i & m);
		for(i=8;i--;) {
			UWORD c = 0;
			if(i&1) c|= 0xB00;
			if(i&2) c|= 0x0B0;
			if(i&4) c|= 0x00B;
			for(j=8; j--;) bp.dist_mat[i][j] = _col_dist(c, bp.colors[j]);
		}
		for(i=8;i--;) bp.colors[i] = i;
		bp.d = -1; _find_rec(&bp, 8);
		// for(i=0; i<8; ++i) fprintf(stderr, " %d:%03X", palette[i]-'0', GetRGB4(cm, palette[i]-'0')); fprintf(stderr, "\n");
	} else {
		UWORD i;
		for(i=8;i--;) palette[i] = '0' + i;
	}
}

/* Zcolors:
 * BLACK 0   BLUE 4   GREEN 2   CYAN 6   RED 1   MAGENTA 5   BROWN 3   WHITE 7
 * ANSI Colors (foreground over background):
 * BLACK 30  BLUE 34  GREEN 32  CYAN 36  RED 31  MAGENTA 35  BROWN 33  WHITE 37
 * BLACK 40  BLUE 44  GREEN 42  CYAN 46  RED 41  MAGENTA 45  BROWN 43  WHITE 47
 */
void set_colours( zword_t foreground, zword_t background )
{
	int fg = 1, bg = 0;
	
	/* Translate from Z-code colour values to natural colour values */
	if ( ( ZINT16 ) foreground >= 1 && ( ZINT16 ) foreground <= 9 )
		fg = ( foreground == 1 ) ? default_fg : foreground - 2;
	if ( ( ZINT16 ) background >= 1 && ( ZINT16 ) background <= 9 )
		bg = ( background == 1 ) ? default_bg : background - 2;

	if(!monochrome) {
		char buf[8], *s = buf; *s=0;
		/* Set foreground and background colour */
		if(fg != current_fg) {
			current_fg = ( ZINT16 ) fg;
			*s++ = 0x9B;
			*s++ = '3';
			*s++ = palette[fg];
		}
		if ( use_bg_color && bg != current_bg ) {
			current_bg = ( ZINT16 ) bg;
			*s++ = buf[0] ? ';' : 0x9B;			
			*s++ = '4';
			*s++ = palette[bg];
		} 
		if(buf[0]) {
			*s++ = 'm';
			*s   = 0;
			_puts(buf);
		}
		// fprintf(stderr, "bg=%d->%c, fg=%d->%c // %d %d %s mono=%d\n", bg, palette[bg], fg, palette[fg],foreground, background, buf[0]?buf+1:"?", monochrome);
	}
	//	_putf(CSI "3%d;4%dm", palette[foreground&7], palette[background&7]);
}

static void init_console()
{
	_puts("\033c");
	_puts(CSI "2;12;11{");     /* report mouse click & window resize & close */
	_puts(CSI "0m"); /* default color */
	_puts(CSI "\x30\x20\x70"); /* cursor_off */
	_puts(CSI "\x3E\x31\x6C"); /* scroll_off */
	_puts(CSI "\x3F\x37\x6C"); /* autowrap_off */
	_puts(CSI "\x32\x30\x68"); /* \n=chr(13) chr(10) */
}

static void exit_console()
{
	_puts(CSI "2;12;11}");     /* report mouse click & window resize & close */
	_puts(CSI "0m"); /* default color */
	_puts(CSI "\x32\x30\x68"); /* \n=chr(13) chr(10) */
	_puts(CSI "\x3F\x37\x68"); /* autowrap on */
	_puts(CSI "\x3E\x31\x68"); /* scroll on */
	_puts(CSI "\x20\x70"); /* cursor on */
}

static void cleanup( )
{
	_cursor(1);
	if(con_Font) {
		_setFont(con_Font); con_Font = NULL;
		delFont3(con_GfxFont); con_GfxFont = NULL;
	}
	if(con_Win && con_Title) 
		SetWindowTitles(con_Win, con_Title, (void*)-1);
	con_Win = NULL;
	if(CONSOLE) {
		exit_console();
		if(!con_Fatal)	_puts("\033c"); else _gotoxy(screen_rows,1);
		_flush();
		if(con_Raw) SetMode13(CONSOLE,0);
		Close(CONSOLE); CONSOLE=0;
	}
}

void initialize_screen(  )
{
	int row, col;

	open_console();
	init_console();

	/* window size */
	get_winsize(screen_rows?NULL:&screen_rows, screen_cols?NULL:&screen_cols);
	if(!screen_cols) screen_cols = DEFAULT_COLS;
	if(!screen_rows) screen_rows = DEFAULT_ROWS;
	// if(screen_rows<) _putf(CSI "%du", screen_rows);
	// if(screen_cols) _putf(CSI "%dt", screen_cols);

	// fprintf(stderr," %d %d\n", screen_cols, screen_rows);
	//_putf(CSI "%d\x74\x9B%d\x75", screen_rows, screen_cols);


	// _puts("\033c");
	clear_screen(  );
	row = screen_rows / 2 - 1;
	col = ( screen_cols - ( sizeof ( JZIPVER ) - 1 ) ) / 2;
	move_cursor( row, col );
	display_string( JZIPVER );
	row = screen_rows / 2;
	col = ( screen_cols - ( sizeof ( "The story is loading..." ) - 1 ) ) / 2;
	move_cursor( row, col );
	display_string( "The story is loading..." );
	move_cursor( screen_rows, 1 );
	_cursor(1); _flush();_cursor(0);
   
	if(con_Win && con_Unit)
		con_GfxFont = newFont3(con_Font = con_Win->RPort->Font); //IFont;

	/* find best palette if not already set */ 
	_find_best_palette();
	
#if defined HARD_COLORS
	current_fg = current_bg = -1;
	if ( default_fg == 9 ) {
		UWORD i;
		default_fg = 0;
		for(i=8; i--;) if(palette[i] == '1') default_fg = i;
		// fprintf(stderr, "default_fg=%d\n", default_fg);
	}
	if(monochrome && default_bg<0) {
		use_bg_color = 0;
		monochrome = 0;
		set_colours(1,1); 
		monochrome = 1;
		con_BG = 0; 
	} else if(monochrome) {
		monochrome = 0;
		set_colours(1,1);  _make_global_bg(current_bg);
		monochrome = 1;
	} else if ( default_bg < 0 ) {
		use_bg_color = 0;
		set_colours(1,1);
		default_fg = current_fg;
		con_BG = default_bg = 0; 
	} else {
		// fprintf(stderr, "default fg/bg %d %d\n", default_fg, default_bg);
		set_colours(1,1); _make_global_bg(current_bg);
	}
	default_bg = current_bg; default_fg = current_fg;
	// fprintf(stderr, "default fg/bg %d %d mono=%d\n", default_fg, default_bg, monochrome);
#endif

	h_interpreter = INTERP_AMIGA;
	
	commands = ( char * ) malloc( hist_buf_size * sizeof ( char ) );
	
	if ( commands == NULL )
	fatal( "initialize_screen(): Could not allocate history buffer!" );
	BUFFER_SIZE = hist_buf_size;
	space_avail = hist_buf_size - 1;
	
	interp_initialized = 1;
}                               /* initialize_screen */

void restart_screen(  )
{
	zbyte_t high = 1, low = 0;
	
	cursor_saved = OFF;
	
	set_byte( H_STANDARD_HIGH, high );
	set_byte( H_STANDARD_LOW, low );
	if ( h_type < V4 )
		set_byte( H_CONFIG, ( get_byte( H_CONFIG ) | CONFIG_WINDOWS ) );
	else {
		/* turn stuff on */
		set_byte( H_CONFIG,
				( get_byte( H_CONFIG ) | CONFIG_BOLDFACE | CONFIG_EMPHASIS | CONFIG_FIXED |
				CONFIG_TIMEDINPUT ) );
#if defined HARD_COLORS
		if(!monochrome)
		set_byte( H_CONFIG, ( get_byte( H_CONFIG ) | CONFIG_COLOUR ) );
#endif
		/* turn stuff off */
		set_byte( H_CONFIG, ( get_byte( H_CONFIG ) & ~CONFIG_PICTURES & ~CONFIG_SFX ) );
	}

	/* Force graphics off as we can't do them */
	set_word( H_FLAGS, ( get_word( H_FLAGS ) & ( ~GRAPHICS_FLAG ) ) );
}                               /* restart_screen */

void reset_screen(  )
{
   /* only do this stuff on exit when called AFTER initialize_screen */
   if ( interp_initialized )
   {
      delete_status_window(  );
      select_text_window(  );
      set_attribute( NORMAL );

	  _flush();
	  interp_initialized = 0;
   }
}                               /* reset_screen */

void clear_screen(  )
{	
 // fprintf(stderr, "clear_screen: %d %d", current_bg, default_bg);
	_make_global_bg(current_bg>=0 ? current_bg : default_bg); _putc(12); _flush();
	current_row = 1;
	current_col = 1;
}                               /* clear_screen */

void select_status_window(  )
{
   save_cursor_position(  );
}                               /* select_status_window */

void select_text_window(  )
{

   restore_cursor_position(  );

}                               /* select_text_window */

void create_status_window(  )
{
}                               /* create_status_window */

void delete_status_window(  )
{
}                               /* delete_status_window */

void clear_line(  )
{
	_make_global_bg(current_bg); _puts(CSI "\x4B"); 
}                               /* clear_line */

void clear_text_window( void)
{
	int row, col;
	get_cursor_position( &row, &col );
	move_cursor(status_size+1,1); 
	_make_global_bg(current_bg); _puts(CSI "\x4A"); /* Erase in Display	(only to end of display) */
	move_cursor( row, col );
}                               /* clear_text_window */

void clear_status_window(  )
{
   int i, row, col;

   get_cursor_position( &row, &col );

   for ( i = status_size; i; i-- )
   {
      move_cursor( i, 1 );
      clear_line(  );
   }

   move_cursor( row, col );

}                               /* clear_status_window */

void move_cursor( row, col )
   int row;
   int col;
{
	// if(con_Unit) current_row = con_Unit->cu_YCCP, current_col = con_Unit->cu_XCCP;
	if(col > screen_cols) col = screen_cols;
	if(row > screen_rows) row = screen_rows;
	// fprintf(stderr, "gxy -> %d %d (%d %d)", row, col, current_row, current_col);
	if(row==current_row) {
		int i = col - current_col;
		if(col==1) 
			_putc('\r');
		else switch(i) {
			case  0: break;
			case  1: _puts(CSI "\x43"); break;
			case -1: _putc(8); break; //_puts(CSI "\x44"); break;
			default:
			if(i>0) _putf(CSI "%d\x43", i);
			else    _putf(CSI "%d\x44", -i);
		}
	} else if(col==1) {
		int i = row - current_row;
		switch(i) {
			case  1: _putc('\n'); break; //CSI "\x45"); break;
			case -1: _puts(CSI "\x46"); break;
			default:
			if(i>0) _putf(CSI "%d\x45", i);
			else    _putf(CSI "%d\x46", -i);
		}
	} else if(col==current_col) {
		int i = row - current_row;
		switch(i) {
			case  1: _puts(CSI "\x42"); break;
			case -1: _puts(CSI "\x41"); break;
			default:
			if(i>0) _putf(CSI "%d\x42", i);
			else    _putf(CSI "%d\x41", -i);
		}
	} else {
		_gotoxy(row, col);
	}
	current_row = row;
	current_col = col;
}                               /* move_cursor */

void get_cursor_position( row, col )
   int *row;
   int *col;
{
	// if(con_Unit) current_row = con_Unit->cu_YCCP, current_col = con_Unit->cu_XCCP;
	*row = current_row;
	*col = current_col;
}                               /* get_cursor_position */

void save_cursor_position(  )
{
   if ( cursor_saved == OFF )
   {
      get_cursor_position( &saved_row, &saved_col );
      cursor_saved = ON;
   }
}                               /* save_cursor_position */

void restore_cursor_position(  )
{
   if ( cursor_saved == ON )
   {
      move_cursor( saved_row, saved_col );
      cursor_saved = OFF;
   }
}                               /* restore_cursor_position */

void set_attribute( attribute )
   int attribute;
{
	current_attr = attribute;
	// fprintf(stderr, "set_attribute(%d)\n", attribute);
#if defined HARD_COLORS
	if ( attribute == NORMAL ) {
		int bak = monochrome, bg = current_bg, fg = current_fg;
		_puts(CSI "0m" ); 
		/* as 0m resets the cols, set them back as well to default */
		// fprintf(stderr, "NORMAL");
		monochrome = 0;
		current_bg = current_fg = -1;
		// fprintf(stderr, "<bg hack>");
		set_colours(fg + 2, bg + 2);
		monochrome = bak;
	}
#else
	// fprintf(stderr,"attribute:%d %d %d %d\n", attribute,attribute&REVERSE,attribute&BOLD,attribute&EMPHASIS);
	if ( attribute == NORMAL ) _puts(CSI "0m");
#endif
	if ( attribute & REVERSE ) _puts(CSI "7m");
	if ( attribute & BOLD    ) _puts(CSI "1m");
	if ( attribute & EMPHASIS) _puts(CSI "4m");
}                               /* set_attribute */

static void display_string( s )
   char *s;
{
   while ( *s )
      display_char( *s++ );

}                               /* display_string */

static UBYTE gfx_font = 0;

void display_char( int c )
{
	static UBYTE old;
	if(gfx_font != old && con_Font) {
		if(old      == GRAPHICS_FONT) _setFont(con_Font);
		if(gfx_font == GRAPHICS_FONT) _setFont(con_GfxFont);
		old = gfx_font;
	}
	
	if(gfx_font==GRAPHICS_FONT && !con_Font) {
		/* https://www.inform-fiction.org/zmachine/standards/z1point1/sect16.html */
		static UBYTE conv[] = {
			' ','<','>','/',
			'\\',' ','-','-',
			'|','|','+','+',
			'+','+','+','+',
			'+','+','+','+',
			'+','+','#','#',
			'#','#','#','#',
			'#','#','#','#',
			'#','#','#','#',
			'#','#','#',' ',
			' ',' ',' ',0xAF,
			'_','|','|','.', 
			':','-','=','+',
			'*','#','%','@',
			'|','|','X','+',
			0xEE,'j','I','O',
			'?','K','B',0xA7,
			0xD8,'M',0xFC,'X',
			'N','|',0xA4,'+',
			'+','+','+','+',
			'+','h','R','N',
			0xEE,'n',0xA5,'P',
			0xA5,'+','x','+',
			'+','+','+'
		};
		c = c>=32 && c<=126 ? conv[c-32] : '+';
		// switch(c) {
			// case 179: c = '|'; break; /* (ASCII 124) */
			// case 186: c = '#'; break; /* (ASCII 35) */
			// case 196: c = '-'; break; /* (ASCII 45) */
			// case 205: c = '='; break; /* (ASCII 61) */
			// default:  c = c>=32 && c<=126 ? conv[c-32] : '+'; break; /* (ASCII 43) */
		// }
	}
	
	switch(c) {
		case 7: break;
		case 8: if(current_col) --current_col; break;
		case 10: current_col = 1;
			if(++current_row > screen_rows)	current_row = screen_rows;
			break;
		case 13: current_col = 1; break;
		case 12: current_col = current_row = 1; break;
		default:
			if (++current_col > screen_cols ) {
				current_col = screen_cols;
				return;
			}
			break;
	}

	_putc( c );
}                               /* display_char */

void scroll_line( )
{
	int row, col;
	
	get_cursor_position( &row, &col );
	
	if (row < screen_rows ) {
		display_char( '\n' );
	} else{
		move_cursor( status_size + 1, 1 );
		/* Delete Line: Remove current line, move all lines 
		up one position to fill gap, blank bottom line */
		_puts(CSI "\x4D"); 
		move_cursor( row, 1 );
	}
	
	_flush();
}                               /* scroll_line */

int display_command( char *buffer )
{
   int counter, loop;

   move_cursor( row, head_col );
   clear_line(  );
   move_cursor( row, head_col );
   /* ptr1 == end_ptr when the player has selected beyond any previously
    * saved command */
   if ( ptr1 == end_ptr )
      return 0;
   else
   {
      counter = 0;
      for ( loop = ptr1; loop <= ptr2; loop++ )
      {
         buffer[counter] = commands[loop];
         display_char( buffer[counter++] );
      }
      return counter;
   }
}

void get_prev_command( void )
{
   if ( ptr1 > 0 )
   {
      ptr2 = ptr1 -= 2;
      if ( ptr1 < 0 )
         ptr1 = 0;
      if ( ptr2 < 0 )
         ptr2 = 0;
      if ( ptr1 > 0 )
      {
         do
            ptr1--;
         while ( ( commands[ptr1] != '\n' ) && ( ptr1 >= 0 ) );
         ptr1++;
      }
   }
}

void get_next_command( void )
{
   if ( ptr2 < end_ptr )
   {
      ptr1 = ptr2 += 2;
      if ( ptr2 >= end_ptr )
         ptr1 = ptr2 = end_ptr;
      else
      {
         do
            ptr2++;
         while ( ( commands[ptr2] != '\n' ) && ( ptr2 <= end_ptr ) );
         ptr2--;
      }
   }
}

void get_first_command( void )
{
   if ( end_ptr > 1 )
   {
      ptr1 = ptr2 = 0;
      do
         ptr2++;
      while ( ( commands[ptr2] != '\n' ) && ( ptr2 <= end_ptr ) );
      ptr2--;
   }

}

void delete_command( void )
{
   int loop;

   do
   {
      for ( loop = 0; loop < end_ptr; loop++ )
      {
         commands[loop] = commands[loop + 1];
      }
      end_ptr--;
      space_avail++;
   }
   while ( commands[0] != '\n' );
   for ( loop = 0; loop < end_ptr; loop++ )
   {
      commands[loop] = commands[loop + 1];
   }
   end_ptr--;
   space_avail++;
   ptr1 = ptr2 = end_ptr;
}

void add_command( char *buffer, int size )
{
   int loop, counter;

   counter = 0;
   for ( loop = end_ptr; loop < ( end_ptr + size ); loop++ )
   {
      commands[loop] = buffer[counter++];
   }
   end_ptr += size + 1;
   ptr1 = ptr2 = end_ptr;
   commands[end_ptr - 1] = '\n';
   space_avail -= size + 1;
}

int input_character( int timeout )
{
	int c;

	_cursor(1);
	switch(c = _getc(timeout)) {
		case MASK2(0x9b,'A'):	/* Up arrow */
			c = 129; break;
		case MASK2(0x9b,'B'):	/* Dn arrow */
			c = 130; break;
		case MASK2(0x9b,'D'): /* Left arrow */
			c = 131; break;
		case MASK2(0x9b,'C'): /* Right arrow */
            c = 132; break;
		case '\n':
			c = '\r'; break;
	}
	_cursor(0);
	return ( c );
}                               /* input_character */

int input_line( int buflen, char *buffer, int timeout, int *read_size )
{
	int c, col;
	int init_char_pos, curr_char_pos;
	int loop, tail_col;
	
	/*
     * init_char_pos : the initial cursor location
	 * curr_char_pos : the current character position within the input line
     * head_col: the head of the input line (used for cursor position)
     *  (global variable)
     * tail_col: the end of the input line (used for cursor position)
     */

	get_cursor_position( &row, &col );
	head_col = tail_col = col;

	init_char_pos = curr_char_pos = *read_size;

	ptr1 = ptr2 = end_ptr;
	_cursor(1); _flush();
	for ( ;; ) {
		/* Read a single keystroke */
		c = _getc(timeout);  if(c==-1) return c;

		/****** Previous Command Selection Keys ******/
		switch(c) {
			case MASK2(0x9b,'A'):	/* Up arrow */
			get_prev_command(  );
            curr_char_pos = *read_size = display_command( buffer );
            tail_col = head_col + *read_size;
			break;
			
			case MASK2(0x9b,'B'): /* Down arrow */
			get_next_command(  );
            curr_char_pos = *read_size = display_command( buffer );
            tail_col = head_col + *read_size;
            break;
			
			case MASK3(0x9b,'4','1'): /* Pgup */
			case MASK2(0x9b,'T'):   /* shift-up */
            get_first_command();
			curr_char_pos = *read_size = display_command(buffer);
			tail_col = head_col + *read_size;
			break;
			
			case MASK3(0x9b,'4','2'): /* PgDn */
			case MASK2(0x9b,'S'):   /* shift down */
			ptr1 = ptr2 = end_ptr;
            curr_char_pos = *read_size = display_command(buffer);
            tail_col = head_col + *read_size;
			break;
			
			case MASK2(0x9b,'D'): /* Left arrow */
            get_cursor_position( &row, &col );
            /* Prevents moving the cursor into the prompt */
            if ( col > head_col ) {
				move_cursor( row, --col );
				curr_char_pos--;
            }
			break;
			
			case MASK2(0x9b,'C'): /* Right arrow */
            get_cursor_position( &row, &col );
			/* Prevents moving the cursor beyond the end of the input line */
			if ( col < tail_col ) {
				move_cursor( row, ++col );
				curr_char_pos++;
            }
            break;
			
			case MASK3(0x9b,'4','5'): /* End */
			case MASK3(0x9b,' ','@'):   /* shift right */
			move_cursor(row, tail_col);
            curr_char_pos = init_char_pos + *read_size;
			break;
			
			case MASK3(0x9b,'4','4'): /* Home */
			case MASK3(0x9b,' ','A'):   /* shift left */
			move_cursor(row, head_col);
            curr_char_pos = init_char_pos;
			break;
			
			case 0x7F:
			case MASK2(0x9b,'P'):  /* delete */
            if (curr_char_pos < *read_size) {
				get_cursor_position (&row, &col);
				for (loop = curr_char_pos; loop < *read_size; loop++) {
					buffer[loop] = buffer[loop + 1];
				}
				--tail_col; --*read_size;
				for (loop = curr_char_pos; loop < *read_size; loop++) {
					display_char (buffer[loop]);
				}
				display_char (' ');
                move_cursor(row, col);
            }
			break;

			case '\b' :       /* Backspace */
            get_cursor_position( &row, &col );
            if ( col > head_col ) {
               move_cursor( row, --col );
               for ( loop = curr_char_pos; loop < *read_size; loop++ ) {
                  buffer[loop - 1] = buffer[loop];
                  display_char( buffer[loop - 1] );
               }
               display_char( ' ' );
               curr_char_pos--;
               tail_col--;
               ( *read_size )--;
               move_cursor( row, col );
            }
			break;
			
			default:
			/* Normal key action */
            if ( *read_size == ( buflen - 1 ) || c>=254  || ( c<32 && c!='\r' && c!='\n')) {
               /* Ring bell if buffer is full */
               _putc('\7'); 
            } else {
               /* Scroll line if return key pressed */
               if ( c == '\r' || c == '\n' ) {
                  c = '\n';
                  scroll_line(  );
               }

               if ( c == '\n' ) {
                  /* Add the current command to the command buffer */
                  if ( *read_size > space_avail ) {
                     do
                        delete_command(  );
                     while ( *read_size > space_avail );
                  }
                  if ( *read_size > 0 )
                     add_command( buffer, *read_size );

                  /* Return key if it is a line terminator */
                  return ( c );
               } else {
                  get_cursor_position( &row, &col );

                  /* Used if the cursor is not at the end of the line */
                  if ( col < tail_col ) {
                     /* Moves the input line one character to the right */
                     for ( loop = *read_size; loop >= curr_char_pos; loop-- ) {
                        buffer[loop + 1] = buffer[loop];
                     }

                     /* Puts the character into the space created by the
                      * "for" loop above */
                     buffer[curr_char_pos] = ( char ) c;

                     /* Increment the end of the line values */

                     ( *read_size )++;
                     tail_col++;

                     /* Move the cursor back to its original position */

                     move_cursor( row, col );

                     /* Redisplays the input line from the point of
                      * insertion */

                     for ( loop = curr_char_pos; loop < *read_size; loop++ ) {
                        display_char( buffer[loop] );
                     }

                     /* Moves the cursor to the next position */

                     move_cursor( row, ++col );
                     curr_char_pos++;
                  } else {
                     /* Used if the cursor is at the end of the line */
                     buffer[curr_char_pos++] = ( char ) c;
                     display_char( c );
                     ( *read_size )++;
                     tail_col++;
                  }
               }
            }
		}
	}
}
                             /* input_line */
#if 0
static void rundown(  )
{
   unload_cache(  );
   close_story(  );
   close_script(  );
   reset_screen(  );
}                               /* rundown */
#endif

#if 0
static void set_cbreak_mode( mode )
   int mode;
{
	_flush();
	/* TODO DopPacket to go back & forth RAW: */
	
   if ( mode )
   {
      signal( SIGINT, rundown );
      signal( SIGTERM, rundown );
   }

   if ( mode == 0 )
   {
      signal( SIGINT, SIG_DFL );
      signal( SIGTERM, SIG_DFL );
   }
}                               /* set_cbreak_mode */
#endif

/*
 * codes_to_text
 *
 * Translate Z-code characters to machine specific characters. These characters
 * include line drawing characters and international characters.
 *
 * The routine takes one of the Z-code characters from the following table and
 * writes the machine specific text replacement. The target replacement buffer
 * is defined by MAX_TEXT_SIZE in ztypes.h. The replacement text should be in a
 * normal C, zero terminated, string.
 *
 * Return 0 if a translation was available, otherwise 1.
 *
 *  International characters (0x9b - 0xa3):
 *                                        
 *  0x9b a umlaut (ae)                    
 *  0x9c o umlaut (oe)
 *  0x9d u umlaut (ue)
 *  0x9e A umlaut (Ae)
 *  0x9f O umlaut (Oe)
 *  0xa0 U umlaut (Ue)
 *  0xa1 sz (ss)     
 *  0xa2 open quote (>>)
 *  0xa3 close quota (<<)
 *                      
 *  Line drawing characters (0xb3 - 0xda):
 *                                       
 *  0xb3 vertical line (|)               
 *  0xba double vertical line (#)
 *  0xc4 horizontal line (-)    
 *  0xcd double horizontal line (=)
 *  all other are corner pieces (+)
 *                                
 */
int codes_to_text( int c, char *s )
{	
   if ( c > 154 && c < 224 )
   {
      if (c == 220 || c == 221) return 1; /* oe OE */
      *s++ = zscii2latin1[c - 155];
	  *s   = '\0';
      return 0;
   }
   return 1;
}                               /* codes_to_text */

void set_font( int font_type )
{
	gfx_font = font;
}                               /* set_font */

void file_cleanup( const char *file_name, int flag )
{
   UNUSEDVAR( file_name );
   UNUSEDVAR( flag );
}      

int fit_line( const char *line_buffer, int pos, int max )
{
   UNUSEDVAR( line_buffer );

   return ( pos < max );
} 

int print_status( int argc, char *argv[] )
{
   UNUSEDVAR( argc );
   UNUSEDVAR( argv );

   return ( FALSE );
}               

void sound( int argc, zword_t * argv )
{
   UNUSEDVAR( argc );
   UNUSEDVAR( argv );
}

void fatal( const char *s )
{
	con_Fatal = TRUE;
	reset_screen(  );
	fprintf( stderr, "\nFatal error: %s (PC = 0x%08lX)\n", s, pc );
#ifdef DEBUG_TERPRE
	fprintf( stdout, "\nFatal error: %s (PC = 0x%08lX)\n", s, pc );
#endif
	exit( 1 );
}                               /* fatal */

#ifdef STRICTZ

/* Define stuff for stricter Z-code error checking, for the generic
 * Unix/DOS/etc terminal-window interface. Feel free to change the way
 * player prefs are specified, or replace report_zstrict_error()
 * completely if you want to change the way errors are reported. 
 */

/* There are four error reporting modes: never report errors;
 * report only the first time a given error type occurs; report
 * every time an error occurs; or treat all errors as fatal
 * errors, killing the interpreter. I strongly recommend
 * "report once" as the default. But you can compile in a
 * different default by changing the definition of
 * STRICTZ_DEFAULT_REPORT_MODE. In any case, the player can
 * specify a report mode on the command line by typing "-s 0"
 * through "-s 3". 
 */

#define STRICTZ_REPORT_NEVER  (0)
#define STRICTZ_REPORT_ONCE   (1)
#define STRICTZ_REPORT_ALWAYS (2)
#define STRICTZ_REPORT_FATAL  (3)

#define STRICTZ_DEFAULT_REPORT_MODE STRICTZ_REPORT_NEVER

static int strictz_report_mode;
static int strictz_error_count[STRICTZ_NUM_ERRORS];

#endif /* STRICTZ */

/*
 * process_arguments
 *
 * Do any argument preprocessing necessary before the game is
 * started. This may include selecting a specific game file or
 * setting interface-specific options.
 *
 */

void process_arguments( int argc, char *argv[] )
{
   int c, errflg = 0;
   int infoflag = 0;
   int size;
   int expected_args;
   int num;
   
#ifdef STRICTZ
   /* Initialize the STRICTZ variables. */

   strictz_report_mode = STRICTZ_DEFAULT_REPORT_MODE;

   for ( num = 0; num < STRICTZ_NUM_ERRORS; num++ )
   {
      strictz_error_count[num] = 0;
   }
#endif /* STRICTZ */

   /* Parse the options */

   monochrome = 0;
   hist_buf_size = 1024;
   bigscreen = 0;

#ifdef STRICTZ                  
#if defined OS2 || defined __MSDOS__ 
#define GETOPT_SET "gbomvzhy?l:c:k:r:t:s:" 
#elif defined TURBOC            
#define GETOPT_SET   "bmvzhy?l:c:k:r:t:s:" 
#elif defined HARD_COLORS       
#define GETOPT_SET    "mvzhy?l:c:k:r:t:s:f:b:" 
#else 
#define GETOPT_SET    "mvzhy?l:c:k:r:t:s:" 
#endif 
#else 
#if defined OS2 || defined __MSDOS__ 
#define GETOPT_SET "gbomvzhy?l:c:k:r:t:" 
#elif defined TURBOC            
#define GETOPT_SET   "bmvzhy?l:c:k:r:t:" 
#elif defined HARD_COLORS       
#define GETOPT_SET    "mvzhy?l:c:k:r:t:f:b:" 
#else 
#define GETOPT_SET    "mvzhy?l:c:k:r:t:" 
#endif 
#endif 
   while ( ( c = getopt( argc, argv, GETOPT_SET ) ) != EOF )
   {                            
      switch ( c )
      {
         case 'l':             /* lines */
            screen_rows = atoi( optarg );
            break;
         case 'c':             /* columns */
            screen_cols = atoi( optarg );
            break;
         case 'r':             /* right margin */
            right_margin = atoi( optarg );
            break;
         case 't':             /* top margin */
            top_margin = atoi( optarg );
            break;
         case 'k':             /* number of K for hist_buf_size */
            size = atoi( optarg );
            hist_buf_size = ( hist_buf_size > size ) ? hist_buf_size : size;
            if ( hist_buf_size > 16384 )
               hist_buf_size = 16384;
            break;
         case 'y':             /* Tandy */
            fTandy = 1;         
            break;              
#if defined OS2 || defined __MSDOS__ 
         case 'g':             /* Beyond Zork or other games using IBM graphics */
            fIBMGraphics = 1;   
            break;              
#endif 
         case 'v':             /* version information */

            fprintf( stdout, "\nJZIP - An Infocom Z-code Interpreter Program \n" );
            fprintf( stdout, "       %s %s\n", JZIPVER, JZIPRELDATE );
            if ( STANDALONE_FLAG )
            {
               fprintf( stdout, "       Standalone game: %s\n", argv[0] );
            }
            fprintf( stdout, "---------------------------------------------------------\n" );
            fprintf( stdout, "Author          :  %s\n", JZIPAUTHOR );
            fprintf( stdout, "Official Webpage: %s\n", JZIPURL );
            fprintf( stdout, "IF Archive      : ftp://ftp.gmd.de/if-archive/interpreters/zip/\n" );
            fprintf( stdout, "         Based on ZIP 2.0 source code by Mark Howell\n\n" );
            fprintf( stdout,
                     "Bugs:    Please report bugs and portability bugs to the maintainer." );
            fprintf( stdout, "\n\nInterpreter:\n\n" );
            fprintf( stdout, "\tThis interpreter will run all Infocom V1 to V5 and V8 games.\n" );
            fprintf( stdout,
                     "\tThis is a Z-machine standard 1.0 interpreter, including support for\n" );
            fprintf( stdout, "\tthe Quetzal portable save file format, ISO 8859-1 (Latin-1)\n" );
            fprintf( stdout,
                     "\tinternational character support, and the extended save and load opcodes.\n" );
            fprintf( stdout, "\t\n" );
            infoflag++;
            break;
#if defined (HARD_COLORS)
         case 'f':
            default_fg = atoi( optarg );
            break;
         case 'b':
            default_bg = atoi( optarg );
            break;
#endif
#if defined OS2 || defined __MSDOS__ 
         case 'm':             /* monochrome */
            iPalette = 2;       
            break;              
         case 'b':             /* black-and-white */
            iPalette = 1;       
            break;              
         case 'o':             /* color */
            iPalette = 0;       
            break;              
#else 
         case 'm':
            monochrome = 1;
            break;
#endif 
#if defined TURBOC              
         case 'b':
            bigscreen = 1;
            break;
#endif 
#ifdef STRICTZ
         case 's':             /* strictz reporting mode */
            strictz_report_mode = atoi( optarg );
            if ( strictz_report_mode < STRICTZ_REPORT_NEVER ||
                 strictz_report_mode > STRICTZ_REPORT_FATAL )
            {
               errflg++;
            }
            break;
#endif /* STRICTZ */

         case 'z':
            print_license(  );
            infoflag++;
            break;

         case 'h':
         case '?':
         default:
            errflg++;
      }
   }

   if ( infoflag )
      exit( EXIT_SUCCESS );

   if ( STANDALONE_FLAG )
      expected_args = 0;
   else
      expected_args = 1;

   /* Display usage */

   if ( errflg || optind + expected_args != argc )
   {

      if ( STANDALONE_FLAG )
         fprintf( stdout, "usage: %s [options...]\n", argv[0] ); 
      else
         fprintf( stdout, "usage: %s [options...] story-file\n", argv[0] ); 

      fprintf( stdout, "JZIP - an Infocom story file interpreter.\n" );
      fprintf( stdout, "      Version %s by %s.\n", JZIPVER, JZIPAUTHOR );
      fprintf( stdout, "      Release %s.\n", JZIPRELDATE );
      fprintf( stdout, "      Based on ZIP V2.0 source by Mark Howell\n" );
      fprintf( stdout, "      Plays types 1-5 and 8 Infocom and Inform games.\n\n" );
      fprintf( stdout, "\t-l n lines in display\n" );
      fprintf( stdout, "\t-c n columns in display\n" );
      fprintf( stdout, "\t-r n text right margin (default = %d)\n", DEFAULT_RIGHT_MARGIN );
      fprintf( stdout, "\t-t n text top margin (default = %d)\n", DEFAULT_TOP_MARGIN );
      fprintf( stdout, "\t-k n set the size of the command history buffer to n bytes\n" );
      fprintf( stdout, "\t     (Default is 1024 bytes.  Maximum is 16384 bytes.)\n" );
      fprintf( stdout, "\t-y   turn on the legendary \"Tandy\" bit\n" ); 
      fprintf( stdout, "\t-v   display version information\n" );
      fprintf( stdout, "\t-h   display this usage information\n" );

#if defined (HARD_COLORS)
      fprintf( stdout, "\t-f n foreground color\n" );
      fprintf( stdout, "\t-b n background color (-1 to ignore bg color (try it on an Eterm))\n" );
      fprintf( stdout, "\t     Black=0 Red=1 Green=2 Yellow=3 Blue=4 Magenta=5 Cyan=6 White=7\n" );
#endif
      fprintf( stdout, "\t-m   force monochrome mode\n" ); 
#if defined __MSDOS__ || defined OS2 
      fprintf( stdout, "\t-b   force black-and-white mode\n" ); 
      fprintf( stdout, "\t-o   force color mode\n" ); 
      fprintf( stdout, "\t-g   use \"Beyond Zork\" graphics, rather than standard international\n" ); 
#elif defined TURBOC            
      fprintf( stdout, "\t-b   run in 43/50 line EGA/VGA mode\n" ); 
#endif 

#ifdef STRICTZ
      fprintf( stdout, "\t-s n stricter error checking (default = %d) (0: none; 1: report 1st\n",
               STRICTZ_DEFAULT_REPORT_MODE );
      fprintf( stdout, "\t     error; 2: report all errors; 3: exit after any error)\n" );
#endif /* STRICTZ */

      fprintf( stdout, "\t-z   display license information.\n" );
      exit( EXIT_FAILURE );
   }

   /* Open the story file */

   if ( !STANDALONE_FLAG )      /* mol 951115 */
      open_story( argv[optind] );
   else
   {
      /* standalone, ie. the .exe file _is_ the story file. */
      if ( argv[0][0] == 0 )
      {
         /* some OS's (ie DOS prior to v.3.0) don't supply the path to */
         /* the .exe file in argv[0]; in that case, we give up. (mol) */
         fatal( "process_arguments(): Sorry, this program will not run on this platform." );
      }
      open_story( argv[0] );
   }

}                               /* process_arguments */
