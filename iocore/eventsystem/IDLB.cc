#include <error.h>
#include <vector>
#include <mutex>
#include <dirent.h>
#include "P_EventSystem.h"

namespace IDLB
{
	DLB_Singleton* DLB_Singleton::_instance = nullptr;
	constexpr static auto CQ_DEPTH  = 256;
	static std::vector< DLB_queue*> queues_private;
	static std::vector<std::vector<dlb_port_hdl_t>>tx_dlb_ports;
	static std::mutex dlb_sin_m;
	static std::mutex dlb_queue_mtx;
	static std::mutex dlb_ports_mtx;

	/*************************************
	*DLB_Singleton methods
	**************************************/

	DLB_Singleton * DLB_Singleton::getInstance()
	{
		dlb_sin_m.lock();
		if(_instance == nullptr)
		{
			_instance = new DLB_Singleton;
		}
		dlb_sin_m.unlock();
		return _instance;
	}

	DLB_Singleton::DLB_Singleton()
	{
		DIR *directory;
		struct dirent *dir_ent;
		directory = opendir("/dev/");
		if (directory)
		{
			while ((dir_ent = readdir(directory)) != NULL)
			{
				if(strstr(dir_ent->d_name, "dlb"))
				{
					int num = strtol (dir_ent->d_name + strlen("dlb"),NULL,10);
					dlb_devices.push_back(new DLB_device(num));
					dlb_dev_ctr++;
				}
			}
			closedir(directory);
		}
		if(!dlb_dev_ctr)
		{
			printf("There are no dlb devices\n");
			exit(EXIT_FAILURE);
		}
	}

	DLB_Singleton::~DLB_Singleton()
	{
		DLB_device *d_ptr;
		while(dlb_devices.size())
		{
			d_ptr = dlb_devices.back();
			dlb_devices.pop_back();
			delete d_ptr;
		}
	}

	DLB_queue* DLB_Singleton::get_dlb_queue()
	{
		DLB_queue *ptr = nullptr;

		dlb_queue_mtx.lock();
		if(queues_private.empty())
		{
			printf("Error: There are no free dlb queues\n");
			exit(EXIT_FAILURE);
		}
		ptr = queues_private.back();
		queues_private.pop_back();
		dlb_queue_mtx.unlock();

		return ptr;
	}

	dlb_port_hdl_t DLB_Singleton::get_tx_port(int dlb_n)
	{
		dlb_port_hdl_t port;

		dlb_ports_mtx.lock();
		if(tx_dlb_ports.empty() || tx_dlb_ports[dlb_n].empty())
		{
			printf("Error: There are no free tx ports\n");
			exit(EXIT_FAILURE);
		}
		port = tx_dlb_ports[dlb_n].back();
		tx_dlb_ports[dlb_n].pop_back();
		dlb_ports_mtx.unlock();

		return port;
	}

	void DLB_Singleton::push_back_dlb_queue(DLB_queue **q)
	{
		queues_private.push_back(*q);
		*q = nullptr;
	}

	void DLB_Singleton::push_back_tx_port(dlb_port_hdl_t *port, int port_dev_id)
	{
	}

	/****************************
	*DLB_queue methods
	****************************/
	DLB_queue::DLB_queue(bool cc, int l_p_id, int d_p_id, dlb_domain_hdl_t d_hdl, int d_n) 
				: combined_credits(cc), ldb_pool_id(l_p_id), dir_pool_id(d_p_id), domain_hdl( d_hdl), dlb_num(d_n)
	{
		queue_id = dlb_create_dir_queue(domain_hdl, -1);
		if (queue_id == -1)
			error(1, errno, "dlb_create_dir_queue");

		rx_port = add_port_rx();
		elements_in_queue = 0;
	}

	DLB_queue::~DLB_queue()
	{
		/* Detach all ports */
		if(dlb_detach_port(rx_port) == -1)
			error(1, errno, "dlb_detach_port");
	}

	dlb_port_hdl_t DLB_queue::add_port_rx()
	{
		/* Prepare args for the port */
		dlb_create_port_t args;

		if (!combined_credits) {
			args.ldb_credit_pool_id = ldb_pool_id;
			args.dir_credit_pool_id = dir_pool_id;
		} else {
			args.credit_pool_id = ldb_pool_id;
		}

		args.cq_depth = CQ_DEPTH;

		int port_id = dlb_create_dir_port(domain_hdl, &args, queue_id);
		if (port_id == -1)
			error(1, errno, "dlb_create_dir_port");

		dlb_port_hdl_t port = dlb_attach_dir_port(domain_hdl, port_id);
		if (port == NULL)
			error(1, errno, "dlb_attach_dir_port");

		return port;
	}

