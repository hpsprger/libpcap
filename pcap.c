/*
 * Copyright (c) 1993, 1994, 1995, 1996, 1997, 1998
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the Computer Systems
 *	Engineering Group at Lawrence Berkeley Laboratory.
 * 4. Neither the name of the University nor of the Laboratory may be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef lint
static const char rcsid[] _U_ =
    "@(#) $Header: /tcpdump/master/libpcap/pcap.c,v 1.63.2.4 2003-11-20 01:18:38 guy Exp $ (LBL)";
#endif

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef WIN32
#include <pcap-stdinc.h>
#else /* WIN32 */
#include <sys/types.h>
#endif /* WIN32 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef WIN32
#include <unistd.h>
#endif
#include <fcntl.h>
#include <errno.h>

#ifdef HAVE_OS_PROTO_H
#include "os-proto.h"
#endif

#include "pcap-int.h"

#ifdef HAVE_DAG_API
#include <dagnew.h>
#include <dagapi.h>
#endif

int
pcap_dispatch(pcap_t *p, int cnt, pcap_handler callback, u_char *user)
{

	return p->read_op(p, cnt, callback, user);
}

/*
 * XXX - is this necessary?
 */
int
pcap_read(pcap_t *p, int cnt, pcap_handler callback, u_char *user)
{

	return p->read_op(p, cnt, callback, user);
}

int
pcap_loop(pcap_t *p, int cnt, pcap_handler callback, u_char *user)
{
	register int n;

	for (;;) {
		if (p->sf.rfile != NULL) {
			/*
			 * 0 means EOF, so don't loop if we get 0.
			 */
			n = pcap_offline_read(p, cnt, callback, user);
		} else {
			/*
			 * XXX keep reading until we get something
			 * (or an error occurs)
			 */
			do {
				n = p->read_op(p, cnt, callback, user);
			} while (n == 0);
		}
		if (n <= 0)
			return (n);
		if (cnt > 0) {
			cnt -= n;
			if (cnt <= 0)
				return (0);
		}
	}
}

struct singleton {
	struct pcap_pkthdr *hdr;
	const u_char *pkt;
};


static void
pcap_oneshot(u_char *userData, const struct pcap_pkthdr *h, const u_char *pkt)
{
	struct singleton *sp = (struct singleton *)userData;
	*sp->hdr = *h;
	sp->pkt = pkt;
}

const u_char *
pcap_next(pcap_t *p, struct pcap_pkthdr *h)
{
	struct singleton s;

	s.hdr = h;
	if (pcap_dispatch(p, 1, pcap_oneshot, (u_char*)&s) <= 0)
		return (0);
	return (s.pkt);
}

struct pkt_for_fakecallback {
	struct pcap_pkthdr *hdr;
	const u_char **pkt;
};

static void
pcap_fakecallback(u_char *userData, const struct pcap_pkthdr *h,
    const u_char *pkt)
{
	struct pkt_for_fakecallback *sp = (struct pkt_for_fakecallback *)userData;

	*sp->hdr = *h;
	*sp->pkt = pkt;
}

int 
pcap_next_ex(pcap_t *p, struct pcap_pkthdr **pkt_header,
    const u_char **pkt_data)
{
	struct pkt_for_fakecallback s;

	s.hdr = &p->pcap_header;
	s.pkt = pkt_data;

	/* Saves a pointer to the packet headers */
	*pkt_header= &p->pcap_header;

	if (p->sf.rfile != NULL) {
		int status;

		/* We are on an offline capture */
		status = pcap_offline_read(p, 1, pcap_fakecallback,
		    (u_char *)&s);

		/*
		 * Return codes for pcap_offline_read() are:
		 *   -  0: EOF
		 *   - -1: error
		 *   - >1: OK
		 * The first one ('0') conflicts with the return code of
		 * 0 from pcap_read() meaning "no packets arrived before
		 * the timeout expired", so we map it to -2 so you can
		 * distinguish between an EOF from a savefile and a
		 * "no packets arrived before the timeout expired, try
		 * again" from a live capture.
		 */
		if (status == 0)
			return (-2);
		else
			return (status);
	}

	/*
	 * Return codes for pcap_read() are:
	 *   -  0: timeout
	 *   - -1: error
	 *   - -2: loop was broken out of with pcap_breakloop()
	 *   - >1: OK
	 * The first one ('0') conflicts with the return code of 0 from
	 * pcap_offline_read() meaning "end of file".
	*/
	return (p->read_op(p, 1, pcap_fakecallback, (u_char *)&s));
}

