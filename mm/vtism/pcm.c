

#include "pcm.h"

#include "linux/kobject.h"
#include "linux/memory-tiers.h"

/*
-----------------------
        PCM
-----------------------
*/

static struct node_info *node_info_data;
extern bool vtism_enable;
extern int demotion_min_free_ratio;
extern int promotion_min_free_ratio;

int find_best_demotion_node(int node, const nodemask_t *maskp) {
    if (!vtism_enable) {
        return node_random(maskp);
    }
    int target_node;
    int best_node = -1;
    int best_score = INT_MAX;
    pr_info("find best demotion node for node %d, mask %p", node, maskp);
    /* 遍历 mask 中的每个候选节点 */
    for_each_node_mask(target_node, *maskp) {
        /* 排除自己 */
        if (target_node == node)
            continue;

        struct node_info *info = &node_info_data[target_node];
        int effective_latency = info->is_toptier ? info->latency : info->to_cxl_latency;
        int effective_bw = (info->read_bw + info->write_bw) / 2;
        int free_ratio = 100 * info->free_mem_size / info->total_mem_size;
        if (free_ratio < demotion_min_free_ratio) {
            continue;
        }
        int score = effective_latency + effective_bw;
        pr_info("Node %d: latency=%d, bw=%d, free_ratio=%d%%, score=%d\n",
                target_node,
                effective_latency,
                effective_bw,
                free_ratio,
                score);
        if (score < best_score) {
            best_score = score;
            best_node = target_node;
        }
    }
    if (best_score == 0) {
        // node_info_data is not initialized yet
        return node_random(maskp);
    }
    pr_info("node %d best demotion node: %d\n", node, best_node);
    return best_node;
}

bool should_migrate_to_target_node(int page_nid, int target_nid) {
    if (!vtism_enable) {
        return false;
    }
    struct node_info *target_node_info = &node_info_data[target_nid];
    // if target node doesn't have enough free memory, don't migrate
    if (100 * target_node_info->free_mem_size / target_node_info->total_mem_size <
        promotion_min_free_ratio) {
        return false;
    }

    int target_score =
        target_node_info->latency + (target_node_info->read_bw + target_node_info->write_bw) / 2;
    int page_score = node_info_data[page_nid].latency +
                     (node_info_data[page_nid].read_bw + node_info_data[page_nid].write_bw) / 2;
    // if current node's latency and bandwidth is better than target node, don't migrate
    if (target_score < page_score) {
        return false;
    }
    return true;
}

// 动态生成每个节点的读带宽显示
static ssize_t node_read_bw_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) {
    struct node_info *data = container_of(attr, struct node_info, read_bw_attr);
    return sysfs_emit(buf, "%u (MB/s)\n", data->read_bw);
}

// 动态生成每个节点的读带宽设置
static ssize_t node_read_bw_store(struct kobject *kobj, struct kobj_attribute *attr,
                                  const char *buf, size_t count) {
    struct node_info *data = container_of(attr, struct node_info, read_bw_attr);
    int ret = kstrtouint(buf, 10, &data->read_bw);
    if (ret < 0)
        return ret;
    return count;
}

// 动态生成每个节点的写带宽显示
static ssize_t node_write_bw_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) {
    struct node_info *data = container_of(attr, struct node_info, write_bw_attr);
    return sysfs_emit(buf, "%u (MB/s)\n", data->write_bw);
}

// 动态生成每个节点的写带宽设置
static ssize_t node_write_bw_store(struct kobject *kobj, struct kobj_attribute *attr,
                                   const char *buf, size_t count) {
    struct node_info *data = container_of(attr, struct node_info, write_bw_attr);
    int ret = kstrtouint(buf, 10, &data->write_bw);
    if (ret < 0)
        return ret;
    return count;
}

// 动态生成每个节点的延迟显示
static ssize_t node_latency_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) {
    struct node_info *data = container_of(attr, struct node_info, latency_attr);
    return sysfs_emit(buf, "%u (ns)\n", data->latency);
}

static ssize_t node_latency_store(struct kobject *kobj, struct kobj_attribute *attr,
                                  const char *buf, size_t count) {
    struct node_info *data = container_of(attr, struct node_info, latency_attr);
    int ret = kstrtouint(buf, 10, &data->latency);
    if (ret < 0)
        return ret;
    return count;
}

static ssize_t node_to_cxl_latency_show(struct kobject *kobj, struct kobj_attribute *attr,
                                        char *buf) {
    struct node_info *data = container_of(attr, struct node_info, to_cxl_latency_attr);
    return sysfs_emit(buf, "%u (ns)\n", data->to_cxl_latency);
}

static ssize_t node_to_cxl_latency_store(struct kobject *kobj, struct kobj_attribute *attr,
                                         const char *buf, size_t count) {
    struct node_info *data = container_of(attr, struct node_info, to_cxl_latency_attr);
    int ret = kstrtouint(buf, 10, &data->to_cxl_latency);
    if (ret < 0)
        return ret;
    return count;
}

