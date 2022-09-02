#ifndef DLB_ATS_H
#define DLB_ATS_H

#include <dlb.h>
#include <vector>
#include "P_EventSystem.h"


#define CQ_DEPTH 128

class DLB_queue
{
	int queue_id;
	std::vector<dlb_port_hdl_t> ports;

public:
	int get_queue_id() { return queue_id; }
	void print_ports();
	dlb_port_hdl_t add_port();
	DLB_queue();
	~DLB_queue();

	/* JSJS derrived fuctions */
//	void enqueue_local(Event *e); // Safe when called from the same thread
//	void enqueue(Event *e);
//	Event *dequeue_local();
  //	void dequeue_external();
};

class DLB_device
{
	dlb_hdl_t dlb_hdl;
	int ID;
	dlb_resources_t rsrcs;

	/* scheduler domain */
	int domain_id;

	std::vector< class DLB_queue> queues;

	int partial_resources = 100;
	int num_credit_combined;
	int num_credit_ldb;
	int num_credit_dir;
	bool use_max_credit_combined = true;
	bool use_max_credit_ldb = true;
	bool use_max_credit_dir = true;

	/* methods */
	int create_sched_domain();

public:

	void print_resources();
	void start_sched();
	DLB_device();
	~DLB_device();
};


#endif