/*
 * Force the loop in "pcap_read()" or "pcap_read_offline()" to terminate.
 */
void
pcap_breakloop(pcap_t *p)
{
	p->break_loop = 1;
}

int
pcap_datalink(pcap_t *p)
{
	return (p->linktype);
}

int
pcap_list_datalinks(pcap_t *p, int **dlt_buffer)
{
	if (p->dlt_count == 0) {
		/*
		 * We couldn't fetch the list of DLTs, which means
		 * this platform doesn't support changing the
		 * DLT for an interface.  Return a list of DLTs
		 * containing only the DLT this device supports.
		 */
		*dlt_buffer = (int*)malloc(sizeof(**dlt_buffer));
		if (*dlt_buffer == NULL) {
			(void)snprintf(p->errbuf, sizeof(p->errbuf),
			    "malloc: %s", pcap_strerror(errno));
			return (-1);
		}
		**dlt_buffer = p->linktype;
		return (1);
	} else {
		*dlt_buffer = (int*)malloc(sizeof(**dlt_buffer) * p->dlt_count);
		if (*dlt_buffer == NULL) {
			(void)snprintf(p->errbuf, sizeof(p->errbuf),
			    "malloc: %s", pcap_strerror(errno));
			return (-1);
		}
		(void)memcpy(*dlt_buffer, p->dlt_list,
		    sizeof(**dlt_buffer) * p->dlt_count);
		return (p->dlt_count);
	}
}

int
pcap_set_datalink(pcap_t *p, int dlt)
{
	int i;
	const char *dlt_name;

	if (p->dlt_count == 0 || p->set_datalink_op == NULL) {
		/*
		 * We couldn't fetch the list of DLTs, or we don't
		 * have a "set datalink" operation, which means
		 * this platform doesn't support changing the
		 * DLT for an interface.  Check whether the new
		 * DLT is the one this interface supports.
		 */
		if (p->linktype != dlt)
			goto unsupported;

		/*
		 * It is, so there's nothing we need to do here.
		 */
		return (0);
	}
	for (i = 0; i < p->dlt_count; i++)
		if (p->dlt_list[i] == dlt)
			break;
	if (i >= p->dlt_count)
		goto unsupported;
	if (p->set_datalink_op(p, dlt) == -1)
		return (-1);
	p->linktype = dlt;
	return (0);

unsupported:
	dlt_name = pcap_datalink_val_to_name(dlt);
	if (dlt_name != NULL) {
		(void) snprintf(p->errbuf, sizeof(p->errbuf),
		    "%s is not one of the DLTs supported by this device",
		    dlt_name);
	} else {
		(void) snprintf(p->errbuf, sizeof(p->errbuf),
		    "DLT %d is not one of the DLTs supported by this device",
		    dlt);
	}
	return (-1);
}

struct dlt_choice {
	const char *name;
	const char *description;
	int	dlt;
};

#define DLT_CHOICE(code, description) { #code, description, code }
#define DLT_CHOICE_SENTINEL { NULL, NULL, 0 }

static struct dlt_choice dlt_choices[] = {
	DLT_CHOICE(DLT_NULL, "BSD loopback"),
	DLT_CHOICE(DLT_EN10MB, "Ethernet"),
	DLT_CHOICE(DLT_IEEE802, "Token ring"),
	DLT_CHOICE(DLT_ARCNET, "ARCNET"),
	DLT_CHOICE(DLT_SLIP, "SLIP"),
	DLT_CHOICE(DLT_PPP, "PPP"),
	DLT_CHOICE(DLT_FDDI, "FDDI"),
	DLT_CHOICE(DLT_ATM_RFC1483, "RFC 1483 IP-over-ATM"),
	DLT_CHOICE(DLT_RAW, "Raw IP"),
	DLT_CHOICE(DLT_SLIP_BSDOS, "BSD/OS SLIP"),
	DLT_CHOICE(DLT_PPP_BSDOS, "BSD/OS PPP"),
	DLT_CHOICE(DLT_ATM_CLIP, "Linux Classical IP-over-ATM"),
	DLT_CHOICE(DLT_PPP_SERIAL, "PPP over serial"),
	DLT_CHOICE(DLT_PPP_ETHER, "PPPoE"),
	DLT_CHOICE(DLT_C_HDLC, "Cisco HDLC"),
	DLT_CHOICE(DLT_IEEE802_11, "802.11"),
	DLT_CHOICE(DLT_FRELAY, "Frame Relay"),
	DLT_CHOICE(DLT_LOOP, "OpenBSD loopback"),
	DLT_CHOICE(DLT_ENC, "OpenBSD encapsulated IP"),
	DLT_CHOICE(DLT_LINUX_SLL, "Linux cooked"),
	DLT_CHOICE(DLT_LTALK, "Localtalk"),
	DLT_CHOICE(DLT_PFLOG, "OpenBSD pflog file"),
	DLT_CHOICE(DLT_PRISM_HEADER, "802.11 plus Prism header"),
	DLT_CHOICE(DLT_IP_OVER_FC, "RFC 2625 IP-over-Fibre Channel"),
	DLT_CHOICE(DLT_SUNATM, "Sun raw ATM"),
	DLT_CHOICE(DLT_IEEE802_11_RADIO, "802.11 plus radio information header"),
	DLT_CHOICE(DLT_ARCNET_LINUX, "Linux ARCNET"),
	DLT_CHOICE(DLT_LINUX_IRDA, "Linux IrDA"),
	DLT_CHOICE_SENTINEL
};

