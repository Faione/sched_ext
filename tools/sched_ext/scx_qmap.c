/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2022 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2022 Tejun Heo <tj@kernel.org>
 * Copyright (c) 2022 David Vernet <dvernet@meta.com>
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <inttypes.h>
#include <signal.h>
#include <libgen.h>
#include <bpf/bpf.h>
#include <scx/common.h>
#include "scx_qmap.bpf.skel.h"

const char help_fmt[] =
"A simple five-level FIFO queue sched_ext scheduler.\n"
"\n"
"See the top-level comment in .bpf.c for more details.\n"
"\n"
"Usage: %s [-s SLICE_US] [-e COUNT] [-t COUNT] [-T COUNT] [-l COUNT] [-d PID]\n"
"       [-D LEN] [-p]\n"
"\n"
"  -s SLICE_US   Override slice duration\n"
"  -e COUNT      Trigger scx_bpf_error() after COUNT enqueues\n"
"  -t COUNT      Stall every COUNT'th user thread\n"
"  -T COUNT      Stall every COUNT'th kernel thread\n"
"  -l COUNT      Trigger dispatch infinite looping after COUNT dispatches\n"
"  -d PID        Disallow a process from switching into SCHED_EXT (-1 for self)\n"
"  -D LEN        Set scx_exit_info.dump buffer length\n"
"  -p            Switch only tasks on SCHED_EXT policy intead of all\n"
"  -h            Display this help and exit\n";

static volatile int exit_req;

static void sigint_handler(int dummy)
{
	exit_req = 1;
}

int main(int argc, char **argv)
{
	struct scx_qmap *skel;
	struct bpf_link *link;
	int opt;

	signal(SIGINT, sigint_handler);
	signal(SIGTERM, sigint_handler);

	libbpf_set_strict_mode(LIBBPF_STRICT_ALL);

	skel = scx_qmap__open();
	SCX_BUG_ON(!skel, "Failed to open skel");

	while ((opt = getopt(argc, argv, "s:e:t:T:l:d:D:ph")) != -1) {
		switch (opt) {
		case 's':
			skel->rodata->slice_ns = strtoull(optarg, NULL, 0) * 1000;
			break;
		case 'e':
			skel->bss->test_error_cnt = strtoul(optarg, NULL, 0);
			break;
		case 't':
			skel->rodata->stall_user_nth = strtoul(optarg, NULL, 0);
			break;
		case 'T':
			skel->rodata->stall_kernel_nth = strtoul(optarg, NULL, 0);
			break;
		case 'l':
			skel->rodata->dsp_inf_loop_after = strtoul(optarg, NULL, 0);
			break;
		case 'd':
			skel->rodata->disallow_tgid = strtol(optarg, NULL, 0);
			if (skel->rodata->disallow_tgid < 0)
				skel->rodata->disallow_tgid = getpid();
			break;
		case 'D':
			skel->struct_ops.qmap_ops->exit_dump_len = strtoul(optarg, NULL, 0);
			break;
		case 'p':
			skel->rodata->switch_partial = true;
			skel->struct_ops.qmap_ops->flags |= __COMPAT_SCX_OPS_SWITCH_PARTIAL;
			break;
		default:
			fprintf(stderr, help_fmt, basename(argv[0]));
			return opt != 'h';
		}
	}

	SCX_OPS_LOAD(skel, qmap_ops, scx_qmap, uei);
	link = SCX_OPS_ATTACH(skel, qmap_ops);

	while (!exit_req && !UEI_EXITED(skel, uei)) {
		long nr_enqueued = skel->bss->nr_enqueued;
		long nr_dispatched = skel->bss->nr_dispatched;

		printf("stats  : enq=%lu dsp=%lu delta=%ld reenq=%" PRIu64 " deq=%" PRIu64 " core=%" PRIu64 "\n",
		       nr_enqueued, nr_dispatched, nr_enqueued - nr_dispatched,
		       skel->bss->nr_reenqueued, skel->bss->nr_dequeued,
		       skel->bss->nr_core_sched_execed);
		printf("cpuperf: cur min/avg/max=%u/%u/%u target min/avg/max=%u/%u/%u\n",
		       skel->bss->cpuperf_min,
		       skel->bss->cpuperf_avg,
		       skel->bss->cpuperf_max,
		       skel->bss->cpuperf_target_min,
		       skel->bss->cpuperf_target_avg,
		       skel->bss->cpuperf_target_max);
		fflush(stdout);
		sleep(1);
	}

	bpf_link__destroy(link);
	UEI_REPORT(skel, uei);
	scx_qmap__destroy(skel);
	return 0;
}
