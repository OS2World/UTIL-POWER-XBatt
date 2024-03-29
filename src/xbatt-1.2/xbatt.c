/*
 * xbatt -- Display battery status for FreeBSD-2.x, Linux & OS/2
 *
 * Copyright (c) 1995,1997 by Toshihisa ETO	<eto@osl.fujitsu.co.jp>
 *						<eto@forus.or.jp>
 *
 * This software may be used, modified, copied, and distributed, in
 * both source and binary form provided that the above copyright and
 * these terms are retained. Under no circumstances is the author
 * responsible for the proper functioning of this software, nor does
 * the author assume any responsibility for damages incurred with its
 * use.
 *
 */

/*
 * HISTORY
 *	95/03/27	0.1	XBM version
 *	95/03/28	0.2	refine bitmap management
 *	95/03/30	0.3	use XPM library
 *	95/04/01	0.3.1	by Noriyuki Takahashi (nor@aecl.ntt.jp)
 *		Display plug and charging status.
 *		Support gray-scale and mono (compile-time option).
 *	95/04/03	0.4	Support gray-scale and mono (auto detect).
 *				Update status, when catch key event.
 *	95/04/19	1.0	Official release with some documents!!!!
 *	95/04/27	1.1	Support Linux apm-bios 0.5
 *				by pcs00294@asciinet.or.jp (Koyama Tadayoshi)
 *	97/01/01	1.2b1	Modify some place
 *	97/01/17	1.2b3	Support Linux-1.3.58 or lator
 *					by hisama@bunff.trad.pfu.co.jp
 *					   mac@pssys.flab.fujitsu.co.jp
 *					   yoshi@funival.mfd.cs.fujitsu.co.jp
 *				Support for PC which does not return
 *				buttery life (e.g. ThinkPad 535)
 *					by umemoto
 *				Add check timer routine for resume problem
 *				Show battery life, when the mouse
 *				cursor located on battery icon.
 *	97/01/20	1.2b4	Changes for Xt related part (e.g. resources)
 *					by mi@sy.ssl.fujitsu.co.jp
 *				Change small bugs
 *	99/10/16	-	Support OS/2 emx, by NBG01720@nifty.ne.jp
 */

#include <stdio.h>
#include <time.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <X11/StringDefs.h>
#include <X11/Intrinsic.h>
#include <X11/Shell.h>
#include <X11/extensions/shape.h>
#include <X11/Xaw/Cardinals.h>

#include <X11/xpm.h>

#ifdef XKB
# include <X11/extensions/XKBbells.h>
#endif

#ifdef __EMX__
# define INCL_DOSFILEMGR
# define INCL_DOSDEVICES
# define INCL_DOSDEVIOCTL
# include <os2.h>
#endif

#ifdef __FreeBSD__
# include <machine/apm_bios.h>
# define APMDEV21	"/dev/apm0"
# define APMDEV22	"/dev/apm"
#endif

#include "pixmaps/battery.xpm"
#include "pixmaps/unknown.xpm"
#include "bitmaps/full.xbm"
#include "bitmaps/digit0.xbm"
#include "bitmaps/digit1.xbm"
#include "bitmaps/digit2.xbm"
#include "bitmaps/digit3.xbm"
#include "bitmaps/digit4.xbm"
#include "bitmaps/digit5.xbm"
#include "bitmaps/digit6.xbm"
#include "bitmaps/digit7.xbm"
#include "bitmaps/digit8.xbm"
#include "bitmaps/digit9.xbm"
#include "bitmaps/percent.xbm"

/* reflash Interval (in seconds) */
#ifndef UPDATE_INTERVAL
# ifndef NOAPM
#  define UPDATE_INTERVAL	30
# else
#  define UPDATE_INTERVAL	5
# endif
#endif

#define APM_STAT_UNKNOWN	255
/* aip->ai_acline */
#define APM_STAT_LINE_OFF	0
#define APM_STAT_LINE_ON	1

