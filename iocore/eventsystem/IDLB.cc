#include "stdio.h"
#include <error.h>
#include <vector>
#include <mutex>
#include "P_EventSystem.h"

namespace IDLB
{
	/* DLB private variables */
	static std::vector< DLB_queue*> queues_private;
	std::mutex dlb_init_mtx;

	#define CQ_DEPTH 128

	/*************************************
	*External functions to use dlb queues
	**************************************/
	DLB_queue *get_dlb_queue()
	{
		DLB_queue *ptr;
		dlb_init_mtx.lock();
		if(!queues_private.size())
		{
			printf("Error: There are no free dlb queues\n");
			exit(1);
		}
		ptr = queues_private.back();
		queues_private.pop_back();
		dlb_init_mtx.unlock();

		return ptr;
	}

	void push_back_dlb_queue(DLB_queue **q)
	{
		queues_private.push_back(*q);
		*q = nullptr;
	}


	/****************************
	*DLB_queue methods
	****************************/
	DLB_queue::DLB_queue(bool cc, int l_p_id, int d_p_id, dlb_domain_hdl_t d_hdl) : combined_credits(cc), ldb_pool_id(l_p_id), dir_pool_id(d_p_id), domain_hdl( d_hdl)
	{
		printf("DLB_DEBUG: Start creating DLB queue\n");

		/* Create dir DLB queue */
		queue_id = dlb_create_dir_queue(domain_hdl, -1);
		if (queue_id == -1)
			error(1, errno, "dlb_create_dir_queue");

		/* create rx_port */
		rx_port = add_port(true);
		/* create tx_port */
		tx_port = add_port(false);
		elements_in_queue = 0;
		printf("DEBUG_DLB: DLB queue creation success\n");
	}

	DLB_queue::~DLB_queue()
	{
		/* Detach all ports */
		if(dlb_detach_port(rx_port) == -1)
			error(1, errno, "dlb_detach_port");
        	if(dlb_detach_port(tx_port) == -1)
                	error(1, errno, "dlb_detach_port");
	}

	dlb_port_hdl_t DLB_queue::add_port(bool is_queue_dep)
	{
		printf("DEBUG_DLB: Adding new port to queue\n");

		/* Prepare args for the port */
		dlb_create_port_t args;

		if (!combined_credits) {
			args.ldb_credit_pool_id = ldb_pool_id;
			args.dir_credit_pool_id = dir_pool_id;
		} else {
			args.credit_pool_id = ldb_pool_id;
		}

		args.cq_depth = CQ_DEPTH;

		/* Create port */
		int port_id;
		if(is_queue_dep)
			port_id = dlb_create_dir_port(domain_hdl, &args, queue_id);
		else
			port_id = dlb_create_dir_port(domain_hdl, &args, -1);
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

		// JSJS printf("Enqueue:%d(%d)  %d %p\n", port_tx,tx_port, queue_id, e);
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
			if(ret == -1 || ret == 0)
				printf("Problem with sending pocket\n");
			else
			{
				elements_in_queue += ret;
				break;
			}

		}

