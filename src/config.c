/*
  Copyright (c) 2012-2014, Matthias Schiffer <mschiffer@universe-factory.net>
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


#include "fastd.h"
#include "config.h"
#include "crypto.h"
#include "lex.h"
#include "method.h"
#include "peer.h"
#include <config.yy.h>

#include <dirent.h>
#include <grp.h>
#include <libgen.h>
#include <pwd.h>
#include <stdarg.h>
#include <strings.h>

#include <sys/stat.h>
#include <sys/types.h>


fastd_config_t conf = {};


extern const fastd_protocol_t fastd_protocol_ec25519_fhmqvc;


static void default_config(void) {
	memset(&conf, 0, sizeof(fastd_config_t));

	conf.log_syslog_ident = strdup("fastd");

	conf.maintenance_interval = 10;
	conf.keepalive_timeout = 15;
	conf.peer_stale_time = 90;
	conf.eth_addr_stale_time = 300;

	conf.reorder_time = 10;

	conf.min_handshake_interval = 15;
	conf.min_resolve_interval = 15;

	conf.mtu = 1500;
	conf.mode = MODE_TAP;

	conf.secure_handshakes = true;
	conf.drop_caps = DROP_CAPS_ON;

	conf.protocol = &fastd_protocol_ec25519_fhmqvc;
	conf.key_valid = 3600;		/* 60 minutes */
	conf.key_valid_old = 60;	/* 1 minute */
	conf.key_refresh = 3300;	/* 55 minutes */
	conf.key_refresh_splay = 300;	/* 5 minutes */

#ifdef WITH_VERIFY
	conf.min_verify_interval = 10;
	conf.verify_valid_time = 60;	/* 1 minute */
#endif

	conf.peer_group = calloc(1, sizeof(fastd_peer_group_config_t));
	conf.peer_group->name = strdup("default");
	conf.peer_group->max_connections = -1;

	conf.ciphers = fastd_cipher_config_alloc();
	conf.macs = fastd_mac_config_alloc();
}

void fastd_config_protocol(const char *name) {
	if (!strcmp(name, "ec25519-fhmqvc"))
		conf.protocol = &fastd_protocol_ec25519_fhmqvc;
	else
		exit_error("config error: protocol `%s' not supported", name);
}

void fastd_config_method(const char *name) {
	fastd_string_stack_t **method;

	for (method = &conf.method_list; *method; method = &(*method)->next) {
		if (!strcmp((*method)->str, name)) {
			pr_debug("duplicate method name `%s', ignoring", name);
			return;
		}
	}

	*method = fastd_string_stack_dup(name);
}

void fastd_config_cipher(const char *name, const char *impl) {
	if (!fastd_cipher_config(conf.ciphers, name, impl))
		exit_error("config error: implementation `%s' is not supported for cipher `%s' (or cipher `%s' is not supported)", impl, name, name);
}

void fastd_config_mac(const char *name, const char *impl) {
	if (!fastd_mac_config(conf.macs, name, impl))
		exit_error("config error: implementation `%s' is not supported for MAC `%s' (or MAC `%s' is not supported)", impl, name, name);
}

void fastd_config_bind_address(const fastd_peer_address_t *address, const char *bindtodev, bool default_v4, bool default_v6) {
#ifndef USE_BINDTODEVICE
	if (bindtodev && !fastd_peer_address_is_v6_ll(address))
		exit_error("config error: device bind configuration not supported on this system");
#endif

#ifndef USE_MULTIAF_BIND
	if (address->sa.sa_family == AF_UNSPEC) {
		fastd_peer_address_t addr4 = { .in = { .sin_family = AF_INET, .sin_port = address->in.sin_port } };
		fastd_peer_address_t addr6 = { .in6 = { .sin6_family = AF_INET6, .sin6_port = address->in.sin_port } };

		fastd_config_bind_address(&addr4, bindtodev, default_v4, default_v6);
		fastd_config_bind_address(&addr6, bindtodev, default_v4, default_v6);
		return;
	}
#endif

	fastd_bind_address_t *addr = malloc(sizeof(fastd_bind_address_t));
	addr->next = conf.bind_addrs;
	conf.bind_addrs = addr;
	conf.n_bind_addrs++;

	addr->addr = *address;
	addr->bindtodev = bindtodev ? strdup(bindtodev) : NULL;

	fastd_peer_address_simplify(&addr->addr);

	if (addr->addr.sa.sa_family != AF_INET6 && (default_v4 || !conf.bind_addr_default_v4))
		conf.bind_addr_default_v4 = addr;

	if (addr->addr.sa.sa_family != AF_INET && (default_v6 || !conf.bind_addr_default_v6))
		conf.bind_addr_default_v6 = addr;
}