	bool
	DLB_queue::enqueue(Event *e, dlb_port_hdl_t port_tx)
	{
		dlb_event_t event;
		const int retry = 10;
		bool was_empty;

		/* Initialize the static fields in the send event */
		event.send.flow_id = 0;
		event.send.queue_id = queue_id;
		event.send.sched_type = SCHED_DIRECTED;
		event.send.priority = 0;
		/* Initialize the dynamic fields in the send event */
		event.adv_send.udata64 = (uint64_t)e;

		/* Send the event */
		auto ret = 0;
		for(int i = 0; i < retry; i++)
		{
			was_empty = (bool)(elements_in_queue == 0);
			ret = dlb_send(port_tx, 1, &event);
			if(ret == -1)
			{
				perror("Problem with sending packets");
				exit(EXIT_FAILURE);
			}
			else if(ret)
			{
				elements_in_queue += ret;
				break;
			}

		}

		return was_empty;
	}

	Event *
	DLB_queue::dequeue_external()
	{
		int ret = 0;
		dlb_event_t event_dlb;
		Event *event_rx = nullptr;

		ret = dlb_recv(rx_port, 1, false, &event_dlb);
		if(ret == -1 )
		{
			perror("Problem with receiving packets");
			exit(EXIT_FAILURE);
		}
		else if(ret)
		{
			elements_in_queue -= ret;
			event_rx = (Event *)event_dlb.recv.udata64;
		}
		return event_rx;
	}


	/**************************************************************
	*DLB device class
	*************************************************************/
	DLB_Singleton::DLB_device::DLB_device(int dev_id)
	{
		device_ID = dev_id;
		if (dlb_open(device_ID, &dlb_hdl) == -1)
			error(1, errno, "dlb_open");

		if (dlb_get_dev_capabilities(dlb_hdl, &cap))
			error(1, errno, "dlb_get_dev_capabilities");

		if (dlb_get_num_resources(dlb_hdl, &rsrcs))
			error(1, errno, "dlb_get_num_resources");

		domain_id = create_sched_domain();
		if (domain_id == -1)
			error(1, errno, "dlb_create_sched_domain");

		domain = dlb_attach_sched_domain(dlb_hdl, domain_id);
		if (domain == NULL)
			error(1, errno, "dlb_attach_sched_domain");

		if (!cap.combined_credits)
		{
			int max_ldb_credits = rsrcs.num_ldb_credits * partial_resources / 100;
			int max_dir_credits = rsrcs.num_dir_credits * partial_resources / 100;

			if (use_max_credit_ldb == true)
				ldb_pool_id = dlb_create_ldb_credit_pool(domain, max_ldb_credits);
			else if (num_credit_ldb <= max_ldb_credits)
				ldb_pool_id = dlb_create_ldb_credit_pool(domain, num_credit_ldb);
			else
				error(1, EINVAL, "Requested ldb credits are unavailable!");

			if (ldb_pool_id == -1)
				error(1, errno, "dlb_create_ldb_credit_pool");

			if (use_max_credit_dir == true)
				dir_pool_id = dlb_create_dir_credit_pool(domain, max_dir_credits);
			else if (num_credit_dir <= max_dir_credits)
				dir_pool_id = dlb_create_dir_credit_pool(domain, num_credit_dir);
			else
				error(1, EINVAL, "Requested dir credits are unavailable!");

			if (dir_pool_id == -1)
				error(1, errno, "dlb_create_dir_credit_pool");
		}else{
			int max_credits = rsrcs.num_credits * partial_resources / 100;

			if (use_max_credit_combined == true)
				ldb_pool_id = dlb_create_credit_pool(domain, max_credits);
			else if (num_credit_combined <= max_credits)
				ldb_pool_id = dlb_create_credit_pool(domain, num_credit_combined);
			else
				error(1, EINVAL, "Requested combined credits are unavailable!");

			if (ldb_pool_id == -1)
				error(1, errno, "dlb_create_credit_pool");
		}

		/* Create queues */
		for(uint32_t i = 0; i < rsrcs.num_dir_ports; i++)
			queues_private.push_back(new DLB_queue(cap.combined_credits, ldb_pool_id, dir_pool_id, domain, tx_dlb_ports.size()));

		/* create tx_ports */
		std::vector<dlb_port_hdl_t>v_ports;
		for(uint32_t i = 0; i < rsrcs.num_ldb_ports; i++)
			v_ports.push_back(add_ldb_port_tx());

		tx_dlb_ports.push_back(v_ports);

		start_sched();
	}