/* aip->ai_batt_stat */
#define APM_STAT_BATT_HIGH	0
#define APM_STAT_BATT_LOW	1
#define APM_STAT_BATT_CRITICAL	2
#define APM_STAT_BATT_CHARGING	3

struct status {
    u_int	remain;
    u_int	acline;
    u_int	charge;
};
static struct status	lastStat = {APM_STAT_UNKNOWN, APM_STAT_UNKNOWN,
			    APM_STAT_UNKNOWN};

static	void		updateStatus(XtPointer, XtIntervalId*);
static	struct status	getBatteryStatus();
static	void		updateWindow(struct status);
static	void		initWindowResource();

#define BatteryWidth	39
#define BatteryHeight	39

static	void setColorSymbol(unsigned int, unsigned int, unsigned int);
static	void goOut();
static	void CallbackTyped(Widget, char*, XEvent*, Boolean*);
static	void CallbackEnter(Widget, char*, XEvent*, Boolean*);
static	void CallbackLeave(Widget, char*, XEvent*, Boolean*);

static	time_t		lastUpdateTime = 0;
static	int		showStatus = 0;
static	int		forceRedraw = 0;
static	int		apmfd = 0;
static	Widget		toplevel;
static	Pixmap		xpmData, xpmMask;
static	GC		gc;
static	XpmAttributes	attr;
static	XtIntervalId	timer = 0;
static	int		displayType;	/* Mono, Gray, Color */
#define Color	0
#define Gray	1
#define Mono	2
#define MAXSYM		30
static	XpmColorSymbol	sym[MAXSYM];
static	unsigned int	nsym = 0;

typedef struct _SetupRec{
    int timerInterval;
} SetupRec, *SetupPtr;

static SetupRec appResources;

static XtResource resources[] = {
    {"timerInterval", "TimerInterval", XtRInt, sizeof(int),
     XtOffsetOf(struct _SetupRec, timerInterval), XtRImmediate,
     (XtPointer)UPDATE_INTERVAL},
};

static XrmOptionDescRec options[] = {
    {"-timerInterval", ".timerInterval", XrmoptionSepArg,
     (XtPointer)UPDATE_INTERVAL},
};

struct	Digits {
    unsigned int	width;
    unsigned int	height;
    unsigned char*	bits;
};

struct Digits	digits[] = {
    {digit0_width, digit0_height, digit0_bits},
    {digit1_width, digit1_height, digit1_bits},
    {digit2_width, digit2_height, digit2_bits},
    {digit3_width, digit3_height, digit3_bits},
    {digit4_width, digit4_height, digit4_bits},
    {digit5_width, digit5_height, digit5_bits},
    {digit6_width, digit6_height, digit6_bits},
    {digit7_width, digit7_height, digit7_bits},
    {digit8_width, digit8_height, digit8_bits},
    {digit9_width, digit9_height, digit9_bits}
};