/*
 * This array is designed for mapping upper and lower case letter
 * together for a case independent comparison.  The mappings are
 * based upon ascii character sequences.
 */
static const u_char charmap[] = {
	(u_char)'\000', (u_char)'\001', (u_char)'\002', (u_char)'\003',
	(u_char)'\004', (u_char)'\005', (u_char)'\006', (u_char)'\007',
	(u_char)'\010', (u_char)'\011', (u_char)'\012', (u_char)'\013',
	(u_char)'\014', (u_char)'\015', (u_char)'\016', (u_char)'\017',
	(u_char)'\020', (u_char)'\021', (u_char)'\022', (u_char)'\023',
	(u_char)'\024', (u_char)'\025', (u_char)'\026', (u_char)'\027',
	(u_char)'\030', (u_char)'\031', (u_char)'\032', (u_char)'\033',
	(u_char)'\034', (u_char)'\035', (u_char)'\036', (u_char)'\037',
	(u_char)'\040', (u_char)'\041', (u_char)'\042', (u_char)'\043',
	(u_char)'\044', (u_char)'\045', (u_char)'\046', (u_char)'\047',
	(u_char)'\050', (u_char)'\051', (u_char)'\052', (u_char)'\053',
	(u_char)'\054', (u_char)'\055', (u_char)'\056', (u_char)'\057',
	(u_char)'\060', (u_char)'\061', (u_char)'\062', (u_char)'\063',
	(u_char)'\064', (u_char)'\065', (u_char)'\066', (u_char)'\067',
	(u_char)'\070', (u_char)'\071', (u_char)'\072', (u_char)'\073',
	(u_char)'\074', (u_char)'\075', (u_char)'\076', (u_char)'\077',
	(u_char)'\100', (u_char)'\141', (u_char)'\142', (u_char)'\143',
	(u_char)'\144', (u_char)'\145', (u_char)'\146', (u_char)'\147',
	(u_char)'\150', (u_char)'\151', (u_char)'\152', (u_char)'\153',
	(u_char)'\154', (u_char)'\155', (u_char)'\156', (u_char)'\157',
	(u_char)'\160', (u_char)'\161', (u_char)'\162', (u_char)'\163',
	(u_char)'\164', (u_char)'\165', (u_char)'\166', (u_char)'\167',
	(u_char)'\170', (u_char)'\171', (u_char)'\172', (u_char)'\133',
	(u_char)'\134', (u_char)'\135', (u_char)'\136', (u_char)'\137',
	(u_char)'\140', (u_char)'\141', (u_char)'\142', (u_char)'\143',
	(u_char)'\144', (u_char)'\145', (u_char)'\146', (u_char)'\147',
	(u_char)'\150', (u_char)'\151', (u_char)'\152', (u_char)'\153',
	(u_char)'\154', (u_char)'\155', (u_char)'\156', (u_char)'\157',
	(u_char)'\160', (u_char)'\161', (u_char)'\162', (u_char)'\163',
	(u_char)'\164', (u_char)'\165', (u_char)'\166', (u_char)'\167',
	(u_char)'\170', (u_char)'\171', (u_char)'\172', (u_char)'\173',
	(u_char)'\174', (u_char)'\175', (u_char)'\176', (u_char)'\177',
	(u_char)'\200', (u_char)'\201', (u_char)'\202', (u_char)'\203',
	(u_char)'\204', (u_char)'\205', (u_char)'\206', (u_char)'\207',
	(u_char)'\210', (u_char)'\211', (u_char)'\212', (u_char)'\213',
	(u_char)'\214', (u_char)'\215', (u_char)'\216', (u_char)'\217',
	(u_char)'\220', (u_char)'\221', (u_char)'\222', (u_char)'\223',
	(u_char)'\224', (u_char)'\225', (u_char)'\226', (u_char)'\227',
	(u_char)'\230', (u_char)'\231', (u_char)'\232', (u_char)'\233',
	(u_char)'\234', (u_char)'\235', (u_char)'\236', (u_char)'\237',
	(u_char)'\240', (u_char)'\241', (u_char)'\242', (u_char)'\243',
	(u_char)'\244', (u_char)'\245', (u_char)'\246', (u_char)'\247',
	(u_char)'\250', (u_char)'\251', (u_char)'\252', (u_char)'\253',
	(u_char)'\254', (u_char)'\255', (u_char)'\256', (u_char)'\257',
	(u_char)'\260', (u_char)'\261', (u_char)'\262', (u_char)'\263',
	(u_char)'\264', (u_char)'\265', (u_char)'\266', (u_char)'\267',
	(u_char)'\270', (u_char)'\271', (u_char)'\272', (u_char)'\273',
	(u_char)'\274', (u_char)'\275', (u_char)'\276', (u_char)'\277',
	(u_char)'\300', (u_char)'\341', (u_char)'\342', (u_char)'\343',
	(u_char)'\344', (u_char)'\345', (u_char)'\346', (u_char)'\347',
	(u_char)'\350', (u_char)'\351', (u_char)'\352', (u_char)'\353',
	(u_char)'\354', (u_char)'\355', (u_char)'\356', (u_char)'\357',
	(u_char)'\360', (u_char)'\361', (u_char)'\362', (u_char)'\363',
	(u_char)'\364', (u_char)'\365', (u_char)'\366', (u_char)'\367',
	(u_char)'\370', (u_char)'\371', (u_char)'\372', (u_char)'\333',
	(u_char)'\334', (u_char)'\335', (u_char)'\336', (u_char)'\337',
	(u_char)'\340', (u_char)'\341', (u_char)'\342', (u_char)'\343',
	(u_char)'\344', (u_char)'\345', (u_char)'\346', (u_char)'\347',
	(u_char)'\350', (u_char)'\351', (u_char)'\352', (u_char)'\353',
	(u_char)'\354', (u_char)'\355', (u_char)'\356', (u_char)'\357',
	(u_char)'\360', (u_char)'\361', (u_char)'\362', (u_char)'\363',
	(u_char)'\364', (u_char)'\365', (u_char)'\366', (u_char)'\367',
	(u_char)'\370', (u_char)'\371', (u_char)'\372', (u_char)'\373',
	(u_char)'\374', (u_char)'\375', (u_char)'\376', (u_char)'\377',
};

