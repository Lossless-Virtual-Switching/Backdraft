#include <bcc/proto.h>
#include <asm/ptrace.h>  // for struct pt_regs
#include <linux/types.h>
#include <linux/rbtree.h>
#include <linux/skbuff.h>

// creates a table for pushing custom events to userspace via ring buffer
BPF_PERF_OUTPUT(output);

typedef struct notify {
	uint64_t pid;
	uint32_t adv_wnd;
} notify_t;

// Warnning: Only little endian systems!
struct tcphdr {
	__be16  source;
	__be16  dest;
	__be32  seq;
	__be32  ack_seq;
	__u16   res1:4,
		doff:4,
		fin:1,
		syn:1,
		rst:1,
		psh:1,
		ack:1,
		urg:1,
		ece:1,
		cwr:1;
	__be16  window;
	__sum16 check;
	__be16  urg_ptr;
};

/*
   int kprobe____tcp_transmit_skb(struct pt_regs *ctx, struct sock *sk, struct sk_buff *skb, int clone_it,
   gfp_t gfp_mask, u32 rcv_nxt)
   {
   struct tcphdr *th;
   th = (struct tcphdr *)skb->data;
   bpf_trace_printk("tcp transmit skb called (%d).\n", th->dest);
// bpf_trace_printk("tcp transmit skb called (%d).\n", 123);
// bpf_trace_printk("tcp transmit skb called.\n");
return 0;
}
*/

int kprobe____ip_queue_xmit(struct pt_regs *ctx, struct sock *sk, struct sk_buff *skb, void *fl, __u8 tos)
{
	struct tcphdr *th;
	th = (struct tcphdr *)skb->data;

	notify_t n;
	n.pid = (__u32)(bpf_get_current_pid_tgid()>> 32);
	n.adv_wnd = th->window;
	bpf_trace_printk("ip queue xmit called (source: %d, dest: %d, wnd: %d).\n",
	 		th->source, th->dest, th->window);
	// output.perf_submit(ctx, n, sizeof(notify_t));
	return 0;
}
