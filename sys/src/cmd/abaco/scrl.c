#include <u.h>
#include <libc.h>
#include <draw.h>
#include <memdraw.h>
#include <thread.h>
#include <cursor.h>
#include <mouse.h>
#include <keyboard.h>
#include <frame.h>
#include <plumb.h>
#include <html.h>
#include "dat.h"
#include "fns.h"

static Image *vscrtmp;
static Image *hscrtmp;

void
scrlresize(void)
{
	freeimage(vscrtmp);
	freeimage(hscrtmp);
	vscrtmp = eallocimage(display, Rect(0, 0, 32, 3*Dy(screen->r)), screen->chan, 0, DNofill);
	hscrtmp = eallocimage(display, Rect(0, 0, 3*Dx(screen->r), 32), screen->chan, 0, DNofill);
}

static
Rectangle
scrpos(Rectangle r, uint p0, uint p1, uint tot)
{
	Rectangle q;
	int h;

	q = r;
	h = q.max.y-q.min.y;
	if(tot == 0)
		return q;
	if(tot > 1024*1024){
		tot>>=10;
		p0>>=10;
		p1>>=10;
	}
	if(p0 > 0)
		q.min.y += h*p0/tot;
	if(p1 < tot)
		q.max.y -= h*(tot-p1)/tot;
	if(q.max.y < q.min.y+2){
		if(q.min.y+2 <= r.max.y)
			q.max.y = q.min.y+2;
		else
			q.min.y = q.max.y-2;
	}
	return q;
}

void
textscrdraw(Text *t)
{
	Rectangle r, r1, r2;
	Image *b;

	if(vscrtmp == nil)
		scrlresize();
	r = t->scrollr;
	b = vscrtmp;
	r1 = r;
	r1.min.x = 0;
	r1.max.x = Dx(r);
	r2 = scrpos(r1, t->org, t->org+t->nchars, t->rs.nr);
	if(!eqrect(r2, t->lastsr)){
		t->lastsr = r2;
		/* move r1, r2 to (0,0) to avoid clipping */
		r2 = rectsubpt(r2, r1.min);
		r1 = rectsubpt(r1, r1.min);
		draw(b, r1, t->cols[BORD], nil, ZP);
		draw(b, r2, t->cols[BACK], nil, ZP);
		r2.min.x = r2.max.x-1;
		draw(b, r2, t->cols[BORD], nil, ZP);
		draw(t->b, r, b, nil, Pt(0, r1.min.y));
/*flushimage(display, 1);/*BUG?*/
	}
}

void
scrsleep(uint dt)
{
	Timer	*timer;
	static Alt alts[3];

	timer = timerstart(dt);
	alts[0].c = timer->c;
	alts[0].v = nil;
	alts[0].op = CHANRCV;
	alts[1].c = mousectl->c;
	alts[1].v = &mousectl->Mouse;
	alts[1].op = CHANRCV;
	alts[2].op = CHANEND;
	for(;;)
		switch(alt(alts)){
		case 0:
			timerstop(timer);
			return;
		case 1:
			timercancel(timer);
			return;
		}
}

void
textscroll(Text *t, int but)
{
	uint p0, oldp0;
	Rectangle s;
	int y, my, h, first;

	s = insetrect(t->scrollr, 1);
	h = s.max.y-s.min.y;
	oldp0 = ~0;
	first = TRUE;
	do{
		flushimage(display, 1);
		my = mouse->xy.y;
		if(my < s.min.y)
			my = s.min.y;
		if(my >= s.max.y)
			my = s.max.y;
		if(but == 2){
			y = my;
			p0 = (vlong)t->rs.nr*(y-s.min.y)/h;
			if(p0 >= t->q1)
				p0 = textbacknl(t, p0, 2);
			if(oldp0 != p0)
				textsetorigin(t, p0, FALSE);
			oldp0 = p0;
			readmouse(mousectl);
			continue;
		}
		if(but == 1)
			p0 = textbacknl(t, t->org, (my-s.min.y)/t->font->height);
		else
			p0 = t->org+frcharofpt(t, Pt(s.max.x, my));
		if(oldp0 != p0)
			textsetorigin(t, p0, TRUE);
		oldp0 = p0;
		/* debounce */
		if(first){
			flushimage(display, 1);
			sleep(200);
			nbrecv(mousectl->c, &mousectl->Mouse);
			first = FALSE;
		}
		scrsleep(80);
	}while(mouse->buttons & (1<<(but-1)));
	while(mouse->buttons)
		readmouse(mousectl);
}