int
pcap_strcasecmp(const char *s1, const char *s2)
{
	register const u_char	*cm = charmap,
				*us1 = (u_char *)s1,
				*us2 = (u_char *)s2;

	while (cm[*us1] == cm[*us2++])
		if (*us1++ == '\0')
			return(0);
	return (cm[*us1] - cm[*--us2]);
}

int
pcap_datalink_name_to_val(const char *name)
{
	int i;

	for (i = 0; dlt_choices[i].name != NULL; i++) {
		if (pcap_strcasecmp(dlt_choices[i].name + sizeof("DLT_") - 1,
		    name) == 0)
			return (dlt_choices[i].dlt);
	}
	return (-1);
}

const char *
pcap_datalink_val_to_name(int dlt)
{
	int i;

	for (i = 0; dlt_choices[i].name != NULL; i++) {
		if (dlt_choices[i].dlt == dlt)
			return (dlt_choices[i].name + sizeof("DLT_") - 1);
	}
	return (NULL);
}

const char *
pcap_datalink_val_to_description(int dlt)
{
	int i;

	for (i = 0; dlt_choices[i].name != NULL; i++) {
		if (dlt_choices[i].dlt == dlt)
			return (dlt_choices[i].description);
	}
	return (NULL);
}

int
pcap_snapshot(pcap_t *p)
{
	return (p->snapshot);
}

