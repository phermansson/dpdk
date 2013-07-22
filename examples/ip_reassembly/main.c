/*-
 *   BSD LICENSE
 * 
 *   Copyright(c) 2010-2013 Intel Corporation. All rights reserved.
 *   All rights reserved.
 * 
 *   Redistribution and use in source and binary forms, with or without 
 *   modification, are permitted provided that the following conditions 
 *   are met:
 * 
 *     * Redistributions of source code must retain the above copyright 
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright 
 *       notice, this list of conditions and the following disclaimer in 
 *       the documentation and/or other materials provided with the 
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its 
 *       contributors may be used to endorse or promote products derived 
 *       from this software without specific prior written permission.
 * 
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT 
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR 
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT 
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, 
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT 
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, 
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY 
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/types.h>
#include <string.h>
#include <sys/queue.h>
#include <stdarg.h>
#include <errno.h>
#include <getopt.h>
#include <signal.h>

#include <rte_common.h>
#include <rte_byteorder.h>
#include <rte_log.h>
#include <rte_memory.h>
#include <rte_memcpy.h>
#include <rte_memzone.h>
#include <rte_tailq.h>
#include <rte_eal.h>
#include <rte_per_lcore.h>
#include <rte_launch.h>
#include <rte_atomic.h>
#include <rte_cycles.h>
#include <rte_prefetch.h>
#include <rte_lcore.h>
#include <rte_per_lcore.h>
#include <rte_branch_prediction.h>
#include <rte_interrupts.h>
#include <rte_pci.h>
#include <rte_random.h>
#include <rte_debug.h>
#include <rte_ether.h>
#include <rte_ethdev.h>
#include <rte_ring.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_malloc.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_udp.h>
#include <rte_string_fns.h>
#include "main.h"

#define APP_LOOKUP_EXACT_MATCH          0
#define APP_LOOKUP_LPM                  1
#define DO_RFC_1812_CHECKS

#ifndef APP_LOOKUP_METHOD
#define APP_LOOKUP_METHOD             APP_LOOKUP_LPM
#endif

#if (APP_LOOKUP_METHOD == APP_LOOKUP_EXACT_MATCH)
#include <rte_hash.h>
#elif (APP_LOOKUP_METHOD == APP_LOOKUP_LPM)
#include <rte_lpm.h>
#include <rte_lpm6.h>
#else
#error "APP_LOOKUP_METHOD set to incorrect value"
#endif

#define MAX_PKT_BURST 32

#include "ipv4_rsmbl.h"

#ifndef IPv6_BYTES
#define IPv6_BYTES_FMT "%02x%02x:%02x%02x:%02x%02x:%02x%02x:"\
                       "%02x%02x:%02x%02x:%02x%02x:%02x%02x"
#define IPv6_BYTES(addr) \
	addr[0],  addr[1], addr[2],  addr[3], \
	addr[4],  addr[5], addr[6],  addr[7], \
	addr[8],  addr[9], addr[10], addr[11],\
	addr[12], addr[13],addr[14], addr[15]
#endif


#define RTE_LOGTYPE_L3FWD RTE_LOGTYPE_USER1

#define MAX_PORTS	RTE_MAX_ETHPORTS

#define MAX_JUMBO_PKT_LEN  9600

#define IPV6_ADDR_LEN 16

#define MEMPOOL_CACHE_SIZE 256

#define	BUF_SIZE	2048
#define MBUF_SIZE	\
	(BUF_SIZE + sizeof(struct rte_mbuf) + RTE_PKTMBUF_HEADROOM)

#define	MAX_FLOW_NUM	UINT16_MAX
#define	MIN_FLOW_NUM	1
#define	DEF_FLOW_NUM	0x1000

/* TTL numbers are in ms. */
#define	MAX_FLOW_TTL	(3600 * MS_PER_S)
#define	MIN_FLOW_TTL	1
#define	DEF_FLOW_TTL	MS_PER_S

#define	DEF_MBUF_NUM	0x400

/* Should be power of two. */
#define	IPV4_FRAG_TBL_BUCKET_ENTRIES	2

static uint32_t max_flow_num = DEF_FLOW_NUM;
static uint32_t max_flow_ttl = DEF_FLOW_TTL;

/*
 * RX and TX Prefetch, Host, and Write-back threshold values should be
 * carefully set for optimal performance. Consult the network
 * controller's datasheet and supporting DPDK documentation for guidance
 * on how these parameters should be set.
 */
#define RX_PTHRESH 8 /**< Default values of RX prefetch threshold reg. */
#define RX_HTHRESH 8 /**< Default values of RX host threshold reg. */
#define RX_WTHRESH 4 /**< Default values of RX write-back threshold reg. */

/*
 * These default values are optimized for use with the Intel(R) 82599 10 GbE
 * Controller and the DPDK ixgbe PMD. Consider using other values for other
 * network controllers and/or network drivers.
 */
#define TX_PTHRESH 36 /**< Default values of TX prefetch threshold reg. */
#define TX_HTHRESH 0  /**< Default values of TX host threshold reg. */
#define TX_WTHRESH 0  /**< Default values of TX write-back threshold reg. */

#define BURST_TX_DRAIN_US 100 /* TX drain every ~100us */

#define NB_SOCKETS 8

/* Configure how many packets ahead to prefetch, when reading packets */
#define PREFETCH_OFFSET	3

/*
 * Configurable number of RX/TX ring descriptors
 */
#define RTE_TEST_RX_DESC_DEFAULT 128
#define RTE_TEST_TX_DESC_DEFAULT 512

static uint16_t nb_rxd = RTE_TEST_RX_DESC_DEFAULT;
static uint16_t nb_txd = RTE_TEST_TX_DESC_DEFAULT;

/* ethernet addresses of ports */
static struct ether_addr ports_eth_addr[MAX_PORTS];

/* mask of enabled ports */
static uint32_t enabled_port_mask = 0;
static int promiscuous_on = 0; /**< Ports set in promiscuous mode off by default. */
static int numa_on = 1; /**< NUMA is enabled by default. */

struct mbuf_table {
	uint32_t len;
	uint32_t head;
	uint32_t tail;
	struct rte_mbuf *m_table[0];
};

struct lcore_rx_queue {
	uint8_t port_id;
	uint8_t queue_id;
} __rte_cache_aligned;

#define MAX_RX_QUEUE_PER_LCORE 16
#define MAX_TX_QUEUE_PER_PORT MAX_PORTS
#define MAX_RX_QUEUE_PER_PORT 128

#define MAX_LCORE_PARAMS 1024
struct lcore_params {
	uint8_t port_id;
	uint8_t queue_id;
	uint8_t lcore_id;
} __rte_cache_aligned;

static struct lcore_params lcore_params_array[MAX_LCORE_PARAMS];
static struct lcore_params lcore_params_array_default[] = {
	{0, 0, 2},
	{0, 1, 2},
	{0, 2, 2},
	{1, 0, 2},
	{1, 1, 2},
	{1, 2, 2},
	{2, 0, 2},
	{3, 0, 3},
	{3, 1, 3},
};

static struct lcore_params * lcore_params = lcore_params_array_default;
static uint16_t nb_lcore_params = sizeof(lcore_params_array_default) /
				sizeof(lcore_params_array_default[0]);

static struct rte_eth_conf port_conf = {
	.rxmode = {
		.max_rx_pkt_len = ETHER_MAX_LEN,
		.split_hdr_size = 0,
		.header_split   = 0, /**< Header Split disabled */
		.hw_ip_checksum = 1, /**< IP checksum offload enabled */
		.hw_vlan_filter = 0, /**< VLAN filtering disabled */
		.jumbo_frame    = 0, /**< Jumbo Frame Support disabled */
		.hw_strip_crc   = 0, /**< CRC stripped by hardware */
	},
	.rx_adv_conf = {
		.rss_conf = {
			.rss_key = NULL,
			.rss_hf = ETH_RSS_IPV4 | ETH_RSS_IPV6,
		},
	},
	.txmode = {
		.mq_mode = ETH_MQ_TX_NONE,
	},
};