main(
  int	argc,
  char	*argv[]
) {
    XSizeHints		size_hints;
    Visual*	visual;
    int		depth;
    XtAppContext appContext;

#if !defined(NOAPM) && defined(__FreeBSD__)
    /* initialize APM Interface */
    if ((apmfd = open(APMDEV21, O_RDWR)) == -1) {
	if ((apmfd = open(APMDEV22, O_RDWR)) == -1) {
	    fprintf(stderr, "xbatt: cannot open apm device\n");
	    exit(1);
	}
    }
#endif

    /* start X-Window session */
    XtSetLanguageProc( NULL, NULL, NULL );
    toplevel = XtOpenApplication(&appContext, "XBatt",
				 options, XtNumber(options),
				 &argc, argv, NULL,
				 sessionShellWidgetClass, NULL, ZERO);
    if (!toplevel) {
	fprintf(stderr, "xbatt: cannot start x-session\n");
	exit(1);
    }
    if (argc != 1) {
	fprintf(stderr, "usage: xbatt [X-Toolkit standard option]\n");
	exit(1);
    }

    /* get rsource information */
    XtGetApplicationResources(toplevel, &appResources,
			      resources, XtNumber(resources), NULL, 0);


    /* get display information */
    visual = XDefaultVisual(XtDisplay(toplevel),
			    DefaultScreen(XtDisplay(toplevel)));
    depth = XDefaultDepth(XtDisplay(toplevel),
			  DefaultScreen(XtDisplay(toplevel)));
    if (depth == 1) {
	displayType = Mono;
    } else {
	switch(visual->class) {
	  case PseudoColor:
	  case DirectColor:
	  case StaticColor:
	  case TrueColor:
	    displayType = Color;
	    break;
	  case GrayScale:
	  case StaticGray:
	    displayType = Gray;
	    break;
	  default:
	    displayType = Mono;
	}
    }

    /* set toplevel property */
    XtVaSetValues(toplevel,
		  XtNmappedWhenManaged, False,
		  XtNinput, True,
		  XtNwidth, BatteryWidth,
		  XtNheight,BatteryHeight,
		  XtNminWidth, BatteryWidth,
		  XtNminHeight, BatteryHeight,
		  XtNiconName, "xbatt",
		  NULL);

    /* realize toplevel widget */
    XtRealizeWidget(toplevel);

    /* set window information and callback procedure */
    initWindowResource();
    XtAddEventHandler(toplevel, KeyPressMask, False,
		      (XtEventHandler)CallbackTyped, NULL);
    XtAddEventHandler(toplevel, EnterWindowMask, False,
		      (XtEventHandler)CallbackEnter, NULL);
    XtAddEventHandler(toplevel, LeaveWindowMask, False,
		      (XtEventHandler)CallbackLeave, NULL);

    /* display current battery status, and set timer callback */
    updateStatus(NULL, NULL);

    /* start main loop */
    XtAppMainLoop(appContext);

    /* free resource, and go out*/
    goOut(0);
}

void updateStatus(
  XtPointer	client_data,
  XtIntervalId	*t
) {
    struct status s;

    /* if this function called by another function, */
    /* then remove current timer */
    if ((t == NULL) && (timer != 0)) {
	XtRemoveTimeOut(timer);
	timer = 0;
    }

    /* get and update current status */
    s = getBatteryStatus();

    /* if status has changed, update window */
    if (((s.remain != lastStat.remain)
	 && (((s.remain % 5) == 0) || showStatus != 0))
	|| (s.acline != lastStat.acline)
	|| (s.charge != lastStat.charge)
	|| (forceRedraw != 0)
	) {
	/* update window */
	updateWindow(s);

	/* remember current status */
	lastStat = s;

	forceRedraw = 0;
    }

    /* start timer for next interval */
    timer = XtAppAddTimeOut(XtWidgetToApplicationContext(toplevel),
			    appResources.timerInterval * 1000,
			    updateStatus, toplevel);

    /* save time stamp */
    lastUpdateTime = time(NULL);
}

struct status getBatteryStatus()
{
    struct status	ret;
#ifdef	__EMX__
    ULONG  cbParmLen=0;
    ULONG  cbDataLen=0;
    ULONG  ulAction=0;
    APIRET rc;
    HFILE  Hf;
    USHORT ReturnCode=0;
#pragma pack(1) /* Define packed struction */
    struct {
	USHORT ParamLength;
	USHORT PowerFlags;
	UCHAR  ACStatus;
	UCHAR  BatteryStatus;
	UCHAR  BatteryLife;
    }param={sizeof param};
#pragma pack()	/* Restore default */

