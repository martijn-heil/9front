#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

#include	<libsec.h>

enum
{
	Hashlen=	SHA1dlen,
	Maxhash=	256,
	Timeout=	60,	/* seconds */
};

/*
 *  if a process knows cap->cap, it can change user
 *  to capabilty->user.
 */
typedef struct Caphash	Caphash;
struct Caphash
{
	Caphash	*next;
	ulong	ticks;
	char	hash[Hashlen];
};

static struct
{
	QLock;
	Caphash	*first;
	int	nhash;
} capalloc;

enum
{
	Qdir,
	Qhash,
	Quse,
};

/* caphash must be last */
Dirtab capdir[] =
{
	".",		{Qdir,0,QTDIR},	0,		DMDIR|0500,
	"capuse",	{Quse},		0,		0222,
	"caphash",	{Qhash},	0,		0200,
};
int ncapdir = nelem(capdir);

static Chan*
capattach(char *spec)
{
	return devattach(L'¤', spec);
}

static Walkqid*
capwalk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, capdir, ncapdir, devgen);
}

static void
capremove(Chan *c)
{
	if(iseve() && c->qid.path == Qhash)
		ncapdir = nelem(capdir)-1;
	else
		error(Eperm);
}


static int
capstat(Chan *c, uchar *db, int n)
{
	return devstat(c, db, n, capdir, ncapdir, devgen);
}

/*
 *  if the stream doesn't exist, create it
 */
static Chan*
capopen(Chan *c, int omode)
{
	if(c->qid.type & QTDIR){
		if(omode != OREAD)
			error(Ebadarg);
		c->mode = omode;
		c->flag |= COPEN;
		c->offset = 0;
		return c;
	}

	switch((ulong)c->qid.path){
	case Qhash:
		if(!iseve())
			error(Eperm);
		break;
	}

	c->mode = openmode(omode);
	c->flag |= COPEN;
	c->offset = 0;
	return c;
}

static void
trimcaps(void)
{
	Caphash *t;

	while((t = capalloc.first) != nil){
		if(capalloc.nhash < Maxhash && TK2SEC(MACHP(0)->ticks - t->ticks) < Timeout)
			break;
		capalloc.nhash--;
		capalloc.first = t->next;
		secfree(t);
	}
}

static Caphash*
remcap(uchar *hash)
{
	Caphash *t, **l;

	qlock(&capalloc);

	/* timeout old caps */
	trimcaps();

	/* find the matching capability */
	for(l = &capalloc.first; *l != nil;){
		t = *l;
		if(tsmemcmp(hash, t->hash, Hashlen) == 0)
			break;
		l = &t->next;
	}
	t = *l;
	if(t != nil){
		capalloc.nhash--;
		*l = t->next;
	}
	qunlock(&capalloc);

	return t;
}

/* add a capability, throwing out any old ones */
static void
addcap(uchar *hash)
{
	Caphash *p, **l;

	p = secalloc(sizeof *p);
	memmove(p->hash, hash, Hashlen);
	p->next = nil;
	p->ticks = MACHP(0)->ticks;

	qlock(&capalloc);

	/* make room for one extra */
	trimcaps();

	/* add new one */
	for(l = &capalloc.first; *l != nil; l = &(*l)->next)
		;
	*l = p;
	capalloc.nhash++;

	qunlock(&capalloc);
}

static void
capclose(Chan*)
{
}

static long
capread(Chan *c, void *va, long n, vlong)
{
	switch((ulong)c->qid.path){
	case Qdir:
		return devdirread(c, va, n, capdir, ncapdir, devgen);

	default:
		error(Eperm);
		break;
	}
}

static long
capwrite(Chan *c, void *va, long n, vlong)
{
	Caphash *p;
	char *cp;
	uchar hash[Hashlen];
	char *key, *from, *to;
	char err[256];

	switch((ulong)c->qid.path){
	case Qhash:
		if(!iseve())
			error(Eperm);
		if(n < Hashlen)
			error(Eshort);
		memmove(hash, va, Hashlen);
		addcap(hash);
		break;

	case Quse:
		if((ulong)n >= 1024)
			error(Etoobig);
		/* copy key to avoid a fault in hmac_xx */
		cp = secalloc(n+1);
		if(waserror()){
			secfree(cp);
			nexterror();
		}
		memmove(cp, va, n);
		cp[n] = 0;

		from = cp;
		key = strrchr(cp, '@');
		if(key == nil)
			error(Eshort);
		*key++ = 0;

		hmac_sha1((uchar*)from, strlen(from), (uchar*)key, strlen(key), hash, nil);

		/* if a from user is supplied, make sure it matches */
		to = strchr(from, '@');
		if(to == nil){
			to = from;
		} else {
			*to++ = 0;
			if(strcmp(from, up->user) != 0)
				error("capability must match user");
		}

		p = remcap(hash);
		if(p == nil){
			snprint(err, sizeof err, "invalid capability %s@%s", from, key);
			error(err);
		}
		secfree(p);

		procsetuser(to);

		secfree(cp);
		poperror();
		break;

	default:
		error(Eperm);
		break;
	}
	memset(hash, 0, Hashlen);

	return n;
}

Dev capdevtab = {
	L'¤',
	"cap",

	devreset,
	devinit,
	devshutdown,
	capattach,
	capwalk,
	capstat,
	capopen,
	devcreate,
	capclose,
	capread,
	devbread,
	capwrite,
	devbwrite,
	capremove,
	devwstat
};
