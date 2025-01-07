

#include "pcm.h"

/*
-----------------------
        PCM
-----------------------
*/

// intel pcm program runing in background
static unsigned int pcm_interval = 1000;
// static char pcm_bandwidth_buf[4096];

static ssize_t pcm_interval_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) {
    return sysfs_emit(buf, "%u\n", pcm_interval);
}
static ssize_t pcm_interval_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count) {
    int ret;
    ret = kstrtouint(buf, 10, &pcm_interval);
    if (ret < 0)
        return ret;
    return count;
}

static struct kobj_attribute vtism_pcm_interval = __ATTR_RW(pcm_interval);

static ssize_t pcm_bandwidth_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) {
    return sysfs_emit(buf, "%u\n", pcm_interval);
}
static ssize_t pcm_bandwidth_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count) {
    int ret;
    ret = kstrtouint(buf, 10, &pcm_interval);
    if (ret < 0)
        return ret;
    return count;
}

static struct kobj_attribute vtism_pcm_bandwidth = __ATTR_RW(pcm_bandwidth);

static struct attribute *vtism_pcm_attrs[] = {
    &vtism_pcm_interval.attr,
    &vtism_pcm_bandwidth.attr,
    NULL,
};

static const struct attribute_group pcm_attr_group = {
    .attrs = vtism_pcm_attrs,
};

struct node_bandwidth {
    unsigned int read_bw;
    unsigned int write_bw;
    struct kobj_attribute read_bw_attr;
    struct kobj_attribute write_bw_attr;
};
static struct node_bandwidth *node_bw_data;

// 动态生成每个节点的读带宽显示
static ssize_t node_read_bw_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) {
    struct node_bandwidth *data = container_of(attr, struct node_bandwidth, read_bw_attr);
    return sysfs_emit(buf, "%u\n", data->read_bw);
}

// 动态生成每个节点的读带宽设置
static ssize_t node_read_bw_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count) {
    struct node_bandwidth *data = container_of(attr, struct node_bandwidth, read_bw_attr);
    int ret = kstrtouint(buf, 10, &data->read_bw);
    if (ret < 0)
        return ret;
    return count;
}

// 动态生成每个节点的写带宽显示
static ssize_t node_write_bw_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) {
    struct node_bandwidth *data = container_of(attr, struct node_bandwidth, write_bw_attr);
    return sysfs_emit(buf, "%u\n", data->write_bw);
}

// 动态生成每个节点的写带宽设置
static ssize_t node_write_bw_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count) {
    struct node_bandwidth *data = container_of(attr, struct node_bandwidth, write_bw_attr);
    int ret = kstrtouint(buf, 10, &data->write_bw);
    if (ret < 0)
        return ret;
    return count;
}

static int create_node_sysfs_files(struct kobject *parent_kobj) {
    int i, num_nodes = num_online_nodes();
    struct kobject *node_kobj;

    // 分配每个 NUMA 节点的带宽数据结构
    node_bw_data = kcalloc(num_nodes, sizeof(*node_bw_data), GFP_KERNEL);
    if (!node_bw_data)
        return -ENOMEM;

    for (i = 0; i < num_nodes; i++) {
        char node_name[32];
        snprintf(node_name, sizeof(node_name), "node%d", i);

        // 创建每个节点的 kobject
        node_kobj = kobject_create_and_add(node_name, parent_kobj);
        if (!node_kobj)
            return -ENOMEM;

        // 初始化带宽数据
        node_bw_data[i].read_bw = 0;
        node_bw_data[i].write_bw = 0;

        // 动态创建属性文件
        sysfs_attr_init(&node_bw_data[i].read_bw_attr.attr);
        node_bw_data[i].read_bw_attr.attr.name = "read_bw";
        node_bw_data[i].read_bw_attr.attr.mode = 0644;
        node_bw_data[i].read_bw_attr.show = node_read_bw_show;
        node_bw_data[i].read_bw_attr.store = node_read_bw_store;

        sysfs_attr_init(&node_bw_data[i].write_bw_attr.attr);
        node_bw_data[i].write_bw_attr.attr.name = "write_bw";
        node_bw_data[i].write_bw_attr.attr.mode = 0644;
        node_bw_data[i].write_bw_attr.show = node_write_bw_show;
        node_bw_data[i].write_bw_attr.store = node_write_bw_store;

        // 添加属性文件到节点
        if (sysfs_create_file(node_kobj, &node_bw_data[i].read_bw_attr.attr) ||
            sysfs_create_file(node_kobj, &node_bw_data[i].write_bw_attr.attr)) {
            kobject_put(node_kobj);
            return -ENOMEM;
        }
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