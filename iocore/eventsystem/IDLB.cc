#include "stdio.h"
#include <error.h>
#include "P_EventSystem.h"

/* private variables */
static dlb_hdl_t dlb_hdl;
static int partial_resources = 100;
static int num_credit_combined;
static int num_credit_ldb = 128;
static int num_credit_dir = 128;
static bool use_max_credit_combined = false;
static bool use_max_credit_ldb = false;
static bool use_max_credit_dir = false;

static dlb_resources_t rsrcs;
static dlb_dev_cap_t cap;
static int queue_ctr_dg = 0;

/* DLB private variables */
static int queue_ctr;
static bool is_dlb_init = false; //JSJS check it

#define CQ_DEPTH 32

enum wait_mode_t{
	POOL,
	INTERRUPT,
}wait_mode=INTERRUPT;

/************************************************
*DLB init
***********************************************/
void dlb_device_init()
{

}

/***********************************************
*DLB deinit
***********************************************/
void dlb_device_clean()
{
	/* When the queue number equal 0 don't need dlb */
	if(!queue_ctr)
	{
		/* Clean dlb device */
	}
}



DLB_queue::DLB_queue()
{
	queue_prog_id = queue_ctr_dg++;
	printf("DLB_DEBUG: Start creating DLB queue\n");
	/* Create scheduler for this queue */
	domain_id = create_sched_domain();
	if (domain_id == -1)
		error(1, errno, "dlb_create_sched_domain");
//	printf("DEBUG_DLB: Succesfully create scheduler domain\n");

	domain = dlb_attach_sched_domain(dlb_hdl, domain_id);
	if (domain == NULL)
		error(1, errno, "dlb_attach_sched_domain");

	if (!cap.combined_credits)
	{
		int max_ldb_credits = 128; //rsrcs.num_ldb_credits * partial_resources / 100;
		int max_dir_credits = 128; //rsrcs.num_dir_credits * partial_resources / 100;

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
		int max_credits = 128; //rsrcs.num_credits * partial_resources / 100;

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

	/* Create dir DLB queue */
//	printf("DEBUG_DLB: Create DLB queue\n");
	queue_id = dlb_create_dir_queue(domain, -1);
	if (queue_id == -1)
		error(1, errno, "dlb_create_dir_queue");

	/* create rx_port */
	rx_port = add_port(0);
	/* create tx_port */
	tx_port = add_port(1);

	start_sched();
	printf("DEBUG_DLB: DLB queue creation success\n");
}

DLB_queue::~DLB_queue()
{
	/* Detach all ports */
	for(dlb_port_hdl_t port : ports)
		if(dlb_detach_port(port) == -1)
			error(1, errno, "dlb_detach_port");

	/* Detach and reset shceduler domain */
	if (dlb_detach_sched_domain(domain) == -1)
		error(1, errno, "dlb_detach_sched_domain");

	if (dlb_reset_sched_domain(dlb_hdl, domain_id) == -1)
		error(1, errno, "dlb_reset_sched_domain");
}

dlb_port_hdl_t DLB_queue::add_port(int dir)
{
//	printf("DEBUG_DLB: Adding new port to queue\n");

	/* Prepare args for the port */
	dlb_create_port_t args;

	if (!cap.combined_credits) {
		args.ldb_credit_pool_id = ldb_pool_id;
		args.dir_credit_pool_id = dir_pool_id;
	} else {
		args.credit_pool_id = ldb_pool_id;
	}

	args.cq_depth = CQ_DEPTH;

	/* Create port */
	int port_id;
	if(dir)
		port_id = dlb_create_dir_port(domain, &args, -1);
	else
		port_id = dlb_create_dir_port(domain, &args, queue_id);
	if (port_id == -1)
		error(1, errno, "dlb_create_dir_port");

	dlb_port_hdl_t port = dlb_attach_dir_port(domain, port_id);
	if (port == NULL)
		error(1, errno, "dlb_attach_dir_port");
	else
		ports.push_back(port);

	return port;
}

void
DLB_queue::print_ports()
{
	printf("DLB_DEBUG: Printing all ports which this queue contains:\n");
}

void
DLB_queue::enqueue(Event *e)
{
	dlb_event_t event;

	/* Initialize the static fields in the send event */
	event.send.flow_id = 0;
	event.send.queue_id = queue_id;
	event.send.sched_type = SCHED_DIRECTED;
	event.send.priority = 0;
	/* Initialize the dynamic fields in the send event */
	event.adv_send.udata64 = (uint64_t)e;

	/* Send the event */
	auto ret = 0;
	for(int i = 0; i < 10; i++)
	{
		ret = dlb_send(tx_port, 1, &event);
		if(ret == -1)
			printf("Problem with sending pocket\n");
		else
			break;
	}
//	printf("DLB_DEBUG: queue: %d\t enq_ext %p\n", queue_prog_id, e);
}

Event *
DLB_queue::dequeue_local()
{
	return NULL;
}

Event *
DLB_queue::dequeue_external()
{
	dlb_event_t events;
	Event *e = nullptr;
	int ret = 0;

	ret = dlb_recv(rx_port, 1, false /*(wait_mode == INTERRUPT)*/, &events);
	if(ret == -1 )
		printf("Problem with receiving packets\n");
	else if(ret > 0)
	{
		if(events.recv.error)
			printf("DLB recive error\n");
		e = (Event *)events.recv.udata64;
	}

	//printf("DLB_DEBUG: queue: %d\t deq_ext %p\t", queue_prog_id, e);
	return e;
}

void
DLB_queue::enqueue_local(Event *e)
{
}

/**************************************************************
*DLB device class
*************************************************************/
DLB_device::DLB_device()
{
	printf("DEBUG_DLB: Hi I am DLB_device constructor\n");

	if(!is_dlb_init)
	{
		/* Open DLB device */
		if (dlb_open(ID, &dlb_hdl) == -1)
			error(1, errno, "dlb_open");
		printf("DEBUG_DLB: DLB device sacesfully opened, ID = %d\n", ID);

		/* get DLB device capabilietes */
		if (dlb_get_dev_capabilities(dlb_hdl, &cap))
			error(1, errno, "dlb_get_dev_capabilities");
		printf("DEBUG_DLB: Succesfully get DLB capabilietes\n");

		/* Get DLB device resources information */
		if (dlb_get_num_resources(dlb_hdl, &rsrcs))
			error(1, errno, "dlb_get_num_resources");
		printf("DEBUG_DLB: Succesfully get DLB resources info\n");

		printf("DEBUG_DLB: Succesfully create DLB device class object\n");
		is_dlb_init = true;
		this->dlb_handler = dlb_hdl;
	}
	else
	{

                 /* Open DLB device */
                 if (dlb_open(ID, &dlb_handler) == -1)
                         error(1, errno, "dlb_open");
                 printf("DEBUG_DLB: DLB device sacesfully opened, ID = %d\n", ID);

                 /* get DLB device capabilietes */
                 if (dlb_get_dev_capabilities(dlb_handler, &cap))
                         error(1, errno, "dlb_get_dev_capabilities");
                 printf("DEBUG_DLB: Succesfully get DLB capabilietes\n");

                 /* Get DLB device resources information */

                 if (dlb_get_num_resources(dlb_handler, &rsrcs))
			error(1, errno, "dlb_get_num_resources");
                 printf("DEBUG_DLB: Succesfully get DLB resources info\n");

                 printf("DEBUG_DLB: Succesfully create DLB device class object\n");
//		printf("Cannot create two DLB devices\n");
//		exit(1);
	}
}

DLB_device::DLB_device(int device_id)
{
         printf("DEBUG_DLB: Hi I am DLB_device constructor\n");
		ID = device_id;
                 /* Open DLB device */
                 if (dlb_open(ID, &dlb_handler) == -1)
                         error(1, errno, "dlb_open");
                 printf("DEBUG_DLB: DLB device sacesfully opened, ID = %d\n", ID);

                 /* get DLB device capabilietes */
                 if (dlb_get_dev_capabilities(dlb_handler, &cap))
                         error(1, errno, "dlb_get_dev_capabilities");
                 printf("DEBUG_DLB: Succesfully get DLB capabilietes\n");

                 /* Get DLB device resources information */
                 if (dlb_get_num_resources(dlb_handler, &rsrcs))
                         error(1, errno, "dlb_get_num_resources");
                 printf("DEBUG_DLB: Succesfully get DLB resources info\n");

                 printf("DEBUG_DLB: Succesfully create DLB device class object\n");
}


DLB_device::~DLB_device()
{
        printf("DEBUG_DLB: DEBUG: Hi I am DLB_device destructor\n");

	/* Close the DLB device */
	if (dlb_close(this->dlb_handler) == -1)
		error(1, errno, "dlb_close");

//	is_dlb_init = false;
}

void DLB_queue::start_sched()
{
	if (dlb_launch_domain_alert_thread(domain, NULL, NULL))
		error(1, errno, "dlb_launch_domain_alert_thread");

	if (dlb_start_sched_domain(domain))
		error(1, errno, "dlb_start_sched_domain");
}


int DLB_queue::create_sched_domain()
{
    int p_rsrsc = partial_resources;
    dlb_create_sched_domain_t args;

    args.num_ldb_queues = 0;
    args.num_ldb_ports = 0;
    args.num_dir_ports = 2; // for now all 64 ports, change it later
    args.num_ldb_event_state_entries = 0;
    if (!cap.combined_credits) {
        args.num_ldb_credits = 128; //rsrcs.max_contiguous_ldb_credits * p_rsrsc / 100;
        args.num_dir_credits = 128; //rsrcs.max_contiguous_dir_credits * p_rsrsc / 100;
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