void fastd_config_peer_group_push(const char *name) {
	fastd_peer_group_config_t *group = calloc(1, sizeof(fastd_peer_group_config_t));
	group->name = strdup(name);
	group->max_connections = -1;

	group->parent = conf.peer_group;
	group->next = group->parent->children;

	group->parent->children = group;

	conf.peer_group = group;
}

void fastd_config_peer_group_pop(void) {
	conf.peer_group = conf.peer_group->parent;
}

static void free_peer_group(fastd_peer_group_config_t *group) {
	while (group->children) {
		fastd_peer_group_config_t *next = group->children->next;
		free_peer_group(group->children);
		group->children = next;
	}

	fastd_string_stack_free(group->peer_dirs);
	free(group->name);
	free(group);
}

static bool has_peer_group_peer_dirs(const fastd_peer_group_config_t *group) {
	if (group->peer_dirs)
		return true;

	const fastd_peer_group_config_t *child;
	for (child = group->children; child; child = child->next) {
		if (has_peer_group_peer_dirs(child))
			return true;
	}

	return false;
}

static void read_peer_dir(const char *dir) {
	DIR *dirh = opendir(".");

	if (dirh) {
		while (true) {
			errno = 0;
			struct dirent *result = readdir(dirh);
			if (!result) {
				if (errno)
					pr_error_errno("readdir");

				break;
			}

			if (result->d_name[0] == '.')
				continue;

			if (result->d_name[strlen(result->d_name)-1] == '~') {
				pr_verbose("ignoring file `%s' as it seems to be a backup file", result->d_name);
				continue;
			}

			struct stat statbuf;
			if (stat(result->d_name, &statbuf)) {
				pr_warn("ignoring file `%s': stat failed: %s", result->d_name, strerror(errno));
				continue;
			}
			if ((statbuf.st_mode & S_IFMT) != S_IFREG) {
				pr_info("ignoring file `%s': no regular file", result->d_name);
				continue;
			}

			fastd_peer_config_new();
			conf.peers->name = strdup(result->d_name);
			conf.peers->config_source_dir = dir;

			if (!fastd_read_config(result->d_name, true, 0)) {
				pr_warn("peer config `%s' will be ignored", result->d_name);
				fastd_peer_config_delete();
			}
		}

		if (closedir(dirh) < 0)
			pr_error_errno("closedir");

	}
	else {
		pr_error("opendir for `%s' failed: %s", dir, strerror(errno));
	}
}

static void read_peer_dirs(void) {
	char *oldcwd = get_current_dir_name();

	fastd_string_stack_t *dir;
	for (dir = conf.peer_group->peer_dirs; dir; dir = dir->next) {
		if (!chdir(dir->str))
			read_peer_dir(dir->str);
		else
			pr_error("change from directory `%s' to `%s' failed: %s", oldcwd, dir->str, strerror(errno));
	}

	if (chdir(oldcwd))
		pr_error("can't chdir to `%s': %s", oldcwd, strerror(errno));

	free(oldcwd);
}

void fastd_add_peer_dir(const char *dir) {
	char *oldcwd = get_current_dir_name();

	if (!chdir(dir)) {
		char *newdir = get_current_dir_name();
		conf.peer_group->peer_dirs = fastd_string_stack_push(conf.peer_group->peer_dirs, newdir);
		free(newdir);

		if(chdir(oldcwd))
			pr_error("can't chdir to `%s': %s", oldcwd, strerror(errno));
	}
	else {
		pr_error("change from directory `%s' to `%s' failed: %s", oldcwd, dir, strerror(errno));
	}

	free(oldcwd);
}