    if (DosOpen("\\DEV\\APM$", &Hf, &ulAction,
		0, FILE_NORMAL, OPEN_ACTION_OPEN_IF_EXISTS,
		OPEN_FLAGS_FAIL_ON_ERROR | OPEN_SHARE_DENYNONE
		| OPEN_ACCESS_READWRITE, NULL)){
	fputs("xbatt: cannot open apm device\n", stderr);
	exit(EXIT_FAILURE);
    }
    rc = DosDevIOCtl(Hf, IOCTL_POWER, POWER_GETPOWERSTATUS,
		     &param, sizeof param, &cbParmLen,
		     &ReturnCode, sizeof ReturnCode, &cbDataLen);
    if(rc||ReturnCode){
	fputs("xbatt: IOCTL_POWER/POWER_GETPOWERSTATUS failed\n", stderr);
	exit(EXIT_FAILURE);
    }
    ret.remain = param.BatteryLife;
    ret.acline = param.ACStatus;
    ret.charge = param.BatteryStatus;
    DosClose(Hf);
#endif	/* __EMX__ */

#ifdef	__FreeBSD__
    struct apm_info	info;

#ifndef NOAPM
    /* get APM information */
    if (ioctl(apmfd, APMIO_GETINFO, &info) == -1) {
	fprintf(stderr, "xbatt: ioctl APMIO_GETINFO failed\n");
	exit(1);
    }
#else	/* NOAPM */
    /* set dummy battery life */
    if ((lastStat.remain < 1) || (lastStat.remain > 10)) {
	info.ai_batt_life = 100;
    } else {
	info.ai_batt_life = (lastStat.remain - 1) * 10;
    }
#endif	/* NOAPM */

    /* get current status */
    if (info.ai_batt_life == APM_STAT_UNKNOWN) {
	switch (info.ai_batt_stat) {
	  case APM_STAT_BATT_HIGH:
	    ret.remain = 100;
	    break;
	  case APM_STAT_BATT_LOW:
	    ret.remain = 40;
	    break;
	  case APM_STAT_BATT_CRITICAL:
	    ret.remain = 10;
	    break;
	  default:	  /* expected to be APM_STAT_UNKNOWN */
	    ret.remain = APM_STAT_UNKNOWN;
	}
    } else if (info.ai_batt_life > 100) {
	/* some APM BIOSes return values slightly > 100 */
	ret.remain = 100;
    } else {
	ret.remain = info.ai_batt_life;
    }

    /* get AC-line status */
    if (info.ai_acline == APM_STAT_LINE_ON) {
	ret.acline = APM_STAT_LINE_ON;
    } else {
	ret.acline = APM_STAT_LINE_OFF;
    }

    /* get charging status */
    if (info.ai_batt_stat == APM_STAT_BATT_CHARGING) {
	ret.charge = APM_STAT_BATT_CHARGING;
    } else {
	ret.charge = APM_STAT_BATT_HIGH;	/* I only want to know, */
						/* chrging or not.	*/
    }
#endif	/* FreeBSD */

#ifdef	__linux__
    char	buffer[64];
    FILE*	fp;
    int		battLife;
     char	driver_version[64];
     int		apm_bios_info_major;
     int		apm_bios_info_minor;
     int		apm_bios_info_flags;
     int		ac_line_status;
     int		battery_status;
     int		battery_flag;
     int		time_units;
     char	units[64];

    /* open power status */
    if ((fp = fopen("/proc/apm", "r")) == NULL) {
	fprintf(stderr, "xbatt: cannot optn /proc/apm\n");
	exit(1);
    }