static const struct rte_eth_rxconf rx_conf = {
	.rx_thresh = {
		.pthresh = RX_PTHRESH,
		.hthresh = RX_HTHRESH,
		.wthresh = RX_WTHRESH,
	},
	.rx_free_thresh = 32,
};

static const struct rte_eth_txconf tx_conf = {
	.tx_thresh = {
		.pthresh = TX_PTHRESH,
		.hthresh = TX_HTHRESH,
		.wthresh = TX_WTHRESH,
	},
	.tx_free_thresh = 0, /* Use PMD default values */
	.tx_rs_thresh = 0, /* Use PMD default values */
	.txq_flags = 0x0,
};

#if (APP_LOOKUP_METHOD == APP_LOOKUP_EXACT_MATCH)

#ifdef RTE_MACHINE_CPUFLAG_SSE4_2
#include <rte_hash_crc.h>
#define DEFAULT_HASH_FUNC       rte_hash_crc
#else
#include <rte_jhash.h>
#define DEFAULT_HASH_FUNC       rte_jhash
#endif

struct ipv4_5tuple {
	uint32_t ip_dst;
	uint32_t ip_src;
	uint16_t port_dst;
	uint16_t port_src;
	uint8_t  proto;
} __attribute__((__packed__));

struct ipv6_5tuple {
	uint8_t  ip_dst[IPV6_ADDR_LEN];
	uint8_t  ip_src[IPV6_ADDR_LEN];
	uint16_t port_dst;
	uint16_t port_src;
	uint8_t  proto;
} __attribute__((__packed__));

struct ipv4_l3fwd_route {
	struct ipv4_5tuple key;
	uint8_t if_out;
};

struct ipv6_l3fwd_route {
	struct ipv6_5tuple key;
	uint8_t if_out;
};

static struct ipv4_l3fwd_route ipv4_l3fwd_route_array[] = {
	{{IPv4(100,10,0,1), IPv4(200,10,0,1), 101, 11, IPPROTO_TCP}, 0},
	{{IPv4(100,20,0,2), IPv4(200,20,0,2), 102, 12, IPPROTO_TCP}, 1},
	{{IPv4(100,30,0,3), IPv4(200,30,0,3), 103, 13, IPPROTO_TCP}, 2},
	{{IPv4(100,40,0,4), IPv4(200,40,0,4), 104, 14, IPPROTO_TCP}, 3},
};

static struct ipv6_l3fwd_route ipv6_l3fwd_route_array[] = {
	{
		{
			{0xfe, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			 0x02, 0x1b, 0x21, 0xff, 0xfe, 0x91, 0x38, 0x05},
			{0xfe, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			 0x02, 0x1e, 0x67, 0xff, 0xfe, 0x0d, 0xb6, 0x0a},
			 1, 10, IPPROTO_UDP
		}, 4
	},
};

typedef struct rte_hash lookup_struct_t;
static lookup_struct_t *ipv4_l3fwd_lookup_struct[NB_SOCKETS];
static lookup_struct_t *ipv6_l3fwd_lookup_struct[NB_SOCKETS];

#define L3FWD_HASH_ENTRIES	1024

#define IPV4_L3FWD_NUM_ROUTES \
	(sizeof(ipv4_l3fwd_route_array) / sizeof(ipv4_l3fwd_route_array[0]))

#define IPV6_L3FWD_NUM_ROUTES \
	(sizeof(ipv6_l3fwd_route_array) / sizeof(ipv6_l3fwd_route_array[0]))

static uint8_t ipv4_l3fwd_out_if[L3FWD_HASH_ENTRIES] __rte_cache_aligned;
static uint8_t ipv6_l3fwd_out_if[L3FWD_HASH_ENTRIES] __rte_cache_aligned;
#endif

#if (APP_LOOKUP_METHOD == APP_LOOKUP_LPM)
struct ipv4_l3fwd_route {
	uint32_t ip;
	uint8_t  depth;
	uint8_t  if_out;
};

struct ipv6_l3fwd_route {
	uint8_t ip[16];
	uint8_t  depth;
	uint8_t  if_out;
};

static struct ipv4_l3fwd_route ipv4_l3fwd_route_array[] = {
	{IPv4(1,1,1,0), 24, 0},
	{IPv4(2,1,1,0), 24, 1},
	{IPv4(3,1,1,0), 24, 2},
	{IPv4(4,1,1,0), 24, 3},
	{IPv4(5,1,1,0), 24, 4},
	{IPv4(6,1,1,0), 24, 5},
	{IPv4(7,1,1,0), 24, 6},
	{IPv4(8,1,1,0), 24, 7},
};

static struct ipv6_l3fwd_route ipv6_l3fwd_route_array[] = {
	{{1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}, 48, 0},
	{{2,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}, 48, 1},
	{{3,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}, 48, 2},
	{{4,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}, 48, 3},
	{{5,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}, 48, 4},
	{{6,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}, 48, 5},
	{{7,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}, 48, 6},
	{{8,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}, 48, 7},
};

#define IPV4_L3FWD_NUM_ROUTES \
	(sizeof(ipv4_l3fwd_route_array) / sizeof(ipv4_l3fwd_route_array[0]))
#define IPV6_L3FWD_NUM_ROUTES \
	(sizeof(ipv6_l3fwd_route_array) / sizeof(ipv6_l3fwd_route_array[0]))

#define IPV4_L3FWD_LPM_MAX_RULES         1024
#define IPV6_L3FWD_LPM_MAX_RULES         1024
#define IPV6_L3FWD_LPM_NUMBER_TBL8S (1 << 16)

typedef struct rte_lpm lookup_struct_t;
typedef struct rte_lpm6 lookup6_struct_t;
static lookup_struct_t *ipv4_l3fwd_lookup_struct[NB_SOCKETS];
static lookup6_struct_t *ipv6_l3fwd_lookup_struct[NB_SOCKETS];
#endif

struct tx_lcore_stat {
	uint64_t call;
	uint64_t drop;
	uint64_t queue;
	uint64_t send;
};

#ifdef IPV4_FRAG_TBL_STAT
#define	TX_LCORE_STAT_UPDATE(s, f, v)	((s)->f += (v))
#else
#define	TX_LCORE_STAT_UPDATE(s, f, v)	do {} while (0)
#endif /* IPV4_FRAG_TBL_STAT */

struct lcore_conf {
	uint16_t n_rx_queue;
	struct lcore_rx_queue rx_queue_list[MAX_RX_QUEUE_PER_LCORE];
	uint16_t tx_queue_id[MAX_PORTS];
	lookup_struct_t * ipv4_lookup_struct;
#if (APP_LOOKUP_METHOD == APP_LOOKUP_LPM)
	lookup6_struct_t * ipv6_lookup_struct;
#else
	lookup_struct_t * ipv6_lookup_struct;
#endif
	struct ipv4_frag_tbl *frag_tbl[MAX_RX_QUEUE_PER_LCORE];
	struct rte_mempool *pool[MAX_RX_QUEUE_PER_LCORE];
	struct ipv4_frag_death_row death_row;
	struct mbuf_table *tx_mbufs[MAX_PORTS];
	struct tx_lcore_stat tx_stat;
} __rte_cache_aligned;

static struct lcore_conf lcore_conf[RTE_MAX_LCORE];