ssize_t dump_node_bw_lat_info(char *buf, ssize_t len) {
    int i;
    len += sysfs_emit_at(buf, len, "[node bw latency info]\n");
    // 设置列宽并打印表头
    len += sysfs_emit_at(buf,
                         len,
                         "%6s %15s %15s %12s %14s\n",
                         "node",
                         "read_bw(MB/s)",
                         "write_bw(MB/s)",
                         "latency(ns)",
                         "to_cxl_latency(ns)");
    for (i = 0; i < num_online_nodes(); i++) {
        struct node_info *data = &node_info_data[i];
        // 格式化输出,每列的宽度与表头保持一致,右对齐
        len += sysfs_emit_at(buf, len, "%6d %15u %15u", i, data->read_bw, data->write_bw);
        if (data->is_toptier) {
            len += sysfs_emit_at(buf, len, "%12u %14u", data->latency, data->to_cxl_latency);
        } else {
            // use - to indicate not available
            len += sysfs_emit_at(buf, len, "%12s %14s", "-", "-");
        }
        len += sysfs_emit_at(buf, len, "\n");
    }
    len += sysfs_emit_at(buf, len, "\n");
    return len;
}

static int create_node_sysfs_files(struct kobject *parent_kobj) {
    int i, num_nodes = num_online_nodes();
    struct kobject *node_kobj;

    // 分配每个 NUMA 节点的带宽数据结构
    node_info_data = kcalloc(num_nodes, sizeof(*node_info_data), GFP_KERNEL);
    if (!node_info_data)
        return -ENOMEM;

    for (i = 0; i < num_nodes; i++) {
        char node_name[32];
        snprintf(node_name, sizeof(node_name), "node%d", i);

        // 创建每个节点的 kobject
        node_kobj = kobject_create_and_add(node_name, parent_kobj);
        if (!node_kobj)
            return -ENOMEM;

        // 初始化带宽数据
        node_info_data[i].read_bw = 0;
        node_info_data[i].write_bw = 0;

        // 动态创建属性文件
        sysfs_attr_init(&node_bw_data[i].read_bw_attr.attr);
        node_info_data[i].read_bw_attr.attr.name = "read_bw";
        node_info_data[i].read_bw_attr.attr.mode = 0666;
        node_info_data[i].read_bw_attr.show = node_read_bw_show;
        node_info_data[i].read_bw_attr.store = node_read_bw_store;

        sysfs_attr_init(&node_bw_data[i].write_bw_attr.attr);
        node_info_data[i].write_bw_attr.attr.name = "write_bw";
        node_info_data[i].write_bw_attr.attr.mode = 0666;
        node_info_data[i].write_bw_attr.show = node_write_bw_show;
        node_info_data[i].write_bw_attr.store = node_write_bw_store;

        // 添加属性文件到节点
        if (sysfs_create_file(node_kobj, &node_info_data[i].read_bw_attr.attr) ||
            sysfs_create_file(node_kobj, &node_info_data[i].write_bw_attr.attr)) {
            kobject_put(node_kobj);
            return -ENOMEM;
        }

        // only toptier node has latency and to_cxl_latency attr
        if (node_is_toptier(i)) {
            node_info_data[i].is_toptier = true;
            sysfs_attr_init(&node_bw_data[i].latency_attr.attr);
            node_info_data[i].latency_attr.attr.name = "latency";
            node_info_data[i].latency_attr.attr.mode = 0666;
            node_info_data[i].latency_attr.show = node_latency_show;
            node_info_data[i].latency_attr.store = node_latency_store;

            sysfs_attr_init(&node_bw_data[i].to_cxl_latency_attr.attr);
            node_info_data[i].to_cxl_latency_attr.attr.name = "to_cxl_latency";
            node_info_data[i].to_cxl_latency_attr.attr.mode = 0666;
            node_info_data[i].to_cxl_latency_attr.show = node_to_cxl_latency_show;
            node_info_data[i].to_cxl_latency_attr.store = node_to_cxl_latency_store;

            if (sysfs_create_file(node_kobj, &node_info_data[i].latency_attr.attr) ||
                sysfs_create_file(node_kobj, &node_info_data[i].to_cxl_latency_attr.attr)) {
                kobject_put(node_kobj);
                return -ENOMEM;
            }
        }

        node_info_data[i].node_kobj = node_kobj;
    }

    return 0;
}

int register_pcm_sysctl(struct kobject *vtism_kobj) {
    struct kobject *pcm;
    int err;
    pcm = kobject_create_and_add("pcm", vtism_kobj);
    err = create_node_sysfs_files(pcm);
    if (err) {
        kobject_put(pcm);
    }
    return err;
}

void unregister_pcm_sysctl(void) {
    if (node_info_data) {
        kfree(node_info_data);
        node_info_data = NULL;
    }
}