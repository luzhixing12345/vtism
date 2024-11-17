

#include "workqueue.h"

static void promote_work_handler(struct work_struct *w);

struct workqueue_struct *wq = NULL;
static DECLARE_WORK(promote_work, promote_work_handler);

static void promote_work_handler(struct work_struct *w)
{
// 	int i;
// 	int promotion_queued_out, err, promote_fail_nr;
// 	uint64_t head, next;
// 	unsigned int nr_retry_times, nr_succeeded = 0;
// 	LIST_HEAD(promote_pages);
// 	struct page *prom_node, *tmp;

// 	struct nomad_context stack_contxt;
// 	stack_contxt.transactional_migrate_success_nr = 0;
// 	stack_contxt.transactional_migrate_fail_nr = 0;

// 	promotion_queued_out =
// 		ring_queque_consume_begin(&context.promotion_queue,
// 					  PROMOTE_QUEUE_LEN, PROMOTE_QUEUE_LEN,
// 					  &head, &next, NULL, &context.q_lock);

// 	if (promotion_queued_out == 0) {
// 		goto out;
// 	}
// 	// isolate pages
// 	for (i = 0; i < promotion_queued_out; i++) {
// 		struct promote_task *promote_task =
// 			context.task_array + (head + i) % PROMOTE_QUEUE_LEN;
// 		BUG_ON(!PageAnon(promote_task->page));
// 		err = isolate_lru_page(promote_task->page);
// 		if (!err) {
// 			list_add(&promote_task->page->lru, &promote_pages);
// 			// if we successfuly isolated a page, we may safely drop caller's ref count
// 			put_page(promote_task->page);
// 		}
// 	}
// 	// anonymous pages for transition should only have two ref counts
// 	// TODO(lingfeng): right now we have the corret target nid written in, use that in the future
// 	promote_fail_nr =
// 		nomad_transit_pages(&promote_pages, alloc_promote_page,
// 					 NULL, 0, MIGRATE_ASYNC,
// 					 MR_NUMA_MISPLACED, &nr_succeeded,
// 					 &nr_retry_times, &stack_contxt);

// 	// sometimes we still may fail, these could either be failed to get lock
// 	// or it's not an anonymous page. The pages that are not moved are also
// 	// called successful ones, and are already handled within *_transit_pages()
// 	list_for_each_entry_safe (prom_node, tmp, &promote_pages, lru) {
// 		BUG_ON(prom_node->lru.next == LIST_POISON1 ||
// 		       prom_node->lru.prev == LIST_POISON2);
// 		list_del(&prom_node->lru);
// 		dec_node_page_state(prom_node,
// 				    NR_ISOLATED_ANON +
// 					    page_is_file_lru(prom_node));
// 		putback_lru_page(prom_node);
// 	}

// 	for (i = 0; i < promotion_queued_out; i++) {
// 		struct promote_task *promote_task =
// 			context.task_array + (head + i) % PROMOTE_QUEUE_LEN;
// 		// Page may be freed after migration, which means all the flags
// 		// will be cleared, it's normal that an enqueued page have no promote
// 		// flag when queued out
// 		TestClearPagePromQueued(promote_task->page);
// 	}

// 	// after copy and remap clear the prom bit

// 	ring_queque_consume_end(&context.promotion_queue, &head, &next,
// 				&context.q_lock);
// 	spin_lock(&context.info_lock);
// 	context.success_nr += nr_succeeded;
// 	context.retry_nr += nr_retry_times;
// 	context.transactional_success_nr +=
// 		stack_contxt.transactional_migrate_success_nr;
// 	context.transactional_fail_nr +=
// 		stack_contxt.transactional_migrate_fail_nr;
// 	if (promote_fail_nr > 0) {
// 		context.retreated_page_nr += promote_fail_nr;
// 	}
// 	context.try_to_promote_nr += promotion_queued_out;
// 	spin_unlock(&context.info_lock);
// out:
// 	return;
}