	DLB_Singleton::DLB_device::~DLB_device()
	{
		/* Delete queues */
		DLB_queue *q_ptr;
		while(queues_private.size())
		{
			q_ptr = queues_private.back();
			queues_private.pop_back();
			delete q_ptr;
		}

		if (dlb_detach_sched_domain(domain) == -1)
			error(1, errno, "dlb_detach_sched_domain");

		if (dlb_reset_sched_domain(dlb_hdl, domain_id) == -1)
			error(1, errno, "dlb_reset_sched_domain");

		if (dlb_close(dlb_hdl) == -1)
			error(1, errno, "dlb_close");
	}

	void DLB_Singleton::DLB_device::start_sched()
	{
		if (dlb_launch_domain_alert_thread(domain, NULL, NULL))
			error(1, errno, "dlb_launch_domain_alert_thread");

		if (dlb_start_sched_domain(domain))
			error(1, errno, "dlb_start_sched_domain");
	}


	int DLB_Singleton::DLB_device::create_sched_domain()
	{
		int p_rsrsc = partial_resources;
		dlb_create_sched_domain_t args;

		args.num_ldb_queues = 0;
		args.num_ldb_ports = rsrcs.num_ldb_ports;
		args.num_dir_ports = rsrcs.num_dir_ports;
		args.num_ldb_event_state_entries = 64 * 8;
		if (!cap.combined_credits) {
			args.num_ldb_credits = rsrcs.max_contiguous_ldb_credits * p_rsrsc / 100;
			args.num_dir_credits = rsrcs.max_contiguous_dir_credits * p_rsrsc / 100;
			args.num_ldb_credit_pools = 1;
			args.num_dir_credit_pools = 1;
		} else {
			args.num_credits = rsrcs.num_credits * p_rsrsc / 100;
			args.num_credit_pools = 1;
		}
		args.num_sn_slots[0] = rsrcs.num_sn_slots[0] * p_rsrsc / 100;
		args.num_sn_slots[1] = rsrcs.num_sn_slots[1] * p_rsrsc / 100;

		return dlb_create_sched_domain(dlb_hdl, &args);
	}

	dlb_port_hdl_t DLB_Singleton::DLB_device::add_ldb_port_tx()
	{
		dlb_create_port_t args;

		if (!cap.combined_credits) {
			args.ldb_credit_pool_id = ldb_pool_id;
			args.dir_credit_pool_id = dir_pool_id;
		} else {
			args.credit_pool_id = ldb_pool_id;
		}

		args.cq_depth = CQ_DEPTH;
		args.num_ldb_event_state_entries = 8;
		args.cos_id = DLB_PORT_COS_ID_ANY;

 		int port_id = dlb_create_ldb_port(domain, &args);
		if (port_id == -1)
			error(1, errno, "dlb_create_ldb_port");

		dlb_port_hdl_t port = dlb_attach_ldb_port(domain, port_id);
		if (port == NULL)
			error(1, errno, "dlb_attach_ldb_port");

		return port;
	}

	dlb_port_hdl_t DLB_Singleton::DLB_device::add_dir_port_tx()
	{
		dlb_create_port_t args;

		if (!cap.combined_credits) {
			args.ldb_credit_pool_id = ldb_pool_id;
			args.dir_credit_pool_id = dir_pool_id;
		} else {
			args.credit_pool_id = ldb_pool_id;
		}

		args.cq_depth = CQ_DEPTH;

		// Create port
		int port_id;
		port_id = dlb_create_dir_port(domain, &args, -1);
		if (port_id == -1)
			error(1, errno, "dlb_create_dir_port");

		dlb_port_hdl_t port = dlb_attach_dir_port(domain, port_id);
		if (port == NULL)
			error(1, errno, "dlb_attach_dir_port");

		return port;
	}
}