/*
 * If number of queued packets reached given threahold, then
 * send burst of packets on an output interface.
 */
static inline uint32_t
send_burst(struct lcore_conf *qconf, uint32_t thresh, uint8_t port)
{
	uint32_t fill, len, k, n;
	struct mbuf_table *txmb;

	txmb = qconf->tx_mbufs[port];
	len = txmb->len;

	if ((int32_t)(fill = txmb->head - txmb->tail) < 0)
		fill += len;

	if (fill >= thresh) {
		n = RTE_MIN(len - txmb->tail, fill);
			
		k = rte_eth_tx_burst(port, qconf->tx_queue_id[port],
			txmb->m_table + txmb->tail, (uint16_t)n);

		TX_LCORE_STAT_UPDATE(&qconf->tx_stat, call, 1);
		TX_LCORE_STAT_UPDATE(&qconf->tx_stat, send, k);

		fill -= k;
		if ((txmb->tail += k) == len)
			txmb->tail = 0;
	}

	return (fill);
}

/* Enqueue a single packet, and send burst if queue is filled */
static inline int
send_single_packet(struct rte_mbuf *m, uint8_t port)
{
	uint32_t fill, lcore_id, len;
	struct lcore_conf *qconf;
	struct mbuf_table *txmb;

	lcore_id = rte_lcore_id();
	qconf = &lcore_conf[lcore_id];

	txmb = qconf->tx_mbufs[port];
	len = txmb->len;

	fill = send_burst(qconf, MAX_PKT_BURST, port);

	if (fill == len - 1) {
		TX_LCORE_STAT_UPDATE(&qconf->tx_stat, drop, 1);
		rte_pktmbuf_free(txmb->m_table[txmb->tail]);
		if (++txmb->tail == len)
			txmb->tail = 0;
	}
		
	TX_LCORE_STAT_UPDATE(&qconf->tx_stat, queue, 1);
	txmb->m_table[txmb->head] = m;
	if(++txmb->head == len)
		txmb->head = 0;

	return (0);
}

#ifdef DO_RFC_1812_CHECKS
static inline int
is_valid_ipv4_pkt(struct ipv4_hdr *pkt, uint32_t link_len)
{
	/* From http://www.rfc-editor.org/rfc/rfc1812.txt section 5.2.2 */
	/*
	 * 1. The packet length reported by the Link Layer must be large
	 * enough to hold the minimum length legal IP datagram (20 bytes).
	 */
	if (link_len < sizeof(struct ipv4_hdr))
		return -1;

	/* 2. The IP checksum must be correct. */
	/* this is checked in H/W */

	/*
	 * 3. The IP version number must be 4. If the version number is not 4
	 * then the packet may be another version of IP, such as IPng or
	 * ST-II.
	 */
	if (((pkt->version_ihl) >> 4) != 4)
		return -3;
	/*
	 * 4. The IP header length field must be large enough to hold the
	 * minimum length legal IP datagram (20 bytes = 5 words).
	 */
	if ((pkt->version_ihl & 0xf) < 5)
		return -4;

	/*
	 * 5. The IP total length field must be large enough to hold the IP
	 * datagram header, whose length is specified in the IP header length
	 * field.
	 */
	if (rte_cpu_to_be_16(pkt->total_length) < sizeof(struct ipv4_hdr))
		return -5;

	return 0;
}
#endif

#if (APP_LOOKUP_METHOD == APP_LOOKUP_EXACT_MATCH)
static void
print_ipv4_key(struct ipv4_5tuple key)
{
	printf("IP dst = %08x, IP src = %08x, port dst = %d, port src = %d, proto = %d\n",
			(unsigned)key.ip_dst, (unsigned)key.ip_src, key.port_dst, key.port_src, key.proto);
}
static void
print_ipv6_key(struct ipv6_5tuple key)
{
	printf( "IP dst = " IPv6_BYTES_FMT ", IP src = " IPv6_BYTES_FMT ", "
	        "port dst = %d, port src = %d, proto = %d\n",
	        IPv6_BYTES(key.ip_dst), IPv6_BYTES(key.ip_src),
	        key.port_dst, key.port_src, key.proto);
}

static inline uint8_t
get_ipv4_dst_port(struct ipv4_hdr *ipv4_hdr,  uint8_t portid, lookup_struct_t * ipv4_l3fwd_lookup_struct)
{
	struct ipv4_5tuple key;
	struct tcp_hdr *tcp;
	struct udp_hdr *udp;
	int ret = 0;

	key.ip_dst = rte_be_to_cpu_32(ipv4_hdr->dst_addr);
	key.ip_src = rte_be_to_cpu_32(ipv4_hdr->src_addr);
	key.proto = ipv4_hdr->next_proto_id;

	switch (ipv4_hdr->next_proto_id) {
	case IPPROTO_TCP:
		tcp = (struct tcp_hdr *)((unsigned char *) ipv4_hdr +
					sizeof(struct ipv4_hdr));
		key.port_dst = rte_be_to_cpu_16(tcp->dst_port);
		key.port_src = rte_be_to_cpu_16(tcp->src_port);
		break;

	case IPPROTO_UDP:
		udp = (struct udp_hdr *)((unsigned char *) ipv4_hdr +
					sizeof(struct ipv4_hdr));
		key.port_dst = rte_be_to_cpu_16(udp->dst_port);
		key.port_src = rte_be_to_cpu_16(udp->src_port);
		break;

	default:
		key.port_dst = 0;
		key.port_src = 0;
		break;
	}

	/* Find destination port */
	ret = rte_hash_lookup(ipv4_l3fwd_lookup_struct, (const void *)&key);
	return (uint8_t)((ret < 0)? portid : ipv4_l3fwd_out_if[ret]);
}

static inline uint8_t
get_ipv6_dst_port(struct ipv6_hdr *ipv6_hdr,  uint8_t portid, lookup_struct_t * ipv6_l3fwd_lookup_struct)
{
	struct ipv6_5tuple key;
	struct tcp_hdr *tcp;
	struct udp_hdr *udp;
	int ret = 0;

	memcpy(key.ip_dst, ipv6_hdr->dst_addr, IPV6_ADDR_LEN);
	memcpy(key.ip_src, ipv6_hdr->src_addr, IPV6_ADDR_LEN);

	key.proto = ipv6_hdr->proto;

	switch (ipv6_hdr->proto) {
	case IPPROTO_TCP:
		tcp = (struct tcp_hdr *)((unsigned char *) ipv6_hdr +
					sizeof(struct ipv6_hdr));
		key.port_dst = rte_be_to_cpu_16(tcp->dst_port);
		key.port_src = rte_be_to_cpu_16(tcp->src_port);
		break;

	case IPPROTO_UDP:
		udp = (struct udp_hdr *)((unsigned char *) ipv6_hdr +
					sizeof(struct ipv6_hdr));
		key.port_dst = rte_be_to_cpu_16(udp->dst_port);
		key.port_src = rte_be_to_cpu_16(udp->src_port);
		break;

	default:
		key.port_dst = 0;
		key.port_src = 0;
		break;
	}

	/* Find destination port */
	ret = rte_hash_lookup(ipv6_l3fwd_lookup_struct, (const void *)&key);
	return (uint8_t)((ret < 0)? portid : ipv6_l3fwd_out_if[ret]);
}
#endif

#if (APP_LOOKUP_METHOD == APP_LOOKUP_LPM)
static inline uint8_t
get_ipv4_dst_port(struct ipv4_hdr *ipv4_hdr,  uint8_t portid, lookup_struct_t * ipv4_l3fwd_lookup_struct)
{
	uint8_t next_hop;

	return (uint8_t) ((rte_lpm_lookup(ipv4_l3fwd_lookup_struct,
			rte_be_to_cpu_32(ipv4_hdr->dst_addr), &next_hop) == 0)?
			next_hop : portid);
}