bool fastd_read_config(const char *filename, bool peer_config, int depth) {
	if (depth >= MAX_CONFIG_DEPTH)
		exit_error("maximum config include depth exceeded");

	bool ret = true;
	char *oldcwd = get_current_dir_name();
	char *filename2 = NULL;
	char *dir = NULL;
	FILE *file;
	fastd_lex_t *lex = NULL;
	fastd_config_pstate *ps;
	fastd_string_stack_t *strings = NULL;

	ps = fastd_config_pstate_new();

	if (!filename) {
		file = stdin;
	}
	else {
		file = fopen(filename, "r");
		if (!file) {
			pr_error("can't open config file `%s': %s", filename, strerror(errno));
			ret = false;
			goto end_free;
		}
	}

	lex = fastd_lex_init(file);

	if (filename) {
		filename2 = strdup(filename);
		dir = dirname(filename2);

		if (chdir(dir)) {
			pr_error("change from directory `%s' to `%s' failed", oldcwd, dir);
			ret = false;
			goto end_free;
		}
	}

	int token;
	YYSTYPE token_val;
	YYLTYPE loc = {1, 0, 1, 0};

	if (peer_config)
		token = START_PEER_CONFIG;
	else
		token = conf.peer_group->parent ? START_PEER_GROUP_CONFIG : START_CONFIG;

	int parse_ret = fastd_config_push_parse(ps, token, &token_val, &loc, filename, depth+1);

	while(parse_ret == YYPUSH_MORE) {
		token = fastd_lex(&token_val, &loc, lex);

		if (token < 0) {
			pr_error("config error: %s at %s:%i:%i", token_val.error, filename, loc.first_line, loc.first_column);
			ret = false;
			goto end_free;
		}

		if (token == TOK_STRING) {
			token_val.str->next = strings;
			strings = token_val.str;
		}

		parse_ret = fastd_config_push_parse(ps, token, &token_val, &loc, filename, depth+1);
	}

	if (parse_ret)
		ret = false;

 end_free:
	fastd_string_stack_free(strings);

	fastd_lex_destroy(lex);
	fastd_config_pstate_delete(ps);

	if(chdir(oldcwd))
		pr_error("can't chdir to `%s': %s", oldcwd, strerror(errno));

	free(filename2);
	free(oldcwd);

	if (filename && file)
		fclose(file);

	return ret;
}

static void assess_peers(void) {
	conf.has_floating = false;

	fastd_peer_config_t *peer;
	for (peer = conf.peers; peer; peer = peer->next) {
		if (fastd_peer_config_is_floating(peer))
			conf.has_floating = true;
	}
}


static void configure_user(void) {
	conf.uid = getuid();
	conf.gid = getgid();

	if (conf.user) {
		struct passwd pwd, *pwdr;
		size_t bufspace = 1024;
		int error;

		do {
			char buf[bufspace];
			error = getpwnam_r(conf.user, &pwd, buf, bufspace, &pwdr);
			bufspace *= 2;
		} while(error == ERANGE);

		if (error)
			exit_errno("getpwnam_r");

		if (!pwdr)
			exit_error("config error: unable to find user `%s'.", conf.user);

		conf.uid = pwdr->pw_uid;
		conf.gid = pwdr->pw_gid;
	}

	if (conf.group) {
		struct group grp, *grpr;
		size_t bufspace = 1024;
		int error;

		do {
			char buf[bufspace];
			error = getgrnam_r(conf.group, &grp, buf, bufspace, &grpr);
			bufspace *= 2;
		} while(error == ERANGE);

		if (error)
			exit_errno("getgrnam_r");

		if (!grpr)
			exit_error("config error: unable to find group `%s'.", conf.group);

		conf.gid = grpr->gr_gid;
	}

	if (conf.user) {
		int ngroups = 0;
		if (getgrouplist(conf.user, conf.gid, NULL, &ngroups) < 0) {
			/* the user has supplementary groups */

			conf.groups = calloc(ngroups, sizeof(gid_t));
			if (getgrouplist(conf.user, conf.gid, conf.groups, &ngroups) < 0)
				exit_errno("getgrouplist");

			conf.n_groups = ngroups;
		}
	}
}