int
pcap_is_swapped(pcap_t *p)
{
	return (p->sf.swapped);
}

int
pcap_major_version(pcap_t *p)
{
	return (p->sf.version_major);
}

int
pcap_minor_version(pcap_t *p)
{
	return (p->sf.version_minor);
}

FILE *
pcap_file(pcap_t *p)
{
	return (p->sf.rfile);
}

int
pcap_fileno(pcap_t *p)
{
#ifndef WIN32
	return (p->fd);
#else
	if (p->adapter != NULL)
		return ((int)(DWORD)p->adapter->hFile);
	else
		return (-1);
#endif
}

void
pcap_perror(pcap_t *p, char *prefix)
{
	fprintf(stderr, "%s: %s\n", prefix, p->errbuf);
}

char *
pcap_geterr(pcap_t *p)
{
	return (p->errbuf);
}

/*
 * NOTE: in the future, these may need to call platform-dependent routines,
 * e.g. on platforms with memory-mapped packet-capture mechanisms where
 * "pcap_read()" uses "select()" or "poll()" to wait for packets to arrive.
 */
int
pcap_getnonblock(pcap_t *p, char *errbuf)
{
#ifndef WIN32
	int fdflags;
#endif

	if (p->sf.rfile != NULL) {
		/*
		 * This is a savefile, not a live capture file, so
		 * never say it's in non-blocking mode.
		 */
		return (0);
	}
#ifndef WIN32
	fdflags = fcntl(p->fd, F_GETFL, 0);
	if (fdflags == -1) {
		snprintf(p->errbuf, PCAP_ERRBUF_SIZE, "F_GETFL: %s",
		    pcap_strerror(errno));
		return (-1);
	}
	if (fdflags & O_NONBLOCK)
		return (1);
	else
		return (0);
#else
	return (p->nonblock);
#endif
}

int
pcap_setnonblock(pcap_t *p, int nonblock, char *errbuf)
{
#ifndef WIN32
	int fdflags;
#else
	int newtimeout;
#endif

	if (p->sf.rfile != NULL) {
		/*
		 * This is a savefile, not a live capture file, so
		 * ignore requests to put it in non-blocking mode.
		 */
		return (0);
	}

#if HAVE_DAG_API
	if (nonblock) {
		p->md.dag_offset_flags |= DAGF_NONBLOCK;
	} else {
		p->md.dag_offset_flags &= ~DAGF_NONBLOCK;
	}
#endif /* HAVE_DAG_API */

#ifndef WIN32
	fdflags = fcntl(p->fd, F_GETFL, 0);
	if (fdflags == -1) {
		snprintf(p->errbuf, PCAP_ERRBUF_SIZE, "F_GETFL: %s",
		    pcap_strerror(errno));
		return (-1);
	}
	if (nonblock)
		fdflags |= O_NONBLOCK;
	else
		fdflags &= ~O_NONBLOCK;
	if (fcntl(p->fd, F_SETFL, fdflags) == -1) {
		snprintf(p->errbuf, PCAP_ERRBUF_SIZE, "F_SETFL: %s",
		    pcap_strerror(errno));
		return (-1);
	}
#else
	if (nonblock) {
		/*
		 * Set the read timeout to -1 for non-blocking mode.
		 */
		newtimeout = -1;
	} else {
		/*
		 * Restore the timeout set when the device was opened.
		 * (Note that this may be -1, in which case we're not
		 * really leaving non-blocking mode.)
		 */
		newtimeout = p->timeout;
	}
	if (!PacketSetReadTimeout(p->adapter, newtimeout)) {
		snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
		    "PacketSetReadTimeout: %s", pcap_win32strerror());
		return (-1);
	}
	p->nonblock = (newtimeout == -1);
#endif
	return (0);
}

#ifdef WIN32
/*
 * Generate a string for the last Win32-specific error (i.e. an error generated when 
 * calling a Win32 API).
 * For errors occurred during standard C calls, we still use pcap_strerror()
 */
char *
pcap_win32strerror(void)
{
	DWORD error;
	static char errbuf[PCAP_ERRBUF_SIZE+1];
	int errlen;

	error = GetLastError();
	FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, error, 0, errbuf,
	    PCAP_ERRBUF_SIZE, NULL);

	/*
	 * "FormatMessage()" "helpfully" sticks CR/LF at the end of the
	 * message.  Get rid of it.
	 */
	errlen = strlen(errbuf);
	if (errlen >= 2) {
		errbuf[errlen - 1] = '\0';
		errbuf[errlen - 2] = '\0';
	}
	return (errbuf);
}
#endif