		return was_empty;
	}

	void
	DLB_queue::prepare_dequeue()
	{
		int ret = 0;
		last_nb_elem_rx = 0;

                ret = dlb_recv(rx_port, 128, false, &events_rx[0]);
                if(ret == -1 )
                        printf("Problem with receiving packets\n");
                else
                {
                        //if(events.recv.error)
                               // printf("DLB recive error\n");
			last_nb_elem_rx = ret;
			elements_in_queue -= ret;
                }
	}

	Event *
	DLB_queue::dequeue_external(int num)
	{
		return (Event *)events_rx[num].recv.udata64;
	}


	/**************************************************************
	*DLB device class
	*************************************************************/
	DLB_device::DLB_device()
	{
		/* Open DLB device */
		if (dlb_open(device_ID, &dlb_hdl) == -1)
			error(1, errno, "dlb_open");
		printf("DEBUG_DLB: DLB device sacesfully opened, ID = %d\n", device_ID);

		/* get DLB device capabilietes */
		if (dlb_get_dev_capabilities(dlb_hdl, &cap))
			error(1, errno, "dlb_get_dev_capabilities");

		/* Get DLB device resources information */
		if (dlb_get_num_resources(dlb_hdl, &rsrcs))
			error(1, errno, "dlb_get_num_resources");

        	/* Create scheduler for this queue */
        	domain_id = create_sched_domain();
        	if (domain_id == -1)
                	error(1, errno, "dlb_create_sched_domain");

        	domain = dlb_attach_sched_domain(dlb_hdl, domain_id);
        	if (domain == NULL)
                	error(1, errno, "dlb_attach_sched_domain");

		/* prepare resources for this sched domain */
		if (!cap.combined_credits)
		{
			int max_ldb_credits = rsrcs.num_ldb_credits * partial_resources / 100;
			int max_dir_credits = rsrcs.num_dir_credits * partial_resources / 100;

                	if (use_max_credit_ldb == true)
                        	ldb_pool_id = dlb_create_ldb_credit_pool(domain, max_ldb_credits);
                	else
                        	if (num_credit_ldb <= max_ldb_credits)
                                	ldb_pool_id = dlb_create_ldb_credit_pool(domain,
                                                                	num_credit_ldb);
                        	else
                                	error(1, EINVAL, "Requested ldb credits are unavailable!");

                	if (ldb_pool_id == -1)
                        	error(1, errno, "dlb_create_ldb_credit_pool");

                	if (use_max_credit_dir == true)
                        	dir_pool_id = dlb_create_dir_credit_pool(domain, max_dir_credits);
                	else
                        	if (num_credit_dir <= max_dir_credits)
                                	dir_pool_id = dlb_create_dir_credit_pool(domain,
                                                                	num_credit_dir);
                        	else
                                	error(1, EINVAL, "Requested dir credits are unavailable!");

                	if (dir_pool_id == -1)
                        	error(1, errno, "dlb_create_dir_credit_pool");
        	}else{
			int max_credits = rsrcs.num_credits * partial_resources / 100;

                	if (use_max_credit_combined == true)
                        	ldb_pool_id = dlb_create_credit_pool(domain, max_credits);
                	else
                        	if (num_credit_combined <= max_credits)
                                	ldb_pool_id = dlb_create_credit_pool(domain,
                                                        	num_credit_combined);
                        	else
                                	error(1, EINVAL, "Requested combined credits are unavailable!");

                	if (ldb_pool_id == -1)
                        	error(1, errno, "dlb_create_credit_pool");
        	}

		printf("DEBUG_DLB: Succesfully create DLB device class object\n");

		/* Create queues */
		for(uint32_t i = 0; i < rsrcs.num_ldb_queues; i++)
			queues_private.push_back(new DLB_queue(cap.combined_credits, ldb_pool_id, dir_pool_id, domain));

		/* start scheduler */
		start_sched();
	}

	DLB_device::DLB_device(int device_id)
	{
		device_ID = device_id;

		/* Open DLB device */
		if (dlb_open(device_ID, &dlb_hdl) == -1)
			error(1, errno, "dlb_open");
		printf("DEBUG_DLB: DLB device sacesfully opened, ID = %d\n", device_ID);

		/* get DLB device capabilietes */
		if (dlb_get_dev_capabilities(dlb_hdl, &cap))
			error(1, errno, "dlb_get_dev_capabilities");

		/* Get DLB device resources information */
		if (dlb_get_num_resources(dlb_hdl, &rsrcs))
			error(1, errno, "dlb_get_num_resources");

		printf("DEBUG_DLB: Succesfully create DLB device class object\n");
	}


	DLB_device::~DLB_device()
	{
		/* Delete queues */
		DLB_queue *q_ptr;
		while(queues_private.size())
		{
			q_ptr = queues_private.back();
			queues_private.pop_back();
			delete q_ptr;
		}

        	/* Detach and reset shceduler domain */
        	if (dlb_detach_sched_domain(domain) == -1)
                	error(1, errno, "dlb_detach_sched_domain");

        	if (dlb_reset_sched_domain(dlb_hdl, domain_id) == -1)
                	error(1, errno, "dlb_reset_sched_domain");

		/* Close the DLB device */
		if (dlb_close(dlb_hdl) == -1)
			error(1, errno, "dlb_close");
	}

	void DLB_device::start_sched()
	{
		if (dlb_launch_domain_alert_thread(domain, NULL, NULL))
			error(1, errno, "dlb_launch_domain_alert_thread");

		if (dlb_start_sched_domain(domain))
			error(1, errno, "dlb_start_sched_domain");
	}


	int DLB_device::create_sched_domain()
	{
    		int p_rsrsc = partial_resources;
    		dlb_create_sched_domain_t args;

    		args.num_ldb_queues = 0;
    		args.num_ldb_ports = 0;
    		args.num_dir_ports = 64;
    		args.num_ldb_event_state_entries = 0;
    		if (!cap.combined_credits) {
			args.num_ldb_credits = rsrcs.max_contiguous_ldb_credits * p_rsrsc / 100;
			args.num_dir_credits = rsrcs.max_contiguous_dir_credits * p_rsrsc / 100;
        		args.num_ldb_credit_pools = 1;
        		args.num_dir_credit_pools = 1;
    		} else {
        		args.num_credits = rsrcs.num_credits * p_rsrsc / 100;
        		args.num_credit_pools = 1;
    		}
    		args.num_sn_slots[0] = 0;
    		args.num_sn_slots[1] = 0;

    		return dlb_create_sched_domain(dlb_hdl, &args);
	}

void DLB_device::print_resources()
{
		printf("DLB's available resources:\n");
		printf("\tDomains:           %d\n", rsrcs.num_sched_domains);
		printf("\tLDB queues:        %d\n", rsrcs.num_ldb_queues);
		printf("\tLDB ports:         %d\n", rsrcs.num_ldb_ports);
		printf("\tDIR ports:         %d\n", rsrcs.num_dir_ports);
		printf("\tSN slots:          %d,%d\n", rsrcs.num_sn_slots[0],
			rsrcs.num_sn_slots[1]);
		printf("\tES entries:        %d\n", rsrcs.num_ldb_event_state_entries);
		printf("\tContig ES entries: %d\n",
			rsrcs.max_contiguous_ldb_event_state_entries);
		if (!cap.combined_credits) {
			printf("\tLDB credits:       %d\n", rsrcs.num_ldb_credits);
			printf("\tContig LDB cred:   %d\n", rsrcs.max_contiguous_ldb_credits);
			printf("\tDIR credits:       %d\n", rsrcs.num_dir_credits);
			printf("\tContig DIR cred:   %d\n", rsrcs.max_contiguous_dir_credits);
			printf("\tLDB credit pls:    %d\n", rsrcs.num_ldb_credit_pools);
			printf("\tDIR credit pls:    %d\n", rsrcs.num_dir_credit_pools);
		} else {
			printf("\tCredits:           %d\n", rsrcs.num_credits);
			printf("\tCredit pools:      %d\n", rsrcs.num_credit_pools);
		}
		printf("\n");
	}
}
