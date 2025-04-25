
# vtism

source code for vtism: Toward Efficient Tiered Memory Management for Virtual Machines with CXL

this repo contains the main source code of vtism kernel(modified from v6.6), and all the benchmark code, result log, draw python scripts and test programs are in [vtism-workspace]() repository, if you want to reproduce our work please follow the instruction from that repo

## Prerequisites

### Software requirements

download the following package from your linux distribution, we recommand to use Ubuntu22.04 LTS

```bash
sudo apt update
sudo apt-get install git fakeroot build-essential ncurses-dev xz-utils libssl-dev bc flex libelf-dev bison vim
```

### Hardware requirements

your system should has at least 1 NUMA node and 1 CXL node

our system have 2 NUMA node and 2 CXL node, the CXL devices are connected to Node 1, which has a closer NUMA topology to Node 1 than to Node 0.

```bash
available: 4 nodes (0-3)
node 0 cpus: 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 96 97 98 99 100 101 102 103 104 105 106 107 108 109 110 111 112 113 114 115 116 117 118 119 120 121 122 123 124 125 126 127 128 129 130 131 132 133 134 135 136 137 138 139 140 141 142 143
node 0 size: 64058 MB
node 0 free: 59658 MB
node 1 cpus: 48 49 50 51 52 53 54 55 56 57 58 59 60 61 62 63 64 65 66 67 68 69 70 71 72 73 74 75 76 77 78 79 80 81 82 83 84 85 86 87 88 89 90 91 92 93 94 95 144 145 146 147 148 149 150 151 152 153 154 155 156 157 158 159 160 161 162 163 164 165 166 167 168 169 170 171 172 173 174 175 176 177 178 179 180 181 182 183 184 185 186 187 188 189 190 191
node 1 size: 64488 MB
node 1 free: 54831 MB
node 2 cpus:
node 2 size: 64511 MB
node 2 free: 64265 MB
node 3 cpus:
node 3 size: 64470 MB
node 3 free: 64222 MB
node distances:
node   0   1   2   3
  0:  10  21  24  24
  1:  21  10  14  14
  2:  24  14  10  16
  3:  24  14  16  10
```

### Setting up CXL memory

Add the following kernel parameter to utilize CXL.mem device in file `/etc/default/grub`

```bash
GRUB_CMDLINE_LINUX_DEFAULT="efi=nosoftreserve"
```

Then update the grub by executing:

```bash
sudo update-grub2
```

## Generate kernel config

we provide our .config, which works well on Ubuntu22.04, you can run the following code to use our .config

```bash
cp vtism.config .config
```

---

if you want to use your own .config, enable kvm and numa and MGLRU

```bash
CONFIG_LRU_GEN=y
CONFIG_LRU_GEN_ENABLED=y

CONFIG_KVM*=y
```

enable vtism

```bash
CONFIG_VTISM=y
CONFIG_VTISM_MODULE_SYM_EXPORT=y
```

you can choose to enable debug info by

```bash
CONFIG_VTISM_DEBUG=y
```

## Build and install kernel

compile the kernel and generate kernel-deb

```bash
make -j`nproc` bindeb-pkg
```

install deb from parent dir

```bash
sudo dpkg -i linux-headers-6.6.0vtism+_6.6.0-g37ba66071375-221_amd64.deb
sudo dpkg -i linux-image-6.6.0vtism+_6.6.0-g37ba66071375-221_amd64.deb
```

use new kernel `vtism` to reboot(you can your our switch_kernel.sh script from vtism-workspace)

## About vtism

vtism register three kernel interface files under `/sys/kernel/vtism`

```bash
$ ls /sys/kernel/mm/vtism/
demotion_min_free_ratio dump  enable  migration_enable
```

`dump` is used to show kernel info like

```bash
$ cat /sys/kernel/mm/vtism/dump
[demotion pretarget]
node 0 demotion target: 1 2 3
node 1 demotion target: 0 2 3
node 2 has no demotion target(cxl node)
node 3 has no demotion target(cxl node)

[vm info]
not enable vtism qemu info, please run qemu first and enable vtism
use "echo 1 > /sys/kernel/mm/vtism/enable" to get qemu info

[node mem info]
node 0: total = 64058 MB(62 GB), free = 59675 MB(58 GB)
node 1: total = 64488 MB(62 GB), free = 54813 MB(53 GB)
node 2: total = 64511 MB(62 GB), free = 64265 MB(62 GB)
node 3: total = 64470 MB(62 GB), free = 64222 MB(62 GB)

[page classify info]
not start page classify thread

[page migration info]
migration disable
```

---

`enable` is used to enable vtism page classification, **you must start a qemu vm first**, and then enable it, or it will fail

```bash
# run qemu vm
# enable vtism classification
$ echo 1 > /sys/kernel/mm/vtism/enable
$ echo /sys/kernel/mm/vtism/enable
1 # success
```

```bash
# directly enable vtism classification
$ echo 1 > /sys/kernel/mm/vtism/enable
$ echo /sys/kernel/mm/vtism/enable
0 # fail
```

```bash
# disable vtism classfication
$ echo 0 > /sys/kernel/mm/vtism/enable
```

---

`migration_enable` is used to enable async migration workqueue, you can enable or disable it by echo 1/0

```bash
# enable async migration workqueue
$ echo 1 > /sys/kernel/mm/vtism/migration_enable

# disable async migration workqueue
$ echo 1 > /sys/kernel/mm/vtism/migration_enable
```

---

`demotion_min_free_ratio`(default 5) means demote target node should have more than demotion_min_free_ratio% free memory space

## How to use vtism

```bash
# start a qemu vm
echo 1 > /sys/kernel/mm/vtism/enable
echo 1 > /sys/kernel/mm/vtism/migration_enable
```

that's all