static inline uint8_t
get_ipv6_dst_port(struct ipv6_hdr *ipv6_hdr,  uint8_t portid, lookup6_struct_t * ipv6_l3fwd_lookup_struct)
{
	uint8_t next_hop;

	return (uint8_t) ((rte_lpm6_lookup(ipv6_l3fwd_lookup_struct,
			ipv6_hdr->dst_addr, &next_hop) == 0)?
			next_hop : portid);
}
#endif

static inline void
l3fwd_simple_forward(struct rte_mbuf *m, uint8_t portid, uint32_t queue,
	struct lcore_conf *qconf, uint64_t tms)
{
	struct ether_hdr *eth_hdr;
	struct ipv4_hdr *ipv4_hdr;
	void *d_addr_bytes;
	uint8_t dst_port;
	uint16_t flag_offset, ip_flag, ip_ofs;

	eth_hdr = rte_pktmbuf_mtod(m, struct ether_hdr *);

	if (m->ol_flags & PKT_RX_IPV4_HDR) {
		/* Handle IPv4 headers.*/
		ipv4_hdr = (struct ipv4_hdr *)(eth_hdr + 1);

#ifdef DO_RFC_1812_CHECKS
		/* Check to make sure the packet is valid (RFC1812) */
		if (is_valid_ipv4_pkt(ipv4_hdr, m->pkt.pkt_len) < 0) {
			rte_pktmbuf_free(m);
			return;
		}

		/* Update time to live and header checksum */
		--(ipv4_hdr->time_to_live);
		++(ipv4_hdr->hdr_checksum);
#endif

		flag_offset = rte_be_to_cpu_16(ipv4_hdr->fragment_offset);
		ip_ofs = (uint16_t)(flag_offset & IPV4_HDR_OFFSET_MASK);
		ip_flag = (uint16_t)(flag_offset & IPV4_HDR_MF_FLAG);

		 /* if it is a fragmented packet, then try to reassemble. */
		if (ip_flag != 0 || ip_ofs  != 0) {

			struct rte_mbuf *mo;
			struct ipv4_frag_tbl *tbl;
			struct ipv4_frag_death_row *dr;

			tbl = qconf->frag_tbl[queue];
			dr = &qconf->death_row;

			/* prepare mbuf: setup l2_len/l3_len. */
			m->pkt.vlan_macip.f.l2_len = sizeof(*eth_hdr);
			m->pkt.vlan_macip.f.l3_len = sizeof(*ipv4_hdr);

			/* process this fragment. */
			if ((mo = ipv4_frag_mbuf(tbl, dr, m, tms, ipv4_hdr,
					ip_ofs, ip_flag)) == NULL) 
				/* no packet to send out. */
				return;

			/* we have our packet reassembled. */
			if (mo != m) {
				m = mo;
				eth_hdr = rte_pktmbuf_mtod(m,
					struct ether_hdr *);
				ipv4_hdr = (struct ipv4_hdr *)(eth_hdr + 1);
			}
		}

		dst_port = get_ipv4_dst_port(ipv4_hdr, portid,
			qconf->ipv4_lookup_struct);
		if (dst_port >= MAX_PORTS ||
				(enabled_port_mask & 1 << dst_port) == 0)
			dst_port = portid;

		/* 02:00:00:00:00:xx */
		d_addr_bytes = &eth_hdr->d_addr.addr_bytes[0];
		*((uint64_t *)d_addr_bytes) = 0x000000000002 + ((uint64_t)dst_port << 40);

		/* src addr */
		ether_addr_copy(&ports_eth_addr[dst_port], &eth_hdr->s_addr);

		send_single_packet(m, dst_port);
	}
	else {
		/* Handle IPv6 headers.*/
		struct ipv6_hdr *ipv6_hdr;

		ipv6_hdr = (struct ipv6_hdr *)(rte_pktmbuf_mtod(m, unsigned char *) +
				sizeof(struct ether_hdr));

		dst_port = get_ipv6_dst_port(ipv6_hdr, portid, qconf->ipv6_lookup_struct);

		if (dst_port >= MAX_PORTS || (enabled_port_mask & 1 << dst_port) == 0)
			dst_port = portid;

		/* 02:00:00:00:00:xx */
		d_addr_bytes = &eth_hdr->d_addr.addr_bytes[0];
		*((uint64_t *)d_addr_bytes) = 0x000000000002 + ((uint64_t)dst_port << 40);

		/* src addr */
		ether_addr_copy(&ports_eth_addr[dst_port], &eth_hdr->s_addr);

		send_single_packet(m, dst_port);
	}

}

/* main processing loop */
static int
main_loop(__attribute__((unused)) void *dummy)
{
	struct rte_mbuf *pkts_burst[MAX_PKT_BURST];
	unsigned lcore_id;
	uint64_t diff_tsc, cur_tsc, prev_tsc;
	int i, j, nb_rx;
	uint8_t portid, queueid;
	struct lcore_conf *qconf;
	const uint64_t drain_tsc = (rte_get_tsc_hz() + US_PER_S - 1) / US_PER_S * BURST_TX_DRAIN_US;

	prev_tsc = 0;

	lcore_id = rte_lcore_id();
	qconf = &lcore_conf[lcore_id];

	if (qconf->n_rx_queue == 0) {
		RTE_LOG(INFO, L3FWD, "lcore %u has nothing to do\n", lcore_id);
		return 0;
	}

	RTE_LOG(INFO, L3FWD, "entering main loop on lcore %u\n", lcore_id);

	for (i = 0; i < qconf->n_rx_queue; i++) {

		portid = qconf->rx_queue_list[i].port_id;
		queueid = qconf->rx_queue_list[i].queue_id;
		RTE_LOG(INFO, L3FWD, " -- lcoreid=%u portid=%hhu rxqueueid=%hhu\n", lcore_id,
			portid, queueid);
	}

	while (1) {

		cur_tsc = rte_rdtsc();

		/*
		 * TX burst queue drain
		 */
		diff_tsc = cur_tsc - prev_tsc;
		if (unlikely(diff_tsc > drain_tsc)) {

			/*
			 * This could be optimized (use queueid instead of
			 * portid), but it is not called so often
			 */
			for (portid = 0; portid < MAX_PORTS; portid++) {
				if ((enabled_port_mask & (1 << portid)) != 0)
					send_burst(qconf, 1, portid);
			}

			prev_tsc = cur_tsc;
		}

		/*
		 * Read packet from RX queues
		 */
		for (i = 0; i < qconf->n_rx_queue; ++i) {

			portid = qconf->rx_queue_list[i].port_id;
			queueid = qconf->rx_queue_list[i].queue_id;

			nb_rx = rte_eth_rx_burst(portid, queueid, pkts_burst,
				MAX_PKT_BURST);

			/* Prefetch first packets */
			for (j = 0; j < PREFETCH_OFFSET && j < nb_rx; j++) {
				rte_prefetch0(rte_pktmbuf_mtod(
						pkts_burst[j], void *));
			}

			/* Prefetch and forward already prefetched packets */
			for (j = 0; j < (nb_rx - PREFETCH_OFFSET); j++) {
				rte_prefetch0(rte_pktmbuf_mtod(pkts_burst[
					j + PREFETCH_OFFSET], void *));
				l3fwd_simple_forward(pkts_burst[j], portid,
					i, qconf, cur_tsc);
			}

			/* Forward remaining prefetched packets */
			for (; j < nb_rx; j++) {
				l3fwd_simple_forward(pkts_burst[j], portid,
					i, qconf, cur_tsc);
			}

			ipv4_frag_free_death_row(&qconf->death_row,
				PREFETCH_OFFSET);
		}
	}
}