static void configure_method_parameters(void) {
	conf.max_overhead = 0;
	conf.min_encrypt_head_space = 0;
	conf.min_decrypt_head_space = 0;
	conf.min_encrypt_tail_space = 0;
	conf.min_decrypt_tail_space = 0;

	size_t i;
	for (i = 0; conf.methods[i].name; i++) {
		const fastd_method_provider_t *provider = conf.methods[i].provider;

		conf.max_overhead = max_size_t(conf.max_overhead, provider->max_overhead);
		conf.min_encrypt_head_space = max_size_t(conf.min_encrypt_head_space, provider->min_encrypt_head_space);
		conf.min_decrypt_head_space = max_size_t(conf.min_decrypt_head_space, provider->min_decrypt_head_space);
		conf.min_encrypt_tail_space = max_size_t(conf.min_encrypt_tail_space, provider->min_encrypt_tail_space);
		conf.min_decrypt_tail_space = max_size_t(conf.min_decrypt_tail_space, provider->min_decrypt_tail_space);
	}

	conf.min_encrypt_head_space = alignto(conf.min_encrypt_head_space, 16);

	/* ugly hack to get alignment right for aes128-gcm, which needs data aligned to 16 and has a 24 byte header */
	conf.min_decrypt_head_space = alignto(conf.min_decrypt_head_space, 16) + 8;
}

static void configure_methods(void) {
	size_t n_methods = 0, i;
	fastd_string_stack_t *method_name;
	for (method_name = conf.method_list; method_name; method_name = method_name->next)
		n_methods++;

	conf.methods = calloc(n_methods+1, sizeof(fastd_method_info_t));

	for (i = 0, method_name = conf.method_list; method_name; i++, method_name = method_name->next) {
		conf.methods[i].name = method_name->str;
		if (!fastd_method_create_by_name(method_name->str, &conf.methods[i].provider, &conf.methods[i].method))
			exit_error("config error: method `%s' not supported", method_name->str);
	}

	configure_method_parameters();
}

static void destroy_methods(void) {
	size_t i;
	for (i = 0; conf.methods[i].name; i++) {
		conf.methods[i].provider->destroy(conf.methods[i].method);
	}

	free(conf.methods);
}

void fastd_configure(int argc, char *const argv[]) {
	default_config();

	fastd_config_handle_options(argc, argv);

	if (!conf.log_stderr_level && !conf.log_syslog_level)
		conf.log_stderr_level = FASTD_DEFAULT_LOG_LEVEL;
}

static void config_check_base(void) {
	if (conf.ifname) {
		if (strchr(conf.ifname, '/'))
			exit_error("config error: invalid interface name");
	}

	if (conf.mode == MODE_TUN) {
		if (conf.peers->next)
			exit_error("config error: in TUN mode exactly one peer must be configured");
		if (conf.peer_group->children)
			exit_error("config error: in TUN mode peer groups can't be used");
		if (has_peer_group_peer_dirs(conf.peer_group))
			exit_error("config error: in TUN mode peer directories can't be used");
	}

#ifndef USE_PMTU
	if (conf.pmtu.set)
		exit_error("config error: setting pmtu is not supported on this system");
#endif

#ifndef USE_PACKET_MARK
	if (conf.packet_mark)
		exit_error("config error: setting a packet mark is not supported on this system");
#endif
}

void fastd_config_check(void) {
	config_check_base();

	if (conf.mode == MODE_TUN) {
		if (!conf.peers)
			exit_error("config error: in TUN mode exactly one peer must be configured");
	}

	if (!conf.peers && !has_peer_group_peer_dirs(conf.peer_group))
		exit_error("config error: neither fixed peers nor peer dirs have been configured");

	if (!conf.method_list) {
		pr_warn("no encryption method configured, falling back to method `null' (unencrypted)");
		fastd_config_method("null");
	}

	configure_user();
	configure_methods();
}

