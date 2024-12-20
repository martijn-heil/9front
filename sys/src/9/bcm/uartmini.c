/*
 * bcm2835 mini uart (UART1)
 */

#include "u.h"
#include "../port/lib.h"
#include "../port/error.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "io.h"

#define AUXREGS		(VIRTIO+0x215000)
#define	OkLed		16
#define	TxPin		14
#define	RxPin		15

/* AUX regs */
enum {
	Irq	= 0x00>>2,
		UartIrq	= 1<<0,
	Enables	= 0x04>>2,
		UartEn	= 1<<0,
	MuIo	= 0x40>>2,
	MuIer	= 0x44>>2,
		RxIen	= 1<<0,
		TxIen	= 1<<1,
	MuIir	= 0x48>>2,
	MuLcr	= 0x4c>>2,
		Bitsmask= 3<<0,
		Bits7	= 2<<0,
		Bits8	= 3<<0,
	MuMcr	= 0x50>>2,
		RtsN	= 1<<1,
	MuLsr	= 0x54>>2,
		TxDone	= 1<<6,
		TxRdy	= 1<<5,
		RxRdy	= 1<<0,
	MuCntl	= 0x60>>2,
		CtsFlow	= 1<<3,
		TxEn	= 1<<1,
		RxEn	= 1<<0,
	MuBaud	= 0x68>>2,
};

extern PhysUart miniphysuart;

static Uart miniuart = {
	.regs	= (u32int*)AUXREGS,
	.name	= "uart1",
	.freq	= 250000000,
	.baud	= 115200,
	.phys	= &miniphysuart,
};

static int baud(Uart*, int);

static void
interrupt(Ureg*, void *arg)
{
	Uart *uart;
	u32int *ap;

	uart = arg;
	ap = (u32int*)uart->regs;

	coherence();
	if(0 && (ap[Irq] & UartIrq) == 0)
		return;
	if(ap[MuLsr] & TxRdy)
		uartkick(uart);
	if(ap[MuLsr] & RxRdy){
		do{
			uartrecv(uart, ap[MuIo] & 0xFF);
		}while(ap[MuLsr] & RxRdy);
	}
	coherence();
}

static Uart*
pnp(void)
{
	return &miniuart;
}

static void
enable(Uart *uart, int ie)
{
	u32int *ap;

	ap = (u32int*)uart->regs;
	delay(10);
	gpiosel(TxPin, Alt5);
	gpiosel(RxPin, Alt5);
	gpiopulloff(TxPin);
	gpiopullup(RxPin);
	ap[Enables] |= UartEn;
	ap[MuIir] = 6;
	ap[MuLcr] = Bits8;
	ap[MuCntl] = TxEn|RxEn;
	baud(uart, uart->baud);
	if(ie){
		intrenable(IRQaux, interrupt, uart, BUSUNKNOWN, uart->name);
		ap[MuIer] = RxIen|TxIen;
	}else
		ap[MuIer] = 0;
}

static void
disable(Uart *uart)
{
	u32int *ap;

	ap = (u32int*)uart->regs;
	ap[MuCntl] = 0;
	ap[MuIer] = 0;
}

static void
kick(Uart *uart)
{
	u32int *ap;

	coherence();
	ap = (u32int*)uart->regs;
	while(ap[MuLsr] & TxRdy){
		if(uart->op >= uart->oe && uartstageoutput(uart) == 0)
			break;
		ap[MuIo] = *(uart->op++);
	}
	if(ap[MuLsr] & TxDone)
		ap[MuIer] &= ~TxIen;
	else
		ap[MuIer] |= TxIen;
	coherence();
}

/* TODO */
static void
dobreak(Uart *uart, int ms)
{
	USED(uart, ms);
}

static int
baud(Uart *uart, int n)
{
	u32int *ap;

	ap = (u32int*)uart->regs;
	if(uart->freq == 0 || n <= 0)
		return -1;
	ap[MuBaud] = (uart->freq + 4*n - 1) / (8 * n) - 1;
	uart->baud = n;
	return 0;
}

static int
bits(Uart *uart, int n)
{
	u32int *ap;
	int set;

	ap = (u32int*)uart->regs;
	switch(n){
	case 7:
		set = Bits7;
		break;
	case 8:
		set = Bits8;
		break;
	default:
		return -1;
	}
	ap[MuLcr] = (ap[MuLcr] & ~Bitsmask) | set;
	uart->bits = n;
	return 0;
}

static int
stop(Uart *uart, int n)
{
	if(n != 1)
		return -1;
	uart->stop = n;
	return 0;
}

static int
parity(Uart *uart, int n)
{
	if(n != 'n')
		return -1;
	uart->parity = n;
	return 0;
}

/*
 * cts/rts flow control
 *   need to bring signals to gpio pins before enabling this
 */

static void
modemctl(Uart *uart, int on)
{
	u32int *ap;

	ap = (u32int*)uart->regs;
	if(on)
		ap[MuCntl] |= CtsFlow;
	else
		ap[MuCntl] &= ~CtsFlow;
	uart->modem = on;
}

static void
rts(Uart *uart, int on)
{
	u32int *ap;

	ap = (u32int*)uart->regs;
	if(on)
		ap[MuMcr] &= ~RtsN;
	else
		ap[MuMcr] |= RtsN;
}

static void
donothing(Uart*, int)
{
}

static void
putc(Uart*, int c)
{
	u32int *ap;

	ap = (u32int*)AUXREGS;
	while((ap[MuLsr] & TxRdy) == 0)
		;
	ap[MuIo] = c;
	while((ap[MuLsr] & TxRdy) == 0)
		;
}

static int
getc(Uart*)
{
	u32int *ap;

	ap = (u32int*)AUXREGS;
	while((ap[MuLsr] & RxRdy) == 0)
		;
	return ap[MuIo] & 0xFF;
}

PhysUart miniphysuart = {
	.name		= "mini",
	.pnp		= pnp,
	.enable		= enable,
	.disable	= disable,
	.kick		= kick,
	.dobreak	= dobreak,
	.baud		= baud,
	.bits		= bits,
	.stop		= stop,
	.parity		= parity,
	.modemctl	= donothing,
	.rts		= rts,
	.dtr		= donothing,
	.fifo		= donothing,
	.getc		= getc,
	.putc		= putc,
};