static int
check_lcore_params(void)
{
	uint8_t queue, lcore;
	uint16_t i;
	int socketid;

	for (i = 0; i < nb_lcore_params; ++i) {
		queue = lcore_params[i].queue_id;
		if (queue >= MAX_RX_QUEUE_PER_PORT) {
			printf("invalid queue number: %hhu\n", queue);
			return -1;
		}
		lcore = lcore_params[i].lcore_id;
		if (!rte_lcore_is_enabled(lcore)) {
			printf("error: lcore %hhu is not enabled in lcore mask\n", lcore);
			return -1;
		}
		if ((socketid = rte_lcore_to_socket_id(lcore) != 0) &&
			(numa_on == 0)) {
			printf("warning: lcore %hhu is on socket %d with numa off \n",
				lcore, socketid);
		}
	}
	return 0;
}

static int
check_port_config(const unsigned nb_ports)
{
	unsigned portid;
	uint16_t i;

	for (i = 0; i < nb_lcore_params; ++i) {
		portid = lcore_params[i].port_id;
		if ((enabled_port_mask & (1 << portid)) == 0) {
			printf("port %u is not enabled in port mask\n", portid);
			return -1;
		}
		if (portid >= nb_ports) {
			printf("port %u is not present on the board\n", portid);
			return -1;
		}
	}
	return 0;
}

static uint8_t
get_port_n_rx_queues(const uint8_t port)
{
	int queue = -1;
	uint16_t i;

	for (i = 0; i < nb_lcore_params; ++i) {
		if (lcore_params[i].port_id == port && lcore_params[i].queue_id > queue)
			queue = lcore_params[i].queue_id;
	}
	return (uint8_t)(++queue);
}

static int
init_lcore_rx_queues(void)
{
	uint16_t i, nb_rx_queue;
	uint8_t lcore;

	for (i = 0; i < nb_lcore_params; ++i) {
		lcore = lcore_params[i].lcore_id;
		nb_rx_queue = lcore_conf[lcore].n_rx_queue;
		if (nb_rx_queue >= MAX_RX_QUEUE_PER_LCORE) {
			printf("error: too many queues (%u) for lcore: %u\n",
				(unsigned)nb_rx_queue + 1, (unsigned)lcore);
			return -1;
		} else {
			lcore_conf[lcore].rx_queue_list[nb_rx_queue].port_id =
				lcore_params[i].port_id;
			lcore_conf[lcore].rx_queue_list[nb_rx_queue].queue_id =
				lcore_params[i].queue_id;
			lcore_conf[lcore].n_rx_queue++;
		}
	}
	return 0;
}

/* display usage */
static void
print_usage(const char *prgname)
{
	printf ("%s [EAL options] -- -p PORTMASK -P"
		"  [--config (port,queue,lcore)[,(port,queue,lcore]]"
		"  [--enable-jumbo [--max-pkt-len PKTLEN]]"
		"  [--maxflows=<flows>]  [--flowttl=<ttl>[(s|ms)]]\n"
		"  -p PORTMASK: hexadecimal bitmask of ports to configure\n"
		"  -P : enable promiscuous mode\n"
		"  --config (port,queue,lcore): rx queues configuration\n"
		"  --no-numa: optional, disable numa awareness\n"
		"  --enable-jumbo: enable jumbo frame"
		" which max packet len is PKTLEN in decimal (64-9600)\n"
		"  --maxflows=<flows>: optional, maximum number of flows "
		"supported\n"
		"  --flowttl=<ttl>[(s|ms)]: optional, maximum TTL for each "
		"flow\n",
		prgname);
}

static uint32_t
parse_flow_num(const char *str, uint32_t min, uint32_t max, uint32_t *val)
{
	char *end;
	uint64_t v;

	/* parse decimal string */
	errno = 0;
	v = strtoul(str, &end, 10);
	if (errno != 0 || *end != '\0')
		return (-EINVAL);

	if (v < min || v > max)
		return (-EINVAL);

	*val = (uint32_t)v;
	return (0);
}

static int
parse_flow_ttl(const char *str, uint32_t min, uint32_t max, uint32_t *val)
{
	char *end;
	uint64_t v;

	static const char frmt_sec[] = "s"; 
	static const char frmt_msec[] = "ms"; 

	/* parse decimal string */
	errno = 0;
	v = strtoul(str, &end, 10);
	if (errno != 0)
		return (-EINVAL);

	if (*end != '\0') {
		if (strncmp(frmt_sec, end, sizeof(frmt_sec)) == 0)
			v *= MS_PER_S;
		else if (strncmp(frmt_msec, end, sizeof (frmt_msec)) != 0)
			return (-EINVAL);
	}

	if (v < min || v > max)
		return (-EINVAL);

	*val = (uint32_t)v;
	return (0);
}


static int parse_max_pkt_len(const char *pktlen)
{
	char *end = NULL;
	unsigned long len;

	/* parse decimal string */
	len = strtoul(pktlen, &end, 10);
	if ((pktlen[0] == '\0') || (end == NULL) || (*end != '\0'))
		return -1;

	if (len == 0)
		return -1;

	return len;
}

static int
parse_portmask(const char *portmask)
{
	char *end = NULL;
	unsigned long pm;

	/* parse hexadecimal string */
	pm = strtoul(portmask, &end, 16);
	if ((portmask[0] == '\0') || (end == NULL) || (*end != '\0'))
		return -1;

	if (pm == 0)
		return -1;

	return pm;
}

static int
parse_config(const char *q_arg)
{
	char s[256];
	const char *p, *p0 = q_arg;
	char *end;
	enum fieldnames {
		FLD_PORT = 0,
		FLD_QUEUE,
		FLD_LCORE,
		_NUM_FLD
	};
	unsigned long int_fld[_NUM_FLD];
	char *str_fld[_NUM_FLD];
	int i;
	unsigned size;

	nb_lcore_params = 0;

	while ((p = strchr(p0,'(')) != NULL) {
		++p;
		if((p0 = strchr(p,')')) == NULL)
			return -1;

		size = p0 - p;
		if(size >= sizeof(s))
			return -1;

		rte_snprintf(s, sizeof(s), "%.*s", size, p);
		if (rte_strsplit(s, sizeof(s), str_fld, _NUM_FLD, ',') != _NUM_FLD)
			return -1;
		for (i = 0; i < _NUM_FLD; i++){
			errno = 0;
			int_fld[i] = strtoul(str_fld[i], &end, 0);
			if (errno != 0 || end == str_fld[i] || int_fld[i] > 255)
				return -1;
		}
		if (nb_lcore_params >= MAX_LCORE_PARAMS) {
			printf("exceeded max number of lcore params: %hu\n",
				nb_lcore_params);
			return -1;
		}
		lcore_params_array[nb_lcore_params].port_id = (uint8_t)int_fld[FLD_PORT];
		lcore_params_array[nb_lcore_params].queue_id = (uint8_t)int_fld[FLD_QUEUE];
		lcore_params_array[nb_lcore_params].lcore_id = (uint8_t)int_fld[FLD_LCORE];
		++nb_lcore_params;
	}
	lcore_params = lcore_params_array;
	return 0;
}

