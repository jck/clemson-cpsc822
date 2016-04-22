/*
 * elevator noop
 */
#include <linux/blkdev.h>
#include <linux/elevator.h>
#include <linux/bio.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>

struct sstf_data {
	// 0 is low_queue, 1 is high_queue
	struct list_head queues[2];
	sector_t pos;
};

static void sstf_merged_requests(struct request_queue *q, struct request *rq,
				 struct request *next)
{
	list_del_init(&next->queuelist);
}

#define distance()

static int sstf_dispatch(struct request_queue *q, int force)
{
	struct sstf_data *sd = q->elevator->elevator_data;
	int i, dist, best_dist;
	struct request *rq, *best_rq;


	best_rq = NULL;
	best_dist = -1;
	for (i=0; i<2; i++) {
		rq = list_first_entry_or_null(&sd->queues[i], struct request, queuelist);
		if (rq == NULL)
			continue;

		dist = abs(sd->pos - blk_rq_pos(rq));
		if (best_rq == NULL || dist < best_dist) {
			best_dist = dist;
			best_rq = rq;

		}
	}

	if (best_rq == NULL)
		return 0;

	sd->pos = rq_end_sector(best_rq);
	list_del_init(&best_rq->queuelist);
	elv_dispatch_sort(q, best_rq);
	return 1;
}


static void sstf_add_request(struct request_queue *q, struct request *rq)
{
	struct sstf_data *sd = q->elevator->elevator_data;

	// 0 = low_queue, 1 = high_queue
	int choose = blk_rq_pos(rq) > sd->pos;
	struct list_head *queue = &sd->queues[choose];
	struct request *r;

	list_for_each_entry(r, queue, queuelist) {
		if ((blk_rq_pos(r) == blk_rq_pos(rq))
		    || ((blk_rq_pos(r) > blk_rq_pos(rq)) == choose)) {
			list_add(&rq->queuelist, &r->queuelist);
			return;
		}
	}

	// List is either empty or this element needs to be in the end.
	list_add_tail(&rq->queuelist, queue);
}

static struct request *
sstf_former_request(struct request_queue *q, struct request *rq)
{
	// struct sstf_data *sd = q->elevator->elevator_data;

	return NULL;
	// if (rq->queuelist.prev == &nd->queue)
	// 	return NULL;

	// return list_entry(rq->queuelist.prev, struct request, queuelist);
}

static struct request *
sstf_latter_request(struct request_queue *q, struct request *rq)
{
	// struct sstf_data *sd = q->elevator->elevator_data;

	return NULL;
	// if (rq->queuelist.next == &sd->queue)
	// 	return NULL;
	// return list_entry(rq->queuelist.next, struct request, queuelist);
}

static int sstf_init_queues(struct request_queue *q, struct elevator_type *e)
{
	struct sstf_data *sd;
	struct elevator_queue *eq;

	eq = elevator_alloc(q, e);
	if (!eq)
		return -ENOMEM;

	sd = kmalloc_node(sizeof(*sd), GFP_KERNEL, q->node);
	if (!sd) {
		kobject_put(&eq->kobj);
		return -ENOMEM;
	}
	eq->elevator_data = sd;

	INIT_LIST_HEAD(&sd->queues[0]);
	INIT_LIST_HEAD(&sd->queues[1]);

	sd->pos = 0;

	spin_lock_irq(q->queue_lock);
	q->elevator = eq;
	spin_unlock_irq(q->queue_lock);
	return 0;
}

static void sstf_exit_queues(struct elevator_queue *e)
{
	struct sstf_data *sd = e->elevator_data;

	BUG_ON(!list_empty(&sd->queues[0]));
	BUG_ON(!list_empty(&sd->queues[1]));
	kfree(sd);
}

static struct elevator_type elevator_sstf = {
	.ops = {
		.elevator_merge_req_fn		= sstf_merged_requests,
		.elevator_dispatch_fn		= sstf_dispatch,
		.elevator_add_req_fn		= sstf_add_request,
		.elevator_former_req_fn		= sstf_former_request,
		.elevator_latter_req_fn		= sstf_latter_request,
		.elevator_init_fn		= sstf_init_queues,
		.elevator_exit_fn		= sstf_exit_queues,
	},
	.elevator_name = "sstf",
	.elevator_owner = THIS_MODULE,
};

static int __init sstf_init(void)
{
	return elv_register(&elevator_sstf);
}

static void __exit sstf_exit(void)
{
	elv_unregister(&elevator_sstf);
}

module_init(sstf_init);
module_exit(sstf_exit);


MODULE_AUTHOR("Keerthan Jaic");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Shortest seek time first IO scheduler");