enum
{
	Scrbord = 1,
};

void
pagescrldraw(Page *p)
{
	Rectangle r1;
	int t, d, l, h;

	if(vscrtmp == nil)
		scrlresize();

	r1 = Rect(0,0,Dx(p->hscrollr), Dy(p->hscrollr));
	d = Dx(r1);
	t = Dx(p->lay->r);
	l = muldiv(p->pos.x, d, t);
	h = muldiv(p->pos.x+d, d, t);
	draw(hscrtmp, r1, tagcols[HIGH], nil, ZP);
	r1.max.x = r1.min.x+h;
	r1.min.x += l;
	r1.min.y += Scrbord;
	r1.max.y -= Scrbord;
	draw(hscrtmp, r1, tagcols[BACK], nil, ZP);

	r1 = Rect(0,0,Dx(p->vscrollr), Dy(p->vscrollr));
	d = Dy(r1);
	t = Dy(p->lay->r);
	l = muldiv(p->pos.y, d, t);
	h = muldiv(p->pos.y+d, d, t);
	draw(vscrtmp, r1, tagcols[HIGH], nil, ZP);
	r1.max.y = r1.min.y+h;
	r1.min.y += l;
	r1.max.x -= Scrbord;
	r1.min.x += Scrbord;
	draw(vscrtmp, r1, tagcols[BACK], nil, ZP);

	draw(screen, p->vscrollr, vscrtmp, nil, vscrtmp->r.min);
	draw(screen, p->hscrollr, hscrtmp, nil, hscrtmp->r.min);
}

void
pagescroll(Page *p, int but, int horizontal)
{
	uint oldp0, p0;
	Rectangle s;
	int m, om, first, d, size;
	int smin, smax, ss, *pos;

	if(horizontal){
		s = insetrect(p->hscrollr, 1);
		ss = s.max.x - s.min.x;
		d = Dx(p->r);
		size = Dx(p->lay->r);
		p0 = p->pos.x;
		pos = &p->pos.x;
		smin =  s.min.x;
		smax = s.max.x;
		om = mouse->xy.x;
	}else{
		s = insetrect(p->vscrollr, 1);
		ss = s.max.y-s.min.y;
		d = Dy(p->r);
		size = Dy(p->lay->r);
		p0 = p->pos.y;
		pos = &p->pos.y;
		smin =  s.min.y;
		smax = s.max.y;
		om = mouse->xy.y;
	}
	oldp0 = ~0;
	first = TRUE;
	do{
		flushimage(display, 1);
		if(horizontal)
			m = mouse->xy.x;
		else
			m = mouse->xy.y;

		if(but != 2){
			if(m > om)
				m += (m-om)*Panspeed;
			else if(m < om)
				m -= (om-m)*Panspeed;
		}

		if(m < smin)
			m = smin;
		if(m > smax)
			m = smax;

		om = m;
		if(but == 2){
			p0 = muldiv(m-smin, size, ss);
			p0 = max(p0, 0);
			p0 = min(p0,size-d);
			if(oldp0 != p0){
				*pos = p0;
				pageredraw(p);
			}
			oldp0 = p0;
			readmouse(mousectl);
			continue;
		}
		if(but == 1)
			p0 -= (d/2);
		else
			p0 += d/2;
		p0 = min(p0, size-d);
		p0 = max(p0, 0);
		if(oldp0 != p0){
			*pos = p0;
			pageredraw(p);
		}
		oldp0 = p0;
		/* debounce */
		if(first){
			flushimage(display, 1);
			sleep(200);
			nbrecv(mousectl->c, &mousectl->Mouse);
			first = FALSE;
		}
		scrsleep(80);
	}while(mouse->buttons & (1<<(but-1)));
	while(mouse->buttons)
		readmouse(mousectl);
}

int
pagescrollxy(Page *p, int x, int y)
{
	int scrled;

	scrled = FALSE;
	if(x != 0){
		p->pos.x += x;
		p->pos.x = max(p->pos.x, 0);
		p->pos.x = min(p->pos.x, Dx(p->lay->r)-Dx(p->r));
		scrled =TRUE;
	}
	if(y != 0){
		p->pos.y += y;
		p->pos.y = max(p->pos.y, 0);
		p->pos.y = min(p->pos.y, Dy(p->lay->r)-Dy(p->r));
		scrled =TRUE;
	}
	return scrled;
}
