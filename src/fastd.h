/*
  Copyright (c) 2012, Matthias Schiffer <mschiffer@universe-factory.net>
  Partly based on QuickTun Copyright (c) 2010, Ivo Smits <Ivo@UCIS.nl>.
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

    1. Redistributions of source code must retain the above copyright notice,
       this list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright notice,
       this list of conditions and the following disclaimer in the documentation
       and/or other materials provided with the distribution.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/


#ifndef _FASTD_FASTD_H_
#define _FASTD_FASTD_H_

#include "queue.h"

#include <errno.h>
#include <linux/if_ether.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/uio.h>
#include <sys/socket.h>


typedef enum _fastd_loglevel {
	LOG_FATAL = 0,
	LOG_ERROR,
	LOG_WARN,
	LOG_INFO,
	LOG_DEBUG,
} fastd_loglevel;

typedef enum _fastd_packet_type {
	PACKET_DATA = 0,
	PACKET_HANDSHAKE,
} fastd_packet_type;

typedef struct _fastd_buffer {
	void *base;
	size_t base_len;

	void *data;
	size_t len;
} fastd_buffer;

typedef enum _fastd_protocol {
	PROTOCOL_ETHERNET,
	PROTOCOL_IP,
} fastd_protocol;

typedef union _fastd_peer_address {
	struct sockaddr sa;
	struct sockaddr_in in;
	struct sockaddr_in6 in6;
} fastd_peer_address;

typedef struct _fastd_peer_config {
	struct _fastd_peer_config *next;

	fastd_peer_address address;
} fastd_peer_config;

typedef enum _fastd_peer_state {
	STATE_WAIT,
	STATE_ESTABLISHED,
	STATE_TEMP,
	STATE_TEMP_ESTABLISHED,
} fastd_peer_state;

typedef struct _fastd_eth_addr {
	uint8_t data[ETH_ALEN];
} fastd_eth_addr;

typedef struct _fastd_peer {
	struct _fastd_peer *next;

	const fastd_peer_config *config;

	fastd_peer_address address;

	fastd_peer_state state;
	uint8_t last_req_id;
} fastd_peer;

typedef struct _fastd_peer_eth_addr {
	fastd_eth_addr addr;
	fastd_peer *peer;
} fastd_peer_eth_addr;

typedef struct _fastd_config fastd_config;
typedef struct _fastd_context fastd_context;

typedef struct _fastd_method {
	const char *name;

	bool (*check_config)(fastd_context *ctx, const fastd_config *conf);

	size_t (*max_packet_size)(fastd_context *ctx);

	void (*init)(fastd_context *ctx, fastd_peer *peer);

	void (*handle_recv)(fastd_context *ctx, fastd_peer *peer, fastd_buffer buffer);
	void (*send)(fastd_context *ctx, fastd_peer *peer, fastd_buffer buffer);
} fastd_method;

struct _fastd_config {
	fastd_loglevel loglevel;

	char *ifname;

	struct sockaddr_in bind_addr_in;
	struct sockaddr_in6 bind_addr_in6;

	uint16_t mtu;
	fastd_protocol protocol;

	fastd_method *method;

	unsigned n_floating;
	fastd_peer_config *peers;
};

struct _fastd_context {
	const fastd_config *conf;

	fastd_peer *peers;
	fastd_queue task_queue;

	int tunfd;
	int sockfd;
	int sock6fd;

	size_t eth_addr_size;
	size_t n_eth_addr;
	fastd_peer_eth_addr *eth_addr;
};


#define pr_log(ctx, level, prefix, args...) if ((ctx)->conf == NULL || (level) <= (ctx)->conf->loglevel) \
		do { fputs(prefix, stderr); fprintf(stderr, args); fputs("\n", stderr); } while(0)

#define is_error(ctx) ((ctx)->conf == NULL || LOG_ERROR <= (ctx)->conf->loglevel)
#define is_warn(ctx) ((ctx)->conf == NULL || LOG_WARN <= (ctx)->conf->loglevel)
#define is_info(ctx) ((ctx)->conf == NULL || LOG_INFO <= (ctx)->conf->loglevel)
#define is_debug(ctx) ((ctx)->conf == NULL || LOG_DEBUG <= (ctx)->conf->loglevel)

#define pr_fatal(ctx, args...) pr_log(ctx, LOG_FATAL, "Fatal: ", args)
#define pr_error(ctx, args...) pr_log(ctx, LOG_ERROR, "Error: ", args)
#define pr_warn(ctx, args...) pr_log(ctx, LOG_WARN, "Warning: ", args)
#define pr_info(ctx, args...) pr_log(ctx, LOG_INFO, "", args)
#define pr_debug(ctx, args...) pr_log(ctx, LOG_DEBUG, "DEBUG: ", args)

#define warn_errno(ctx, message) pr_warn(ctx, "%s: %s", message, strerror(errno))
#define exit_fatal(ctx, args...) do { pr_fatal(ctx, args); abort(); } while(0)
#define exit_bug(ctx, message) exit_fatal(ctx, "BUG: %s", message)
#define exit_error(ctx, args...) do { pr_error(ctx, args); exit(1); } while(0)
#define exit_errno(ctx, message) exit_error(ctx, "%s: %s", message, strerror(errno))


static inline fastd_buffer fastd_buffer_alloc(size_t len, size_t head_space, size_t tail_space) {
	size_t base_len = head_space+len+tail_space;
	uint8_t *ptr = malloc(head_space+len);
	return (fastd_buffer){ .base = ptr, .base_len = base_len, .data = ptr+head_space, .len = len };
}

static inline void fastd_buffer_free(fastd_buffer buffer) {
	free(buffer.base);
}

static inline size_t fastd_max_packet_size(const fastd_context *ctx) {
	switch (ctx->conf->protocol) {
	case PROTOCOL_ETHERNET:
		return ctx->conf->mtu+ETH_HLEN;
	case PROTOCOL_IP:
		return ctx->conf->mtu;
	default:
		exit_bug(ctx, "invalid protocol");
	}
}

#endif /* _FASTD_FASTD_H_ */
