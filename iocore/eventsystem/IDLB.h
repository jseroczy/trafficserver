#ifndef IDLB_H
#define IDLB_H

#include <dlb.h>
#include <atomic>
#include "I_EventSystem.h"

namespace IDLB
{

	constexpr static auto CQ_DEPTH  = 128;

	/* DLB queue class */
	class DLB_queue
	{
		int queue_id;

		/* port for rx is using only for this queue */
		dlb_port_hdl_t rx_port;
        dlb_port_hdl_t add_port_rx(bool);

		/* constructor variables */
		bool combined_credits;
		int ldb_pool_id;
		int dir_pool_id;
		dlb_domain_hdl_t domain_hdl;
		std::atomic<uint32_t> elements_in_queue{};

	public:
		int get_queue_id() { return queue_id; }

		DLB_queue(bool, int, int, dlb_domain_hdl_t);
		~DLB_queue();

		bool enqueue(Event *e, dlb_port_hdl_t);
		Event *dequeue_external();
		bool is_empty() { return (elements_in_queue == 0); }
	};

	/* DLB_queues function */
	DLB_queue *get_dlb_queue();
	dlb_port_hdl_t get_tx_port();

	void push_back_dlb_queue(DLB_queue **q);

	/* DLB device */
	class DLB_device
	{
		const int device_ID = 1;

		dlb_hdl_t dlb_hdl;
		int partial_resources = 100;
		int num_credit_combined;
		int num_credit_ldb = 128;
		int num_credit_dir = 128;
		bool use_max_credit_combined = true;
		bool use_max_credit_ldb = false;
		bool use_max_credit_dir = true;

		dlb_resources_t rsrcs;
		dlb_dev_cap_t cap;

		/* schedular domain id */
		int domain_id;
		/* schedulare domain handler */
		dlb_domain_hdl_t domain;
 		int ldb_pool_id;
 		int dir_pool_id;
 		int  create_sched_domain();
		void start_sched();

		dlb_port_hdl_t add_ldb_port_tx();
		dlb_port_hdl_t add_dir_port_tx();

	public:
		DLB_device();
		~DLB_device();
	};

}

#endif /* define IDLB_H */