/* Parse the argument given in the command line of the application */
static int
parse_args(int argc, char **argv)
{
	int opt, ret;
	char **argvopt;
	int option_index;
	char *prgname = argv[0];
	static struct option lgopts[] = {
		{"config", 1, 0, 0},
		{"no-numa", 0, 0, 0},
		{"enable-jumbo", 0, 0, 0},
		{"maxflows", 1, 0, 0},
		{"flowttl", 1, 0, 0},
		{NULL, 0, 0, 0}
	};

	argvopt = argv;

	while ((opt = getopt_long(argc, argvopt, "p:P",
				lgopts, &option_index)) != EOF) {

		switch (opt) {
		/* portmask */
		case 'p':
			enabled_port_mask = parse_portmask(optarg);
			if (enabled_port_mask == 0) {
				printf("invalid portmask\n");
				print_usage(prgname);
				return -1;
			}
			break;
		case 'P':
			printf("Promiscuous mode selected\n");
			promiscuous_on = 1;
			break;

		/* long options */
		case 0:
			if (!strncmp(lgopts[option_index].name, "config", 6)) {
				ret = parse_config(optarg);
				if (ret) {
					printf("invalid config\n");
					print_usage(prgname);
					return -1;
				}
			}

			if (!strncmp(lgopts[option_index].name, "no-numa", 7)) {
				printf("numa is disabled \n");
				numa_on = 0;
			}
			
			if (!strncmp(lgopts[option_index].name,
					"maxflows", 8)) {
				if ((ret = parse_flow_num(optarg, MIN_FLOW_NUM,
						MAX_FLOW_NUM,
						&max_flow_num)) != 0) {
					printf("invalid value: \"%s\" for "
						"parameter %s\n",
						optarg,
						lgopts[option_index].name);
					print_usage(prgname);
					return (ret);
				}
			}
			
			if (!strncmp(lgopts[option_index].name, "flowttl", 7)) {
				if ((ret = parse_flow_ttl(optarg, MIN_FLOW_TTL,
						MAX_FLOW_TTL,
						&max_flow_ttl)) != 0) {
					printf("invalid value: \"%s\" for "
						"parameter %s\n",
						optarg,
						lgopts[option_index].name);
					print_usage(prgname);
					return (ret);
				}
			}

			if (!strncmp(lgopts[option_index].name, "enable-jumbo", 12)) {
				struct option lenopts = {"max-pkt-len", required_argument, 0, 0};

				printf("jumbo frame is enabled \n");
				port_conf.rxmode.jumbo_frame = 1;
	
				/* if no max-pkt-len set, use the default value ETHER_MAX_LEN */	
				if (0 == getopt_long(argc, argvopt, "", &lenopts, &option_index)) {
					ret = parse_max_pkt_len(optarg);
					if ((ret < 64) || (ret > MAX_JUMBO_PKT_LEN)){
						printf("invalid packet length\n");
						print_usage(prgname);
						return -1;
					}
					port_conf.rxmode.max_rx_pkt_len = ret;
				}
				printf("set jumbo frame max packet length to %u\n", 
						(unsigned int)port_conf.rxmode.max_rx_pkt_len);
			}
			
			break;

		default:
			print_usage(prgname);
			return -1;
		}
	}

	if (optind >= 0)
		argv[optind-1] = prgname;

	ret = optind-1;
	optind = 0; /* reset getopt lib */
	return ret;
}

static void
print_ethaddr(const char *name, const struct ether_addr *eth_addr)
{
	printf ("%s%02X:%02X:%02X:%02X:%02X:%02X", name,
		eth_addr->addr_bytes[0],
		eth_addr->addr_bytes[1],
		eth_addr->addr_bytes[2],
		eth_addr->addr_bytes[3],
		eth_addr->addr_bytes[4],
		eth_addr->addr_bytes[5]);
}

#if (APP_LOOKUP_METHOD == APP_LOOKUP_EXACT_MATCH)
static void
setup_hash(int socketid)
{
	struct rte_hash_parameters ipv4_l3fwd_hash_params = {
		.name = NULL,
		.entries = L3FWD_HASH_ENTRIES,
		.bucket_entries = 4,
		.key_len = sizeof(struct ipv4_5tuple),
		.hash_func = DEFAULT_HASH_FUNC,
		.hash_func_init_val = 0,
	};

	struct rte_hash_parameters ipv6_l3fwd_hash_params = {
		.name = NULL,
		.entries = L3FWD_HASH_ENTRIES,
		.bucket_entries = 4,
		.key_len = sizeof(struct ipv6_5tuple),
		.hash_func = DEFAULT_HASH_FUNC,
		.hash_func_init_val = 0,
	};

	unsigned i;
	int ret;
	char s[64];

	/* create ipv4 hash */
	rte_snprintf(s, sizeof(s), "ipv4_l3fwd_hash_%d", socketid);
	ipv4_l3fwd_hash_params.name = s;
	ipv4_l3fwd_hash_params.socket_id = socketid;
	ipv4_l3fwd_lookup_struct[socketid] = rte_hash_create(&ipv4_l3fwd_hash_params);
	if (ipv4_l3fwd_lookup_struct[socketid] == NULL)
		rte_exit(EXIT_FAILURE, "Unable to create the l3fwd hash on "
				"socket %d\n", socketid);

	/* create ipv6 hash */
	rte_snprintf(s, sizeof(s), "ipv6_l3fwd_hash_%d", socketid);
	ipv6_l3fwd_hash_params.name = s;
	ipv6_l3fwd_hash_params.socket_id = socketid;
	ipv6_l3fwd_lookup_struct[socketid] = rte_hash_create(&ipv6_l3fwd_hash_params);
	if (ipv6_l3fwd_lookup_struct[socketid] == NULL)
		rte_exit(EXIT_FAILURE, "Unable to create the l3fwd hash on "
				"socket %d\n", socketid);


	/* populate the ipv4 hash */
	for (i = 0; i < IPV4_L3FWD_NUM_ROUTES; i++) {
		ret = rte_hash_add_key (ipv4_l3fwd_lookup_struct[socketid],
				(void *) &ipv4_l3fwd_route_array[i].key);
		if (ret < 0) {
			rte_exit(EXIT_FAILURE, "Unable to add entry %u to the"
				"l3fwd hash on socket %d\n", i, socketid);
		}
		ipv4_l3fwd_out_if[ret] = ipv4_l3fwd_route_array[i].if_out;
		printf("Hash: Adding key\n");
		print_ipv4_key(ipv4_l3fwd_route_array[i].key);
	}

	/* populate the ipv6 hash */
	for (i = 0; i < IPV6_L3FWD_NUM_ROUTES; i++) {
		ret = rte_hash_add_key (ipv6_l3fwd_lookup_struct[socketid],
				(void *) &ipv6_l3fwd_route_array[i].key);
		if (ret < 0) {
			rte_exit(EXIT_FAILURE, "Unable to add entry %u to the"
				"l3fwd hash on socket %d\n", i, socketid);
		}
		ipv6_l3fwd_out_if[ret] = ipv6_l3fwd_route_array[i].if_out;
		printf("Hash: Adding key\n");
		print_ipv6_key(ipv6_l3fwd_route_array[i].key);
	}
}
#endif

