#ifndef IDLB_H
#define IDLB_H

#include <dlb.h>
#include <atomic>
#include "I_EventSystem.h"
#include <iostream>
#include <mutex>

namespace IDLB
{

	/* Class designed as a singleton */
	class DLB_Manager
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
			unsigned int num_seq_numbers;

			dlb_resources_t rsrcs;
			dlb_dev_cap_t cap;
			int domain_id;
			dlb_domain_hdl_t domain;
 			int ldb_pool_id;
 			int dir_pool_id;
			int queue_id; //implemented only one instance of queue

			int create_ldb_port();
			dlb_port_hdl_t add_ldb_port(bool);
			int create_ldb_queue();
			void pool_creation();
 			int  create_sched_domain();
			void start_sched();

		public:
			dlb_port_hdl_t tx_port;
			std::vector<dlb_port_hdl_t>rx_ports;
			dlb_port_hdl_t get_tx_port() { return tx_port; }
			dlb_port_hdl_t get_rx_port();
			int get_queue_id() { return queue_id; }
			DLB_device(int);
			~DLB_device();
		};
		//////////////////////////////////////////
		////////Only for now, later in constructor
		DLB_device *dlb_dev;
		//////////////////////////////////////////

		constexpr static auto CQ_DEPTH  = 8;
		static DLB_Manager * _instance;
		DLB_Manager();
		~DLB_Manager();

	public:
		static DLB_Manager * getInstance();
		dlb_port_hdl_t get_rx_port() { return dlb_dev->get_rx_port(); }
		void push_back_rx_port(dlb_port_hdl_t);
		Event* dequeue(dlb_port_hdl_t);
		void enqueue(Event *);

	};

}


#endif /* define IDLB_H */