    ret.charge = APM_STAT_BATT_HIGH;
    ret.acline = APM_STAT_LINE_OFF;
    while (fgets(buffer, 64, fp) != NULL) {
	/*
	 * for linux-1.3.58 or later
	 */
	if (sscanf(buffer, "%s %d.%d 0x%x 0x%x 0x%x 0x%x %d%% %d %s\n",
		driver_version,
		&apm_bios_info_major,
		&apm_bios_info_minor,
		&apm_bios_info_flags,
		&ac_line_status,
		&battery_status,
		&battery_flag,
		&battLife,
		&time_units,
		units) == 10) {
	    if (battLife < 0) {
		ret.remain = APM_STAT_UNKNOWN;
	    } else {
		ret.remain = battLife;
	    }
	    if (ac_line_status == 0x01) {
		ret.acline = APM_STAT_LINE_ON;
	    }
	    if (battery_status == 0x03) {
		ret.charge = APM_STAT_BATT_CHARGING;
	    }
	}
	/*
	 * for apm_bios-0.5
	 */
	else if (sscanf(buffer, "Battery life: %d%%\n", &battLife) == 1) {
	    ret.remain = battLife;
	} else if (strcmp("AC: on line\n", buffer)==0) {
	    ret.acline = APM_STAT_LINE_ON;
	} else if (strcmp("Battery status: charging\n", buffer) == 0) {
	    ret.charge = APM_STAT_BATT_CHARGING;
	}
    }
    fclose(fp);
#endif	/* Linux */

    return ret;
}

void initWindowResource()
{
    gc = XCreateGC(XtDisplay(toplevel), XtWindow(toplevel), 0, 0);
    XSetState(XtDisplay(toplevel), gc, 1, 0, GXcopy, AllPlanes);
    XSetFillStyle(XtDisplay(toplevel), gc, FillSolid);
    XFlush(XtDisplay(toplevel));
}

