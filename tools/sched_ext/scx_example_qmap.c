// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2022 Facebook */

#define _GNU_SOURCE
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <bpf/bpf.h>
#include "scx_example_qmap.skel.h"

static volatile int exit_req;

static void sigint_handler(int dummy)
{
	exit_req = 1;
}

int main(int argc, char **argv)
{
	struct scx_example_qmap *skel;
	struct bpf_link *link;

	signal(SIGINT, sigint_handler);
	signal(SIGTERM, sigint_handler);

	libbpf_set_strict_mode(LIBBPF_STRICT_ALL);

	skel = scx_example_qmap__open_and_load();
	if (!skel) {
		fprintf(stderr, "Failed to open_and_load: %s\n",
			strerror(errno));
		return 1;
	}

	link = bpf_map__attach_struct_ops(skel->maps.qmap_ops);
	if (!link) {
		fprintf(stderr, "Failed to attach_struct_ops: %s\n",
			strerror(errno));
		return 1;
	}

	while (!exit_req && !skel->bss->exited) {
		long nr_queued = skel->bss->nr_queued;
		long nr_dispatched = skel->bss->nr_dispatched;

		printf("%lu queued, %lu dispatched, delta=%ld, nr_bad_lookups=%lu\n",
		       nr_queued, nr_dispatched, nr_queued - nr_dispatched,
		       skel->bss->nr_bad_lookups);
		fflush(stdout);
		sleep(1);
	}

	bpf_link__destroy(link);
	scx_example_qmap__destroy(skel);

	return 0;
}