void fastd_config_verify(void) {
	config_check_base();
	configure_methods();

	fastd_peer_config_t *peer;
	for (peer = conf.peers; peer; peer = peer->next)
		conf.protocol->peer_verify(peer);
}

static void peer_dirs_read_peer_group(void) {
	read_peer_dirs();

	fastd_peer_group_config_t *base = conf.peer_group, *group;
	for (group = conf.peer_group->children; group; group = group->next) {
		conf.peer_group = group;
		peer_dirs_read_peer_group();
	}

	conf.peer_group = base;
}

static void peer_dirs_handle_old_peers(fastd_peer_config_t **old_peers, fastd_peer_config_t **new_peers) {
	fastd_peer_config_t **peer, **next, **new_peer, **new_next;
	for (peer = old_peers; *peer; peer = next) {
		next = &(*peer)->next;

		/* don't touch statically configured peers */
		if (!(*peer)->config_source_dir)
			continue;

		/* search for each peer in the list of new peers */
		for (new_peer = new_peers; *new_peer; new_peer = new_next) {
			new_next = &(*new_peer)->next;

			if (((*peer)->config_source_dir == (*new_peer)->config_source_dir) && strequal((*peer)->name, (*new_peer)->name)) {
				if (fastd_peer_config_equal(*peer, *new_peer)) {
					pr_verbose("peer `%s' unchanged", (*peer)->name);

					fastd_peer_config_t *free_peer = *new_peer;
					*new_peer = *new_next;
					fastd_peer_config_free(free_peer);
					peer = NULL;
				}
				else {
					pr_verbose("peer `%s' changed, resetting", (*peer)->name);
					new_peer = NULL;
				}

				break;
			}
		}

		/* no new peer was found, or the old one has changed */
		if (peer && (!new_peer || !*new_peer)) {
			pr_verbose("removing peer `%s'", (*peer)->name);

			fastd_peer_config_t *free_peer = *peer;
			*peer = *next;
			next = peer;

			fastd_peer_config_purge(free_peer);
		}
	}
}

static void peer_dirs_handle_new_peers(fastd_peer_config_t **peers, fastd_peer_config_t *new_peers) {
	fastd_peer_config_t *peer;
	for (peer = new_peers; peer; peer = peer->next) {
		if (peer->next)
			continue;

		peer->next = *peers;
		*peers = new_peers;
		return;
	}
}

void fastd_config_load_peer_dirs(void) {
	fastd_peer_config_t *old_peers = conf.peers;
	conf.peers = NULL;

	peer_dirs_read_peer_group();

	fastd_peer_config_t *new_peers = conf.peers;
	conf.peers = old_peers;

	peer_dirs_handle_old_peers(&conf.peers, &new_peers);
	peer_dirs_handle_new_peers(&conf.peers, new_peers);

	assess_peers();
}

void fastd_config_release(void) {
	while (conf.peers)
		fastd_peer_config_delete();

	while (conf.bind_addrs) {
		fastd_bind_address_t *next = conf.bind_addrs->next;
		free(conf.bind_addrs->bindtodev);
		free(conf.bind_addrs);
		conf.bind_addrs = next;
	}

	free_peer_group(conf.peer_group);

	destroy_methods();
	fastd_string_stack_free(conf.method_list);

	fastd_mac_config_free(conf.macs);
	fastd_cipher_config_free(conf.ciphers);

	fastd_shell_command_unset(&conf.on_pre_up);
	fastd_shell_command_unset(&conf.on_up);
	fastd_shell_command_unset(&conf.on_down);
	fastd_shell_command_unset(&conf.on_post_down);
	fastd_shell_command_unset(&conf.on_connect);
	fastd_shell_command_unset(&conf.on_establish);
	fastd_shell_command_unset(&conf.on_disestablish);
#ifdef WITH_VERIFY
	fastd_shell_command_unset(&conf.on_verify);
#endif

	free(conf.user);
	free(conf.group);
	free(conf.groups);
	free(conf.ifname);
	free(conf.secret);
	free(conf.protocol_config);
	free(conf.log_syslog_ident);
}
