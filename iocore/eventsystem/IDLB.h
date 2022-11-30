#ifndef IDLB_H
#define IDLB_H

#include <dlb.h>
#include <atomic>
#include "I_EventSystem.h"
#include <iostream>
#include <mutex>

namespace IDLB
{

	constexpr static auto CQ_DEPTH  = 256;
	static std::mutex dlb_sin_m;

	/* DLB queue class */
	class DLB_queue
	{
		int queue_id;
		int dlb_id;
		/* port for rx is using only for this queue */
		dlb_port_hdl_t rx_port;
        dlb_port_hdl_t add_port_rx();

		/* constructor variables */
		bool combined_credits;
		int ldb_pool_id;
		int dir_pool_id;
		dlb_domain_hdl_t domain_hdl;
		std::atomic<uint32_t> elements_in_queue{};

	public:
		int get_queue_id() { return queue_id; }
		int get_dlb_id() { return dlb_id; }

		DLB_queue(bool, int, int, dlb_domain_hdl_t, int);
		~DLB_queue();

		bool enqueue(Event *e, dlb_port_hdl_t);
		Event *dequeue_external();
		bool is_empty() { return (elements_in_queue == 0); }
	};

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

	/* Singleton */
	class DLB_Singleton
	{
		static DLB_Singleton * _instance;
		DLB_Singleton()
		{
			std::cout << "Singleton DLB class constuctor called" << std::endl;
			DLB_device *dlb_dev0 = new DLB_device(0);
			dlb_dev_ctr++;
			DLB_device *dlb_dev1 = new DLB_device(1);
			dlb_dev_ctr++;
		}
		int dlb_dev_ctr = 0;

	public:
		static DLB_Singleton * getInstance()
		{
			dlb_sin_m.lock();
			if(_instance == nullptr)
			{
				_instance = new DLB_Singleton;
			}
			dlb_sin_m.unlock();
			return _instance;
		}

        DLB_queue *get_dlb_queue();
        dlb_port_hdl_t get_tx_port(int dlb_n);
        void push_back_dlb_queue(DLB_queue **q);
		int dlb_dev_num() { return dlb_dev_ctr; }
	};

}

#endif /* define IDLB_H */
