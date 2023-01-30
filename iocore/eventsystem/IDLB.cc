#include <error.h>
#include <vector>
#include <dirent.h>
#include "P_EventSystem.h"

namespace IDLB
{
	DLB_Manager* DLB_Manager::_instance = nullptr;	
	std::mutex dlb_sin_m;
	static std::mutex dlb_ports_mtx;
	/*************************************
	*DLB_Manager methods
	**************************************/
	DLB_Manager::DLB_Manager()
	{
		dlb_dev = new DLB_device(0);
	}

	DLB_Manager::~DLB_Manager()
	{
		delete dlb_dev;
	}


	DLB_Manager * DLB_Manager::getInstance()
	{
		dlb_sin_m.lock();
		if(_instance == nullptr)
		{
			_instance = new DLB_Manager;
		}
		dlb_sin_m.unlock();
		return _instance;
	}

	void DLB_Manager::push_back_rx_port(dlb_port_hdl_t port)
	{

	}

	Event* DLB_Manager::dequeue(dlb_port_hdl_t port)
	{
		Event * event = nullptr;
		dlb_event_t event_dlb;
		//printf("port: %p\t", port);
		/* Receive the events */
		int ret = dlb_recv(port, 1, false, &event_dlb);
		if (ret == -1)
			printf("Problem with receivig events\n");
		//else if(ret == 0)
		//	printf("Why zero?\n");
		else if(ret)
		{
			event = (Event *)event_dlb.recv.udata64;
		}

		return event;
    }

	void DLB_Manager::enqueue(Event *ev)
	{
		dlb_event_t dlb_event;
		/* Initialize the static fields */
		dlb_event.send.queue_id = dlb_dev->get_queue_id();
		dlb_event.send.sched_type = SCHED_UNORDERED;
		dlb_event.send.priority = 0;
		dlb_event.send.flow_id = 0xABCD;

		dlb_event.adv_send.udata64 = (uint64_t)ev;

		int ret = dlb_send(dlb_dev->get_tx_port(), 1, &dlb_event);
		if (ret == -1)
			printf("Problem with receivig events\n");
		else if(ret == 0)
			printf("Why zero?\n");
		else
			printf("enq: %p\n", ev);

	}

	/*************************************
	*DLB_device methods
	**************************************/
	DLB_Manager::DLB_device::DLB_device(int dev_id) : device_ID{dev_id}
	{
		if (dlb_open(device_ID, &dlb_hdl) == -1)
			error(1, errno, "dlb_open");

		if (dlb_get_dev_capabilities(dlb_hdl, &cap))
			error(1, errno, "dlb_get_dev_capabilities");

		if (dlb_get_num_resources(dlb_hdl, &rsrcs))
			error(1, errno, "dlb_get_num_resources");

		if (dlb_get_ldb_sequence_number_allocation(dlb_hdl, 0, &num_seq_numbers))
			error(1, errno, "dlb_get_ldb_sequence_number_allocation");

		domain_id = create_sched_domain();
    	if (domain_id == -1)
		{
        	error(1, errno, "dlb_create_sched_domain");
		}

		domain = dlb_attach_sched_domain(dlb_hdl, domain_id);
    	if (domain == NULL)
		{
        	error(1, errno, "dlb_attach_sched_domain");
		}

		pool_creation();

		/* Create queue */
		queue_id = create_ldb_queue();
		printf("DLB queue: %d\n", queue_id);
		if (queue_id == -1)
			error(1, errno, "dlb_create_ldb_queue");

		/* Create tx port */
		tx_port = add_ldb_port(false);
 
		/* Create rx ports and add to the vector */
		for(int i = 0; i < 4; i++)
			rx_ports.push_back(add_ldb_port(true));

		/* Start scheduler */
		if (dlb_launch_domain_alert_thread(domain, NULL, NULL))
			error(1, errno, "dlb_launch_domain_alert_thread");

		if (dlb_start_sched_domain(domain))
			error(1, errno, "dlb_start_sched_domain");

		printf("DEBUG: DLB device succesfully opened\n");
	}

	DLB_Manager::DLB_device::~DLB_device()
	{
		if (dlb_detach_sched_domain(domain) == -1)
    		error(1, errno, "dlb_detach_sched_domain");

		if (dlb_reset_sched_domain(dlb_hdl, domain_id) == -1)
        	error(1, errno, "dlb_reset_sched_domain");

		if (dlb_close(dlb_hdl) == -1)
			error(1, errno, "dlb_close");
	}

	int DLB_Manager::DLB_device::create_sched_domain()
	{
		int p_rsrsc = partial_resources;
		dlb_create_sched_domain_t args;

		args.num_ldb_queues = 1;
		args.num_ldb_ports = rsrcs.num_ldb_ports;
		args.num_dir_ports = 0;
		args.num_ldb_event_state_entries = 2 * CQ_DEPTH * 32;
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

	void DLB_Manager::DLB_device::pool_creation()
	{
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
		} 
		else 
		{
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
	}

	dlb_port_hdl_t DLB_Manager::DLB_device::add_ldb_port(bool is_rx)
	{
		int port_id = create_ldb_port();
		if (port_id == -1)
			error(1, errno, "dlb_create_ldb_port");
		printf("ldb port created\n");

    	dlb_port_hdl_t port = dlb_attach_ldb_port(domain, port_id);
		if (port == NULL)
			error(1, errno, "dlb_attach_ldb_port");
		printf("ldb port attached\n");


		if (is_rx)
		{
			if(dlb_link_queue(port, queue_id, 0) == -1)
				error(1, errno, "dlb_link_queue");
			printf("port_rx: %p\n", port);
		}
		printf("dlb link queue and port\n");

		return port;
	}

	int DLB_Manager::DLB_device::create_ldb_port()
	{
		dlb_create_port_t args;

		if (!cap.combined_credits) 
		{
			args.ldb_credit_pool_id = ldb_pool_id;
			args.dir_credit_pool_id = dir_pool_id;
		} 
		else 
		{
    		args.credit_pool_id = ldb_pool_id;
    	}

    	args.cq_depth = CQ_DEPTH;
    	args.num_ldb_event_state_entries = CQ_DEPTH*2;
    	args.cos_id = DLB_PORT_COS_ID_ANY;

    	return dlb_create_ldb_port(domain, &args);
	}

	int DLB_Manager::DLB_device::create_ldb_queue()
	{
    	dlb_create_ldb_queue_t args = {0};

    	args.num_sequence_numbers = 0; //num_seq_numbers;

    	return dlb_create_ldb_queue(domain, &args);
	}

	dlb_port_hdl_t DLB_Manager::DLB_device::get_rx_port()
	{
		dlb_port_hdl_t port;

		dlb_ports_mtx.lock();
		if(rx_ports.empty())
		{
			printf("Error: There are no free tx ports\n");
			exit(1);
		}
		printf("Give port\n");
		port = rx_ports.back();
		rx_ports.pop_back();
		dlb_ports_mtx.unlock();

		return port;
	}

}