#if (APP_LOOKUP_METHOD == APP_LOOKUP_LPM)
static void
setup_lpm(int socketid)
{
	struct rte_lpm6_config config;
	unsigned i;
	int ret;
	char s[64];

	/* create the LPM table */
	rte_snprintf(s, sizeof(s), "IPV4_L3FWD_LPM_%d", socketid);
	ipv4_l3fwd_lookup_struct[socketid] = rte_lpm_create(s, socketid,
				IPV4_L3FWD_LPM_MAX_RULES, 0);
	if (ipv4_l3fwd_lookup_struct[socketid] == NULL)
		rte_exit(EXIT_FAILURE, "Unable to create the l3fwd LPM table"
				" on socket %d\n", socketid);

	/* populate the LPM table */
	for (i = 0; i < IPV4_L3FWD_NUM_ROUTES; i++) {
		ret = rte_lpm_add(ipv4_l3fwd_lookup_struct[socketid],
			ipv4_l3fwd_route_array[i].ip,
			ipv4_l3fwd_route_array[i].depth,
			ipv4_l3fwd_route_array[i].if_out);

		if (ret < 0) {
			rte_exit(EXIT_FAILURE, "Unable to add entry %u to the "
				"l3fwd LPM table on socket %d\n",
				i, socketid);
		}

		printf("LPM: Adding route 0x%08x / %d (%d)\n",
			(unsigned)ipv4_l3fwd_route_array[i].ip,
			ipv4_l3fwd_route_array[i].depth,
			ipv4_l3fwd_route_array[i].if_out);
	}
	
	/* create the LPM6 table */
	rte_snprintf(s, sizeof(s), "IPV6_L3FWD_LPM_%d", socketid);
	
	config.max_rules = IPV6_L3FWD_LPM_MAX_RULES;
	config.number_tbl8s = IPV6_L3FWD_LPM_NUMBER_TBL8S;
	config.flags = 0;
	ipv6_l3fwd_lookup_struct[socketid] = rte_lpm6_create(s, socketid,
				&config);
	if (ipv6_l3fwd_lookup_struct[socketid] == NULL)
		rte_exit(EXIT_FAILURE, "Unable to create the l3fwd LPM table"
				" on socket %d\n", socketid);

	/* populate the LPM table */
	for (i = 0; i < IPV6_L3FWD_NUM_ROUTES; i++) {
		ret = rte_lpm6_add(ipv6_l3fwd_lookup_struct[socketid],
			ipv6_l3fwd_route_array[i].ip,
			ipv6_l3fwd_route_array[i].depth,
			ipv6_l3fwd_route_array[i].if_out);

		if (ret < 0) {
			rte_exit(EXIT_FAILURE, "Unable to add entry %u to the "
				"l3fwd LPM table on socket %d\n",
				i, socketid);
		}

		printf("LPM: Adding route %s / %d (%d)\n",
			"IPV6",
			ipv6_l3fwd_route_array[i].depth,
			ipv6_l3fwd_route_array[i].if_out);
	}
}
#endif

static int
init_mem(void)
{
	struct lcore_conf *qconf;
	int socketid;
	unsigned lcore_id;

	for (lcore_id = 0; lcore_id < RTE_MAX_LCORE; lcore_id++) {
		if (rte_lcore_is_enabled(lcore_id) == 0)
			continue;

		if (numa_on)
			socketid = rte_lcore_to_socket_id(lcore_id);
		else
			socketid = 0;

		if (socketid >= NB_SOCKETS) {
			rte_exit(EXIT_FAILURE,
				"Socket %d of lcore %u is out of range %d\n",
				socketid, lcore_id, NB_SOCKETS);
		}

#if (APP_LOOKUP_METHOD == APP_LOOKUP_LPM)
			setup_lpm(socketid);
#else
			setup_hash(socketid);
#endif
		qconf = &lcore_conf[lcore_id];
		qconf->ipv4_lookup_struct = ipv4_l3fwd_lookup_struct[socketid];
		qconf->ipv6_lookup_struct = ipv6_l3fwd_lookup_struct[socketid];
	}
	return 0;
}

/* Check the link status of all ports in up to 9s, and print them finally */
static void
check_all_ports_link_status(uint8_t port_num, uint32_t port_mask)
{
#define CHECK_INTERVAL 100 /* 100ms */
#define MAX_CHECK_TIME 90 /* 9s (90 * 100ms) in total */
	uint8_t portid, count, all_ports_up, print_flag = 0;
	struct rte_eth_link link;

	printf("\nChecking link status");
	fflush(stdout);
	for (count = 0; count <= MAX_CHECK_TIME; count++) {
		all_ports_up = 1;
		for (portid = 0; portid < port_num; portid++) {
			if ((port_mask & (1 << portid)) == 0)
				continue;
			memset(&link, 0, sizeof(link));
			rte_eth_link_get_nowait(portid, &link);
			/* print link status if flag set */
			if (print_flag == 1) {
				if (link.link_status)
					printf("Port %d Link Up - speed %u "
						"Mbps - %s\n", (uint8_t)portid,
						(unsigned)link.link_speed,
				(link.link_duplex == ETH_LINK_FULL_DUPLEX) ?
					("full-duplex") : ("half-duplex\n"));
				else
					printf("Port %d Link Down\n",
						(uint8_t)portid);
				continue;
			}
			/* clear all_ports_up flag if any link down */
			if (link.link_status == 0) {
				all_ports_up = 0;
				break;
			}
		}
		/* after finally printing all link status, get out */
		if (print_flag == 1)
			break;

		if (all_ports_up == 0) {
			printf(".");
			fflush(stdout);
			rte_delay_ms(CHECK_INTERVAL);
		}

		/* set the print_flag if all ports up or timeout */
		if (all_ports_up == 1 || count == (MAX_CHECK_TIME - 1)) {
			print_flag = 1;
			printf("done\n");
		}
	}
}
static void
setup_port_tbl(struct lcore_conf *qconf, uint32_t lcore, int socket,
	uint32_t port)
{
	struct mbuf_table *mtb;
	uint32_t n;
	size_t sz;

	n = RTE_MAX(max_flow_num, 2UL * MAX_PKT_BURST);
	sz = sizeof (*mtb) + sizeof (mtb->m_table[0]) *  n;

	if ((mtb = rte_zmalloc_socket(__func__, sz, CACHE_LINE_SIZE,
			socket)) == NULL)
		rte_exit(EXIT_FAILURE, "%s() for lcore: %u, port: %u "
			"failed to allocate %zu bytes\n",
			__func__, lcore, port, sz);

	mtb->len = n;
	qconf->tx_mbufs[port] = mtb;
}

static void
setup_queue_tbl(struct lcore_conf *qconf, uint32_t lcore, int socket,
	uint32_t queue)
{
	uint32_t nb_mbuf;
	uint64_t frag_cycles;
	char buf[RTE_MEMPOOL_NAMESIZE];

	frag_cycles = (rte_get_tsc_hz() + MS_PER_S - 1) / MS_PER_S *
		max_flow_ttl;

	if ((qconf->frag_tbl[queue] = ipv4_frag_tbl_create(max_flow_num,
			IPV4_FRAG_TBL_BUCKET_ENTRIES, max_flow_num, frag_cycles,
			socket)) == NULL)
		rte_exit(EXIT_FAILURE, "ipv4_frag_tbl_create(%u) on "
			"lcore: %u for queue: %u failed\n",
			max_flow_num, lcore, queue);

	/*
	 * At any given moment up to <max_flow_num * (MAX_FRAG_NUM - 1)>
	 * mbufs could be stored int the fragment table.
	 * Plus, each TX queue can hold up to <max_flow_num> packets.
	 */ 

	nb_mbuf = 2 * RTE_MAX(max_flow_num, 2UL * MAX_PKT_BURST) * MAX_FRAG_NUM;
	nb_mbuf *= (port_conf.rxmode.max_rx_pkt_len + BUF_SIZE - 1) / BUF_SIZE;
	nb_mbuf += RTE_TEST_RX_DESC_DEFAULT + RTE_TEST_TX_DESC_DEFAULT;

	nb_mbuf = RTE_MAX(nb_mbuf, (uint32_t)DEF_MBUF_NUM);
		
	rte_snprintf(buf, sizeof(buf), "mbuf_pool_%u_%u", lcore, queue);

	if ((qconf->pool[queue] = rte_mempool_create(buf, nb_mbuf, MBUF_SIZE, 0,
			sizeof(struct rte_pktmbuf_pool_private),
			rte_pktmbuf_pool_init, NULL, rte_pktmbuf_init, NULL,
			socket, MEMPOOL_F_SP_PUT | MEMPOOL_F_SC_GET)) == NULL)
		rte_exit(EXIT_FAILURE, "mempool_create(%s) failed", buf);
}

