#ifndef DLB_ATS_H
#define DLB_ATS_H

#include <dlb.h>
#include <vector>
#include "I_EventSystem.h"

/* DLB queue class */
class DLB_queue : public ProtectedQueue
{
	int queue_id;
	std::vector<dlb_port_hdl_t> ports;

	/* port for reading */
	dlb_port_hdl_t rx_port;
	/* port for writing */
	dlb_port_hdl_t tx_port;

	/* schedular domain id */
	int domain_id;
	/* schedulare domain handler */
	dlb_domain_hdl_t domain;
	int ldb_pool_id;
	int dir_pool_id;
	int  create_sched_domain();
	void start_sched();
public:
	int get_queue_id() { return queue_id; }
	void print_ports();
	dlb_port_hdl_t add_port(int);
	DLB_queue();
	~DLB_queue();

	void enqueue(Event *e);
	void enqueue_local(Event *e); // Safe when called from the same thread
	void remove(Event *e) {}
	Event *dequeue_local();
	void dequeue_external();       // Dequeue any external events.

};

/* DLB device */
class DLB_device
{
	int ID;

	std::vector< class DLB_queue> queues;

public:

	void print_resources();
	DLB_device();
	~DLB_device();
};


#endif
