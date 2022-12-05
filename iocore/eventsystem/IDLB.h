#ifndef IDLB_H
#define IDLB_H

#include <dlb.h>
#include <atomic>
#include "I_EventSystem.h"
#include <iostream>

namespace IDLB
{
	/* DLB queue class */
	class DLB_queue
	{
		bool combined_credits;
		int ldb_pool_id;
		int dir_pool_id;
		dlb_domain_hdl_t domain_hdl;
		int dlb_num;
		int queue_id;
		/* port for rx is using only for this queue */
		dlb_port_hdl_t rx_port;
		dlb_port_hdl_t add_port_rx();
		std::atomic<uint32_t> elements_in_queue{};

	public:
		int get_queue_id() { return queue_id; }
		int get_dlb_id() { return dlb_num; }

		DLB_queue(bool, int, int, dlb_domain_hdl_t, int);
		~DLB_queue();

		bool enqueue(Event *e, dlb_port_hdl_t);
		Event *dequeue_external();
		bool is_empty() { return (elements_in_queue == 0); }
	};

	/* Singleton */
	class DLB_Singleton
	{
		/* DLB device */
		class DLB_device
		{
			int device_ID;
			dlb_hdl_t dlb_hdl;
			int partial_resources = 100;
			int num_credit_combined;
			int num_credit_ldb = 128;
			int num_credit_dir = 128;
			bool use_max_credit_combined = true;
			bool use_max_credit_ldb = true;
			bool use_max_credit_dir = true;

			dlb_resources_t rsrcs;
			dlb_dev_cap_t cap;
			int domain_id;
			dlb_domain_hdl_t domain;
 			int ldb_pool_id;
 			int dir_pool_id;
 			int  create_sched_domain();
			void start_sched();

			dlb_port_hdl_t add_ldb_port_tx();
			dlb_port_hdl_t add_dir_port_tx();

		public:
			DLB_device(int);
			~DLB_device();
		};
		static DLB_Singleton * _instance;
		std::vector< DLB_device*>dlb_devices;
		DLB_Singleton();
		~DLB_Singleton();
		int dlb_dev_ctr {};

	public:
		static DLB_Singleton * getInstance();
		DLB_queue *get_dlb_queue();
		dlb_port_hdl_t get_tx_port(int dlb_n);
		void push_back_dlb_queue(DLB_queue **q);
		void push_back_tx_port(dlb_port_hdl_t *port, int);
		int dlb_dev_num() { return dlb_dev_ctr; }
	};

}

#endif /* define IDLB_H */
