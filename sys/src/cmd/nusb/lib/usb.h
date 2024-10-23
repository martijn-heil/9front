typedef struct Conf Conf;
typedef struct DConf DConf;
typedef struct DDesc DDesc;
typedef struct DDev DDev;
typedef struct DEp DEp;
typedef struct DIface DIface;
typedef struct Desc Desc;
typedef struct Dev Dev;
typedef struct Ep Ep;
typedef struct Iface Iface;
typedef struct Usbdev Usbdev;

enum {
	/* tunable parameters */
	Nep	= 64,	/* max. endpoints per interface */
	Nconf	= 16,	/* max. configurations per usb device */
	Nddesc	= 8*Nep, /* max. device-specific descriptors per usb device */
	Niface	= 16,	/* max. interfaces per configuration */
	Uctries	= 4,	/* no. of tries for usbcmd */
	Ucdelay	= 50,	/* delay before retrying */

	/* fundamental constants */
	Epmax	= 0xF,		/* endpoint address mask */

	/* request type */
	Rh2d	= 0<<7,		/* host to device */
	Rd2h	= 1<<7,		/* device to host */ 

	Rstd	= 0<<5,		/* types */
	Rclass	= 1<<5,
	Rvendor	= 2<<5,

	Rdev	= 0,		/* recipients */
	Riface	= 1,
	Rep	= 2,		/* endpoint */
	Rother	= 3,

	/* standard requests (USB2.0) */
	Rgetstatus	= 0,
	Rclearfeature	= 1,
	Rsetfeature	= 3,
	Rsetaddress	= 5,
	Rgetdesc	= 6,
	Rsetdesc	= 7,
	Rgetconf	= 8,
	Rsetconf	= 9,
	Rgetiface	= 10,
	Rsetiface	= 11,
	Rsynchframe	= 12,

	/* standard requests (USB3.0) */
	Rsethubdepth	= 12,
	Rgetporterrcnt	= 13,

	/* dev classes */
	Clnone		= 0,		/* not in usb */
	Claudio		= 1,
	Clcomms		= 2,
	Clhid		= 3,
	Clprinter	= 7,
	Clstorage	= 8,
	Clhub		= 9,
	Cldata		= 10,

	/* standard descriptor sizes */
	Ddevlen		= 18,
	Dconflen	= 9,
	Difacelen	= 9,
	Deplen		= 7,

	/* descriptor types */
	Ddev		= 1,
	Dconf		= 2,
	Dstr		= 3,
	Diface		= 4,
	Dep		= 5,
	Dreport		= 0x22,
	Dfunction	= 0x24,
	Dphysical	= 0x23,

	/* feature selectors */
	Fdevremotewakeup = 1,
	Fhalt 	= 0,

	/* device state */
	Detached = 0,
	Attached,
	Enabled,
	Assigned,
	Configured,

	/* endpoint direction */
	Ein = 0,
	Eout,
	Eboth,

	/* endpoint type */
	Econtrol = 0,
	Eiso = 1,
	Ebulk = 2,
	Eintr = 3,

	/* endpoint isousage */
	Edata = 0,
	Efeedback = 1,
	Eimplicit = 2,

	/* config attrib */
	Cbuspowered = 1<<7,
	Cselfpowered = 1<<6,
	Cremotewakeup = 1<<5,

	/* report types */
	Tmtype	= 3<<2,
	Tmitem	= 0xF0,
	Tmain	= 0<<2,
		Tinput	= 0x80,
		Toutput	= 0x90,
		Tfeature = 0xB0,
		Tcoll	= 0xA0,
		Tecoll	= 0xC0,
	 Tglobal	= 1<<2,
		Tusagepage = 0x00,
		Tlmin	= 0x10,
		Tlmax	= 0x20,
		Tpmin	= 0x30,
		Tpmax	= 0x40,
		Tunitexp	= 0x50,
		Tunit	= 0x60,
		Trepsize	= 0x70,
		TrepID	= 0x80,
		Trepcount = 0x90,
		Tpush	= 0xA0,
		Tpop	= 0xB0,
	 Tlocal	= 2<<2,
		Tusage	= 0x00,
		Tumin	= 0x10,
		Tumax	= 0x20,
		Tdindex	= 0x30,
		Tdmin	= 0x40,
		Tdmax	= 0x50,
		Tsindex	= 0x70,
		Tsmin	= 0x80,
		Tsmax	= 0x90,
		Tsetdelim = 0xA0,
	 Treserved	= 3<<2,
	 Tlong	= 0xFE,

};

/*
 * Usb device (when used for ep0s) or endpoint.
 * RC: One ref because of existing, another one per ogoing I/O.
 * per-driver resources (including FS if any) are released by aux
 * once the last ref is gone. This may include other Devs using
 * to access endpoints for actual I/O.
 */
struct Dev
{
	Ref;
	char*	dir;		/* path for the endpoint dir */
	int	id;		/* usb id for device or ep. number */
	int	dfd;		/* descriptor for the data file */
	int	cfd;		/* descriptor for the control file */
	int	isusb3;		/* this is a usb3 device */
	int	depth;		/* hub depth from roothub -1 */
	int	maxpkt;		/* cached from usb description */
	Usbdev*	usb;		/* USB description */
	Ep*	ep;		/* endpoint from epopen() */
	void*	aux;		/* for the device driver */
	char*	hname;		/* hash name, unique for device */
};

/*
 * device description as reported by USB (unpacked).
 */