static void
queue_dump_stat(void)
{
	uint32_t i, lcore;
	const struct lcore_conf *qconf;

	for (lcore = 0; lcore < RTE_MAX_LCORE; lcore++) {
		if (rte_lcore_is_enabled(lcore) == 0)
			continue;

		qconf = lcore_conf + lcore;
		for (i = 0; i < qconf->n_rx_queue; i++) {

			fprintf(stdout, " -- lcoreid=%u portid=%hhu "
				"rxqueueid=%hhu frag tbl stat:\n",
				lcore,  qconf->rx_queue_list[i].port_id,
				qconf->rx_queue_list[i].queue_id);
			ipv4_frag_tbl_dump_stat(stdout, qconf->frag_tbl[i]);
			fprintf(stdout, "TX bursts:\t%" PRIu64 "\n"
				"TX packets _queued:\t%" PRIu64 "\n"
				"TX packets dropped:\t%" PRIu64 "\n"
				"TX packets send:\t%" PRIu64 "\n",
				qconf->tx_stat.call,
				qconf->tx_stat.queue,
				qconf->tx_stat.drop,
				qconf->tx_stat.send);
		}
	}
}

static void
signal_handler(int signum)
{
	queue_dump_stat();
	if (signum != SIGUSR1)
		rte_exit(0, "received signal: %d, exiting\n", signum);
}

int
MAIN(int argc, char **argv)
{
	struct lcore_conf *qconf;
	int ret;
	unsigned nb_ports;
	uint16_t queueid;
	unsigned lcore_id;
	uint32_t n_tx_queue, nb_lcores;
	uint8_t portid, nb_rx_queue, queue, socketid;

	/* init EAL */
	ret = rte_eal_init(argc, argv);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Invalid EAL parameters\n");
	argc -= ret;
	argv += ret;

	/* parse application arguments (after the EAL ones) */
	ret = parse_args(argc, argv);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Invalid L3FWD parameters\n");

	if (check_lcore_params() < 0)
		rte_exit(EXIT_FAILURE, "check_lcore_params failed\n");

	ret = init_lcore_rx_queues();
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "init_lcore_rx_queues failed\n");


	/* init driver(s) */
	if (rte_pmd_init_all() < 0)
		rte_exit(EXIT_FAILURE, "Cannot init pmd\n");

	if (rte_eal_pci_probe() < 0)
		rte_exit(EXIT_FAILURE, "Cannot probe PCI\n");

	nb_ports = rte_eth_dev_count();
	if (nb_ports > MAX_PORTS)
		nb_ports = MAX_PORTS;

	if (check_port_config(nb_ports) < 0)
		rte_exit(EXIT_FAILURE, "check_port_config failed\n");

	nb_lcores = rte_lcore_count();

	/* initialize all ports */
	for (portid = 0; portid < nb_ports; portid++) {
		/* skip ports that are not enabled */
		if ((enabled_port_mask & (1 << portid)) == 0) {
			printf("\nSkipping disabled port %d\n", portid);
			continue;
		}

		/* init port */
		printf("Initializing port %d ... ", portid );
		fflush(stdout);

		nb_rx_queue = get_port_n_rx_queues(portid);
		n_tx_queue = nb_lcores;
		if (n_tx_queue > MAX_TX_QUEUE_PER_PORT)
			n_tx_queue = MAX_TX_QUEUE_PER_PORT;
		printf("Creating queues: nb_rxq=%d nb_txq=%u... ",
			nb_rx_queue, (unsigned)n_tx_queue );
		ret = rte_eth_dev_configure(portid, nb_rx_queue,
					(uint16_t)n_tx_queue, &port_conf);
		if (ret < 0)
			rte_exit(EXIT_FAILURE, "Cannot configure device: err=%d, port=%d\n",
				ret, portid);

		rte_eth_macaddr_get(portid, &ports_eth_addr[portid]);
		print_ethaddr(" Address:", &ports_eth_addr[portid]);
		printf(", ");

		/* init memory */
		ret = init_mem();
		if (ret < 0)
			rte_exit(EXIT_FAILURE, "init_mem failed\n");

		/* init one TX queue per couple (lcore,port) */
		queueid = 0;
		for (lcore_id = 0; lcore_id < RTE_MAX_LCORE; lcore_id++) {
			if (rte_lcore_is_enabled(lcore_id) == 0)
				continue;

			if (numa_on)
				socketid = (uint8_t)rte_lcore_to_socket_id(lcore_id);
			else
				socketid = 0;

			printf("txq=%u,%d,%d ", lcore_id, queueid, socketid);
			fflush(stdout);
			ret = rte_eth_tx_queue_setup(portid, queueid, nb_txd,
						     socketid, &tx_conf);
			if (ret < 0)
				rte_exit(EXIT_FAILURE, "rte_eth_tx_queue_setup: err=%d, "
					"port=%d\n", ret, portid);

			qconf = &lcore_conf[lcore_id];
			qconf->tx_queue_id[portid] = queueid;
			setup_port_tbl(qconf, lcore_id, socketid, portid);
			queueid++;
		}
		printf("\n");
	}

	for (lcore_id = 0; lcore_id < RTE_MAX_LCORE; lcore_id++) {
		if (rte_lcore_is_enabled(lcore_id) == 0)
			continue;
		qconf = &lcore_conf[lcore_id];
		printf("\nInitializing rx queues on lcore %u ... ", lcore_id );
		fflush(stdout);
		/* init RX queues */
		for(queue = 0; queue < qconf->n_rx_queue; ++queue) {
			portid = qconf->rx_queue_list[queue].port_id;
			queueid = qconf->rx_queue_list[queue].queue_id;

			if (numa_on)
				socketid = (uint8_t)rte_lcore_to_socket_id(lcore_id);
			else
				socketid = 0;

			printf("rxq=%d,%d,%d ", portid, queueid, socketid);
			fflush(stdout);

			setup_queue_tbl(qconf, lcore_id, socketid, queue);

			ret = rte_eth_rx_queue_setup(portid, queueid, nb_rxd,
				        socketid, &rx_conf, qconf->pool[queue]);
			if (ret < 0)
				rte_exit(EXIT_FAILURE,
					"rte_eth_rx_queue_setup: err=%d,"
					"port=%d\n", ret, portid);
		}
	}

	printf("\n");

	/* start ports */
	for (portid = 0; portid < nb_ports; portid++) {
		if ((enabled_port_mask & (1 << portid)) == 0) {
			continue;
		}
		/* Start device */
		ret = rte_eth_dev_start(portid);
		if (ret < 0)
			rte_exit(EXIT_FAILURE, "rte_eth_dev_start: err=%d, port=%d\n",
				ret, portid);

		/*
		 * If enabled, put device in promiscuous mode.
		 * This allows IO forwarding mode to forward packets
		 * to itself through 2 cross-connected  ports of the
		 * target machine.
		 */
		if (promiscuous_on)
			rte_eth_promiscuous_enable(portid);
	}

	check_all_ports_link_status((uint8_t)nb_ports, enabled_port_mask);

	signal(SIGUSR1, signal_handler);
	signal(SIGTERM, signal_handler);
	signal(SIGINT, signal_handler);

	/* launch per-lcore init on every lcore */
	rte_eal_mp_remote_launch(main_loop, NULL, CALL_MASTER);
	RTE_LCORE_FOREACH_SLAVE(lcore_id) {
		if (rte_eal_wait_lcore(lcore_id) < 0)
			return -1;
	}

	return 0;
}
