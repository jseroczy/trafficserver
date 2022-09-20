#ifndef IDLB_H
#define IDLB_H

#include <dlb.h>
#include "I_EventSystem.h"

namespace IDLB
{


	/* DLB queue class */
	class DLB_queue
	{
		int queue_id;

		/* port for reading is usin only for this queue */
		dlb_port_hdl_t rx_port;
		/* port for writing using to write for every queue in domain */
		dlb_port_hdl_t tx_port;

        	dlb_port_hdl_t add_port(bool);

		/* constructor variables */
		bool combined_credits;
		int ldb_pool_id;
		int dir_pool_id;
		dlb_domain_hdl_t domain_hdl;

	public:
		int get_queue_id() { return queue_id; }
		dlb_port_hdl_t get_port() { return tx_port; }
		DLB_queue(bool, int, int, dlb_domain_hdl_t);
		~DLB_queue();

		bool enqueue(Event *e, dlb_port_hdl_t);
		bool remove(Event *e) { return false; }
		Event *dequeue_external();
		bool is_empty() { return false; }
	};

	/* DLB_queues function */
	DLB_queue *get_dlb_queue();
	void push_back_dlb_queue(DLB_queue **q);

	/* DLB device */
	class DLB_device
	{
		int device_ID = 1;

		dlb_hdl_t dlb_hdl;
		int partial_resources = 100;
		int num_credit_combined;
		int num_credit_ldb = 128;
		int num_credit_dir = 128;
		bool use_max_credit_combined = false;
		bool use_max_credit_ldb = false;
		bool use_max_credit_dir = false;

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
		int queue_prog_id;

		dlb_port_hdl_t add_port_rx(int);
		dlb_port_hdl_t add_port_tx();

	public:
		void print_resources();
		DLB_device();
		DLB_device(int);
		~DLB_device();
	};

}

#endif /* define IDLB_H */