void updateWindow(struct status s)
{
    Pixmap	bmp;
    int		ret;

    /* free old data */
    if (xpmData) {
	XFreePixmap(XtDisplay(toplevel), xpmData);
    }
    if (xpmMask) {
	XFreePixmap(XtDisplay(toplevel), xpmMask);
    }
    XpmFreeAttributes(&attr);

    /* set attribute value mask */
    attr.valuemask = 0;
    attr.valuemask |= XpmReturnPixels;
    attr.valuemask |= XpmReturnExtensions;
    attr.valuemask |= XpmColorSymbols;

    /* set color symbol for current status */
    setColorSymbol(s.remain, s.acline, s.charge);

    /* set color_class flag */
    if (displayType == Mono) {
	attr.valuemask |= XpmColorKey;
	attr.color_key = XPM_MONO;
    } else if (displayType == Gray) {
	attr.valuemask |= XpmColorKey;
	attr.color_key = XPM_GRAY;
    }

    /* create pixmap data */
    if (s.remain != APM_STAT_UNKNOWN) {
	ret = XpmCreatePixmapFromData(XtDisplay(toplevel),
				      XtWindow(toplevel),
				      batt_xpm, &xpmData,
				      &xpmMask, &attr);
    } else {
	ret = XpmCreatePixmapFromData(XtDisplay(toplevel),
				      XtWindow(toplevel),
				      ubatt_xpm, &xpmData,
				      &xpmMask, &attr);
    }
    switch (ret) {
      case XpmSuccess:
	break;
      case XpmColorError:
	fprintf(stderr,
		"xbatt: Could not parse or alloc requested color\n");
	break;
      case XpmNoMemory:
	fprintf(stderr, "xbatt: Not enough memory\n");
	goOut(1);
	break;
      case XpmColorFailed:
	fprintf(stderr, "xbatt: Failed to parse or alloc some color\n");
	goOut(1);
	break;
    }

    /* resize window size to pixmap size */
    XtResizeWidget(toplevel, attr.width, attr.height, 1);

    /* if show status is active, then draw status info */
    if ((showStatus != 0) && (s.remain != APM_STAT_UNKNOWN)) {
	Pixmap	bm;

	XSetForeground(XtDisplay(toplevel), gc,
		       BlackPixel(XtDisplay(toplevel),
				  DefaultScreen(XtDisplay(toplevel))));
	XDrawRectangle(XtDisplay(toplevel), xpmData, gc, 17, 17, 14, 8);
	XSetForeground(XtDisplay(toplevel), gc,
		       WhitePixel(XtDisplay(toplevel),
				  DefaultScreen(XtDisplay(toplevel))));
	XFillRectangle(XtDisplay(toplevel), xpmData, gc, 18, 18, 13, 7);

	if (s.remain == 100) {
	    bm = XCreatePixmapFromBitmapData(XtDisplay(toplevel),
					     XtWindow(toplevel),
					     full_bits, full_width,
					     full_height,
					     BlackPixel(XtDisplay(toplevel),
						 DefaultScreen(
						     XtDisplay(toplevel))),
					     WhitePixel(XtDisplay(toplevel),
						 DefaultScreen(
						     XtDisplay(toplevel))),
					     1);	/* Depth */
	    XCopyPlane(XtDisplay(toplevel), bm, xpmData, gc, 0, 0,
		       full_width, full_height,
		       19, 19, 1);
	    XFreePixmap(XtDisplay(toplevel), bm);
	} else {
	    int d = s.remain / 10;
	    bm = XCreatePixmapFromBitmapData(XtDisplay(toplevel),
					     XtWindow(toplevel),
					     digits[d].bits,
					     digits[d].width,
					     digits[d].height,
					     BlackPixel(XtDisplay(toplevel),
						 DefaultScreen(
						     XtDisplay(toplevel))),
					     WhitePixel(XtDisplay(toplevel),
						 DefaultScreen(
						     XtDisplay(toplevel))),
					     1);
	    XCopyPlane(XtDisplay(toplevel), bm, xpmData, gc, 0, 0, 3, 5,
		       19, 19, 1);
	    XFreePixmap(XtDisplay(toplevel), bm);

	    d = s.remain % 10;
	    bm = XCreatePixmapFromBitmapData(XtDisplay(toplevel),
					     XtWindow(toplevel),
					     digits[d].bits,
					     digits[d].width,
					     digits[d].height,
					     BlackPixel(XtDisplay(toplevel),
						 DefaultScreen(
						     XtDisplay(toplevel))),
					     WhitePixel(XtDisplay(toplevel),
						 DefaultScreen(
						     XtDisplay(toplevel))),
					     1);
	    XCopyPlane(XtDisplay(toplevel), bm, xpmData, gc, 0, 0, 3, 5,
		       23, 19, 1);
	    XFreePixmap(XtDisplay(toplevel), bm);

	    bm = XCreatePixmapFromBitmapData(XtDisplay(toplevel),
					     XtWindow(toplevel),
					     percent_bits,
					     percent_width,
					     percent_height,
					     BlackPixel(XtDisplay(toplevel),
						 DefaultScreen(
						     XtDisplay(toplevel))),
					     WhitePixel(XtDisplay(toplevel),
						 DefaultScreen(
						     XtDisplay(toplevel))),
					     1);
	    XCopyPlane(XtDisplay(toplevel), bm, xpmData, gc, 0, 0, 3, 5,
		       27, 19, 1);
	    XFreePixmap(XtDisplay(toplevel), bm);
	}
    }

    /* set pixmap data */
    XSetWindowBackgroundPixmap(XtDisplay(toplevel),
			       XtWindow(toplevel),
			       xpmData);

    /* if pixmap has transparent pixel, set mask pixmap */
    if (xpmMask) {
	XShapeCombineMask(XtDisplay(toplevel), XtWindow(toplevel),
			  ShapeBounding, 0, 0, xpmMask, ShapeSet);
    }

    /* redraw window */
    XClearWindow(XtDisplay(toplevel), XtWindow(toplevel));

    /* map window */
    XtMapWidget(toplevel);
}

