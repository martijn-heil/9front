#define EXTERN
#include "gc.h"

void
listinit(void)
{

	fmtinstall('A', Aconv);
	fmtinstall('B', Bconv);
	fmtinstall('P', Pconv);
	fmtinstall('S', Sconv);
	fmtinstall('D', Dconv);
	fmtinstall('R', Rconv);
}

int
Bconv(Fmt *fp)
{
	Bits bits;
	int i;

	bits = va_arg(fp->args, Bits);
	while(bany(&bits)) {
		i = bnum(bits);
		bits.b[i/32] &= ~(1L << (i%32));
		if(var[i].sym == S)
			fmtprint(fp, "$%lld ", var[i].offset);
		else
			fmtprint(fp, "%s ", var[i].sym->name);
	}
	return 0;
}

int
Pconv(Fmt *fp)
{
	Prog *p;

	p = va_arg(fp->args, Prog*);
	if(p->as == ADATA)
		return fmtprint(fp, "	%A	%D/%d,%D",
			p->as, &p->from, p->from.scale, &p->to);
	else if(p->as == ATEXT)
		return fmtprint(fp, "	%A	%D,%d,%D",
			p->as, &p->from, p->from.scale, &p->to);
	else
		return fmtprint(fp, "	%A	%D,%D",
			p->as, &p->from, &p->to);
}

int
Aconv(Fmt *fp)
{
	int i;

	i = va_arg(fp->args, int);
	return fmtstrcpy(fp, anames[i]);
}

int
Dconv(Fmt *fp)
{
	char str[40];
	Adr *a;
	int i;

	a = va_arg(fp->args, Adr*);
	i = a->type;
	if(i >= D_INDIR) {
		if(a->offset)
			snprint(str, sizeof str, "%lld(%R)", a->offset, i-D_INDIR);
		else
			snprint(str, sizeof str, "(%R)", i-D_INDIR);
		goto brk;
	}
	switch(i) {

	default:
		if(a->offset)
			snprint(str, sizeof str, "$%lld,%R", a->offset, i);
		else
			snprint(str, sizeof str, "%R", i);
		break;

	case D_NONE:
		str[0] = 0;
		break;

	case D_BRANCH:
		snprint(str, sizeof str, "%lld(PC)", a->offset-pc);
		break;

	case D_EXTERN:
		snprint(str, sizeof str, "%s+%lld(SB)", a->sym->name, a->offset);
		break;

	case D_STATIC:
		snprint(str, sizeof str, "%s<>+%lld(SB)", a->sym->name, a->offset);
		break;

	case D_AUTO:
		snprint(str, sizeof str, "%s+%lld(SP)", a->sym->name, a->offset);
		break;

	case D_PARAM:
		if(a->sym)
			snprint(str, sizeof str, "%s+%lld(FP)", a->sym->name, a->offset);
		else
			snprint(str, sizeof str, "%lld(FP)", a->offset);
		break;

	case D_CONST:
		snprint(str, sizeof str, "$%lld", a->offset);
		break;

	case D_FCONST:
		snprint(str, sizeof str, "$(%.17e)", a->dval);
		break;

	case D_SCONST:
		snprint(str, sizeof str, "$\"%S\"", a->sval);
		break;

	case D_ADDR:
		a->type = a->index;
		a->index = D_NONE;
		snprint(str, sizeof str, "$%D", a);
		a->index = a->type;
		a->type = D_ADDR;
		goto conv;
	}
brk:
	if(a->index != D_NONE) 
		return fmtprint(fp, "%s(%R*%d)", str, (int)a->index, (int)a->scale);
conv:
	return fmtstrcpy(fp, str);
}

char*	regstr[] =
{
	"AL",		/* [D_AL] */
	"CL",
	"DL",
	"BL",
	"SPB",
	"BPB",
	"SIB",
	"DIB",
	"R8B",
	"R9B",
	"R10B",
	"R11B",
	"R12B",
	"R13B",
	"R14B",
	"R15B",

	"AX",		/* [D_AX] */
	"CX",
	"DX",
	"BX",
	"SP",
	"BP",
	"SI",
	"DI",
	"R8",
	"R9",
	"R10",
	"R11",
	"R12",
	"R13",
	"R14",
	"R15",

	"AH",
	"CH",
	"DH",
	"BH",

	"F0",		/* [D_F0] */
	"F1",
	"F2",
	"F3",
	"F4",
	"F5",
	"F6",
	"F7",

	"M0",
	"M1",
	"M2",
	"M3",
	"M4",
	"M5",
	"M6",
	"M7",

	"X0",
	"X1",
	"X2",
	"X3",
	"X4",
	"X5",
	"X6",
	"X7",
	"X8",
	"X9",
	"X10",
	"X11",
	"X12",
	"X13",
	"X14",
	"X15",

	"CS",		/* [D_CS] */
	"SS",
	"DS",
	"ES",
	"FS",
	"GS",

	"GDTR",		/* [D_GDTR] */
	"IDTR",		/* [D_IDTR] */
	"LDTR",		/* [D_LDTR] */
	"MSW",		/* [D_MSW] */
	"TASK",		/* [D_TASK] */

	"CR0",		/* [D_CR] */
	"CR1",
	"CR2",
	"CR3",
	"CR4",
	"CR5",
	"CR6",
	"CR7",
	"CR8",
	"CR9",
	"CR10",
	"CR11",
	"CR12",
	"CR13",
	"CR14",
	"CR15",

	"DR0",		/* [D_DR] */
	"DR1",
	"DR2",
	"DR3",
	"DR4",
	"DR5",
	"DR6",
	"DR7",

	"TR0",		/* [D_TR] */
	"TR1",
	"TR2",
	"TR3",
	"TR4",
	"TR5",
	"TR6",
	"TR7",

	"NONE",		/* [D_NONE] */
};

int
Rconv(Fmt *fp)
{
	int r;

	r = va_arg(fp->args, int);
	if(r >= D_AL && r <= D_NONE)
		return fmtprint(fp, "%s", regstr[r-D_AL]);
	else
		return fmtprint(fp, "gok(%d)", r);
}

int
Sconv(Fmt *fp)
{
	int i, c;
	char str[30], *p, *a;

	a = va_arg(fp->args, char*);
	p = str;
	for(i=0; i<sizeof(double); i++) {
		c = a[i] & 0xff;
		if(c >= 'a' && c <= 'z' ||
		   c >= 'A' && c <= 'Z' ||
		   c >= '0' && c <= '9') {
			*p++ = c;
			continue;
		}
		*p++ = '\\';
		switch(c) {
		default:
			if(c < 040 || c >= 0177)
				break;	/* not portable */
			p[-1] = c;
			continue;
		case 0:
			*p++ = 'z';
			continue;
		case '\\':
		case '"':
			*p++ = c;
			continue;
		case '\n':
			*p++ = 'n';
			continue;
		case '\t':
			*p++ = 't';
			continue;
		}
		*p++ = (c>>6) + '0';
		*p++ = ((c>>3) & 7) + '0';
		*p++ = (c & 7) + '0';
	}
	*p = 0;
	return fmtstrcpy(fp, str);
}