/*
 * Not all systems have strerror().
 */
char *
pcap_strerror(int errnum)
{
#ifdef HAVE_STRERROR
	return (strerror(errnum));
#else
	extern int sys_nerr;
	extern const char *const sys_errlist[];
	static char ebuf[20];

	if ((unsigned int)errnum < sys_nerr)
		return ((char *)sys_errlist[errnum]);
	(void)snprintf(ebuf, sizeof ebuf, "Unknown error: %d", errnum);
	return(ebuf);
#endif
}

int
pcap_setfilter(pcap_t *p, struct bpf_program *fp)
{
	return p->setfilter_op(p, fp);
}

int
pcap_stats(pcap_t *p, struct pcap_stat *ps)
{
	return p->stats_op(p, ps);
}

static int
pcap_stats_dead(pcap_t *p, struct pcap_stat *ps)
{
	snprintf(p->errbuf, PCAP_ERRBUF_SIZE,
	    "Statistics aren't available from a pcap_open_dead pcap_t");
	return (-1);
}

static void
pcap_close_dead(pcap_t *p)
{
	/* Nothing to do. */
}

pcap_t *
pcap_open_dead(int linktype, int snaplen)
{
	pcap_t *p;

	p = malloc(sizeof(*p));
	if (p == NULL)
		return NULL;
	memset (p, 0, sizeof(*p));
	p->snapshot = snaplen;
	p->linktype = linktype;
	p->stats_op = pcap_stats_dead;
	p->close_op = pcap_close_dead;
	return p;
}

void
pcap_close(pcap_t *p)
{
	p->close_op(p);
	if (p->dlt_list != NULL)
		free(p->dlt_list);
	pcap_freecode(&p->fcode);
	free(p);
}

/*
 * We make the version string static, and return a pointer to it, rather
 * than exporting the version string directly.  On at least some UNIXes,
 * if you import data from a shared library into an program, the data is
 * bound into the program binary, so if the string in the version of the
 * library with which the program was linked isn't the same as the
 * string in the version of the library with which the program is being
 * run, various undesirable things may happen (warnings, the string
 * being the one from the version of the library with which the program
 * was linked, or even weirder things, such as the string being the one
 * from the library but being truncated).
 */
#ifdef WIN32
/*
 * XXX - it'd be nice if we could somehow generate the WinPcap and libpcap
 * version numbers when building WinPcap.  (It'd be nice to do so for
 * the packet.dll version number as well.)
 */
static const char wpcap_version_string[] = "3.0";
static const char pcap_version_string_fmt[] =
    "WinPcap version %s, based on libpcap version 0.8";
static const char pcap_version_string_packet_dll_fmt[] =
    "WinPcap version %s (packet.dll version %s), based on libpcap version 0.8";
static char *pcap_version_string;

const char *
pcap_lib_version(void)
{
	char *packet_version_string;
	size_t pcap_version_string_len;

	if (pcap_version_string == NULL) {
		/*
		 * Generate the version string.
		 */
		packet_version_string = PacketGetVersion();
		if (strcmp(wpcap_version_string, packet_version_string) == 0) {
			/*
			 * WinPcap version string and packet.dll version
			 * string are the same; just report the WinPcap
			 * version.
			 */
			pcap_version_string_len =
			    (sizeof pcap_version_string_fmt - 2) +
			    strlen(wpcap_version_string);
			pcap_version_string = malloc(pcap_version_string_len);
			sprintf(pcap_version_string, pcap_version_string_fmt,
			    wpcap_version_string);
		} else {
			/*
			 * WinPcap version string and packet.dll version
			 * string are different; that shouldn't be the
			 * case (the two libraries should come from the
			 * same version of WinPcap), so we report both
			 * versions.
			 */
			pcap_version_string_len =
			    (sizeof pcap_version_string_packet_dll_fmt - 4) +
			    strlen(wpcap_version_string) +
			    strlen(packet_version_string);
			pcap_version_string = malloc(pcap_version_string_len);
			sprintf(pcap_version_string,
			    pcap_version_string_packet_dll_fmt,
			    wpcap_version_string, packet_version_string);
		}
	}
	return (pcap_version_string);
}
#else
#include "version.h"

const char *
pcap_lib_version(void)
{
	return (pcap_version_string);
}
#endif