struct Usbdev
{
	int	ver;		/* usb version */
	ulong	csp;		/* USB class/subclass/proto */
	int	vid;		/* vendor id */
	int	did;		/* product (device) id */
	int	dno;		/* device release number */
	char*	vendor;
	char*	product;
	char*	serial;
	int	vsid;
	int	psid;
	int	ssid;
	int	class;		/* from descriptor */
	int	nconf;		/* from descriptor */
	Conf*	conf[Nconf];	/* configurations */
	Ep*	ep[Epmax+1];	/* all endpoints in device (chained), indexed by address */
	Desc*	ddesc[Nddesc];	/* (raw) device specific descriptors */
};

struct Ep
{
	Iface*	iface;		/* the endpoint belongs to */
	Conf*	conf;		/* the endpoint belongs to */

	int	id;		/* endpoint number: (id & Epmax) == endpoint address */
	uchar	dir;		/* direction, Ein/Eout/Eboth */
	uchar	type;		/* Econtrol, Eiso, Ebulk, Eintr */

	int	attrib;
	int	pollival;
	int	maxpkt;		/* max. packet size */
	int	ntds;		/* nb. of Tds per µframe */

	/* chain of endpoints with same address (used in different interfaces/altsettings) */
	Ep*	next;

	void*	aux;		/* for the driver program */
};

struct Iface
{
	int	id;		/* interface number */
	int	alt;		/* altsetting for this interface */
	ulong	csp;		/* USB class/subclass/proto */
	Iface*	next;		/* chain of interfaces of different altsettings */
	Ep*	ep[Nep];	/* consecutive array of endpoints in this interface (not including ep0) */

	void*	aux;		/* for the driver program */
};

struct Conf
{
	int	cval;		/* value for set configuration */
	int	attrib;
	int	milliamps;	/* maximum power in this config. */
	Iface*	iface[Niface];
};

/*
 * Device-specific descriptors.
 * They show up mixed with other descriptors
 * within a configuration.
 * These are unknown to the library but handed to the driver.
 */
struct DDesc
{
	uchar	bLength;
	uchar	bDescriptorType;
	uchar	bbytes[1];
	/* extra bytes allocated here to keep the rest of it */
};

struct Desc
{
	Conf*	conf;		/* where this descriptor was read */
	Iface*	iface;		/* last iface before desc in conf. */
	Ep*	ep;		/* last endpt before desc in conf. */
	DDesc	data;		/* unparsed standard USB descriptor */
};

/*
 * layout of standard descriptor types
 */
struct DDev
{
	uchar	bLength;
	uchar	bDescriptorType;
	uchar	bcdUSB[2];
	uchar	bDevClass;
	uchar	bDevSubClass;
	uchar	bDevProtocol;
	uchar	bMaxPacketSize0;
	uchar	idVendor[2];
	uchar	idProduct[2];
	uchar	bcdDev[2];
	uchar	iManufacturer;
	uchar	iProduct;
	uchar	iSerialNumber;
	uchar	bNumConfigurations;
};

struct DConf
{
	uchar	bLength;
	uchar	bDescriptorType;
	uchar	wTotalLength[2];
	uchar	bNumInterfaces;
	uchar	bConfigurationValue;
	uchar	iConfiguration;
	uchar	bmAttributes;
	uchar	MaxPower;
};

struct DIface
{
	uchar	bLength;
	uchar	bDescriptorType;
	uchar	bInterfaceNumber;
	uchar	bAlternateSetting;
	uchar	bNumEndpoints;
	uchar	bInterfaceClass;
	uchar	bInterfaceSubClass;
	uchar	bInterfaceProtocol;
	uchar	iInterface;
};

struct DEp
{
	uchar	bLength;
	uchar	bDescriptorType;
	uchar	bEndpointAddress;
	uchar	bmAttributes;
	uchar	wMaxPacketSize[2];
	uchar	bInterval;
};

#define Class(csp)	((csp) & 0xff)
#define Subclass(csp)	(((csp)>>8) & 0xff)
#define Proto(csp)	(((csp)>>16) & 0xff)
#define CSP(c, s, p)	((c) | (s)<<8 | (p)<<16)

#define	GET2(p)		(((p)[1] & 0xFF)<<8 | ((p)[0] & 0xFF))
#define	PUT2(p,v)	{(p)[0] = (v); (p)[1] = (v)>>8;}
#define	GET4(p)		(((p)[3]&0xFF)<<24 | ((p)[2]&0xFF)<<16 | \
			 ((p)[1]&0xFF)<<8  | ((p)[0]&0xFF))
#define	PUT4(p,v)	{(p)[0] = (v);     (p)[1] = (v)>>8; \
			 (p)[2] = (v)>>16; (p)[3] = (v)>>24;}

#define dprint if(usbdebug)fprint
#define ddprint if(usbdebug > 1)fprint

#pragma	varargck	type  "U"	Dev*
#pragma	varargck	argpos	devctl	2

int	Ufmt(Fmt *f);
char*	classname(int c);
void	closedev(Dev *d);
int	configdev(Dev *d);
int	devctl(Dev *dev, char *fmt, ...);
void*	emallocz(ulong size, int zero);
char*	estrdup(char *s);
char*	hexstr(void *a, int n);
int	loaddevconf(Dev *d, int n);
int	loaddevdesc(Dev *d);
char*	loaddevstr(Dev *d, int sid);
Dev*	opendev(char *fn);
int	opendevdata(Dev *d, int mode);
Dev*	openep(Dev *d, Ep *e);
int	parseconf(Usbdev *d, Conf *c, uchar *b, int n);
int	parsedesc(Usbdev *d, Conf *c, uchar *b, int n);
int	parsedev(Dev *xd, uchar *b, int n);
int	unstall(Dev *dev, Dev *ep, int dir);
int	usbcmd(Dev *d, int type, int req, int value, int index, uchar *data, int count);
Dev*	getdev(char *devid);
int	setconf(Dev *d, Conf *c);
int	setalt(Dev *d, Iface *ifc);

extern int usbdebug;	/* more messages for bigger values */