void setColorSymbol(
  unsigned int	life,
  unsigned int	acline,
  unsigned int	charge
) {
    unsigned int	i;
    char*		gage_color;

    life = (life + 9) / 10;

    /* If battery life is shore, ring a bell */
    if ((life != APM_STAT_UNKNOWN) && (life < 5) && ((life % 5) == 0)) {
#ifdef XKB
	XkbStdBell(XtDisplay(toplevel), XtWindow(toplevel), 0,
		   XkbBI_MinorError);
#else
	XBell(XtDisplay(toplevel), 100);
#endif
    }

    /* clear old data */
    for(i = 0; i < nsym; i++) {
	if (sym[i].name != NULL) {
	    free(sym[i].name);
	    sym[i].name = NULL;
	}
    }

    /* if the battery life is not UNKNOWN status */
    if (life != APM_STAT_UNKNOWN) {
	/* set default(High life) gage color */
	switch(displayType) {
	  case Mono:
	    gage_color = "black";
	    break;
	  case Gray:
	    gage_color = "grey80";
	    break;
	  case Color:
	    gage_color = "green";
	}

	/* set gage color */
	if (displayType == Color) {
	    if (life < 3) {
		gage_color = "red";
	    } else if (life < 5) {
		gage_color = "yellow";
	    }
	}

	/* set symbol for gage */
	nsym = 0;
	for(i = 1; i <= life; i++) {
	    sym[nsym].name	= (char*)malloc(7);
	    sprintf(sym[nsym].name, "gage%d", i);
	    sym[nsym].value = gage_color;
	    nsym++;
	}
    }

    /* set symbol for AC-plug */
    if(acline != APM_STAT_LINE_ON) {
	sym[nsym].name = (char*)malloc(5);
	strcpy(sym[nsym].name, "plug");
	sym[nsym].value = "None";
	nsym++;
    }

    /* set symbol for AC-plug */
    if(charge != APM_STAT_BATT_CHARGING) {
	sym[nsym].name = (char*)malloc(6);
	strcpy(sym[nsym].name, "spark");
	sym[nsym].value = "None";
	nsym++;
    }

    /* set array to pixmap attribute */
    attr.colorsymbols	= sym;
    attr.numsymbols	= nsym;
}

void goOut(
  int i
) {
    Arg arg;

    /* if already running XtTimer, then remove it */
    if (timer != 0) {
	XtRemoveTimeOut(timer);
    }

    /* resign from the session */
    XtSetArg(arg, XtNjoinSession, False);
    XtSetValues(toplevel, &arg, ONE);

    /* free pixmap resources */
    if (xpmData) {
	XFreePixmap(XtDisplay(toplevel), xpmData);
    }
    if (xpmMask) {
	XFreePixmap(XtDisplay(toplevel), xpmMask);
    }
    XpmFreeAttributes(&attr);

    /* close apm fd */
    close(apmfd);
    XCloseDisplay(XtDisplay(toplevel));
    exit(i);
}

void CallbackTyped(
  Widget	widget,
  char*		tag,
  XEvent*	xe,
  Boolean*	b
) {
    char c;

    /* get key character */
    c = '\0';
    XLookupString(&(xe->xkey), &c, 1, NULL, NULL);

    /* if place `q' key, then just quit */
    if (c == 'q' || c == 'Q') {
	goOut(0);
    }

    /* display current battery status, and set timer callback */
    updateStatus(NULL, NULL);
}

void CallbackEnter(
  Widget	widget,
  char*		tag,
  XEvent*	xe,
  Boolean*	b
) {
    /* set status flag */
    showStatus = 1;

    /* display current battery status, and set timer callback */
    forceRedraw = 1;
    updateStatus(NULL, NULL);

/*    fprintf(stderr, ">>> Enter [%d]\n", s.remain);*/
}

void CallbackLeave(
  Widget	widget,
  char*		tag,
  XEvent*	xe,
  Boolean*	b
) {
    /* reset status flag */
    showStatus = 0;

    /* display current battery status, and set timer callback */
    forceRedraw = 1;
    updateStatus(NULL, NULL);

/*    fprintf(stderr, "<<< Leave\n");*/
}
