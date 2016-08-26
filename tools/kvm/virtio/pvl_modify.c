#include "kvm/virtio-pvl.h"
#include "kvm/virtio-pci-dev.h"

#include "kvm/virtio.h"
#include "kvm/util.h"
#include "kvm/kvm.h"
#include "kvm/pci.h"
#include "kvm/threadpool.h"
#include "kvm/guest_compat.h"
#include "kvm/kvm-ipc.h"

#include "kvm/atomic.h"

#include <sys/socket.h>
#include <linux/virtio_ring.h>
#include <linux/virtio_pvl.h>

#include <linux/kernel.h>
#include <linux/list.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <pthread.h>
#include <sys/eventfd.h>

#include <stdio.h>
#include <string.h>
#include <signal.h>

#include <kvm/util.h>
#include <kvm/kvm.h>
#include <kvm/kvm-ipc.h>

#include<stdlib.h>
#include<sys/shm.h>                //공유메모리 함수의 라이브러리

#define NUM_VIRT_QUEUES		1
#define VIRTIO_PVL_QUEUE_SIZE	128
#define VIRTIO_PVL_COMMAND	0

#define QUEUE_SIZE 100

struct shared_use_memory{ 
  unsigned int id;
  signed int prediction_delta;
  signed int reclaim_delta;
  const char *instance_name;
};

struct pvl_command_t {
  unsigned short undone;
  unsigned long args[4];
  int result; 
};

struct pvl_queue_t {
  int head; 
  int tail;
  struct pvl_command_t pages[QUEUE_SIZE];
};

struct pvl_dev {
  struct list_head	list;
  struct virtio_device	vdev;

  u32 features;

  /* virtio queue */
  struct virt_queue	vqs[NUM_VIRT_QUEUES];
  struct pvl_queue_t *rq;
  struct virtio_pvl_config config;
};

static struct pvl_dev pdev;
static int compat_id = -1;

//공유 변수 
struct shared_use_memory* shared_stuff;

static int pvl_dequeue(struct pvl_queue_t *q, struct pvl_command_t **command) 
{
  int index;
  int next;

  do {
    if (q->head == q->tail)
      return -1;

    index = q->head;
    next = (index + 1) % QUEUE_SIZE;
    if (q->pages[index].undone == 0)
      return -1;

  } while (atomic_cmpxchg((void*)&q->head, index, next) != index);
  *command = &q->pages[index];

  return 0;
}

pthread_t iocore;

static void* iocore_main(void *param) {
  cpu_set_t mask;
  struct pvl_command_t *cmd; 
  // 메모리 사용정보 예측 변수 
  signed int start_delta = 0;
  signed int past_delta = 0;
  signed int current_delta = 0;
  //재분배를 위한 socket and rebalancing variable
  signed int instance;

  signed int request_amount_memory = 0;
  int total_ram = 0;
  CPU_ZERO(&mask);
  CPU_SET(0, &mask);
  sched_setaffinity(0, sizeof(mask), &mask);

  while(1) {
    if(pvl_dequeue(pdev.rq, &cmd)) {
      __asm__ __volatile__ ("rep; nop":::"memory");
    } else {

      current_delta = cmd->args[0] - start_delta;
      start_delta = cmd->args[0];
      shared_stuff->prediction_delta = current_delta * 0.75 + past_delta * 0.25 + cmd->args[2];
      past_delta = shared_stuff->prediction_delta;
      shared_stuff->reclaim_delta = cmd->args[1];
      total_ram = cmd->args[3];

      if(shared_stuff->prediction_delta > (shared_stuff->reclaim_delta-(total_ram/10))){

        printf("delta : %d\n", shared_stuff->prediction_delta);

        request_amount_memory = -shared_stuff->prediction_delta;
        instance = kvm__get_sock_by_instance(shared_stuff->instance_name);
        kvm_ipc__send_msg(instance, KVM_IPC_BALLOON, 4, (u8 *)&request_amount_memory);
        close(instance);

        shared_stuff->prediction_delta = 0;
        past_delta = 0;
      }
      cmd->undone = 0;
      __asm__ __volatile__ ("sfence":::"memory");
    }
  }
  return NULL;
}

static bool virtio_pvl_do_io_request(struct kvm *kvm, struct pvl_dev *pdev, u32 qaddr)
{
  int shmid;
  void* shared_memory=(void*)0;
  shmid = shmget((key_t)1113,sizeof(struct shared_use_memory),0666 | IPC_CREAT);
  shared_memory = shmat(shmid,(void*)0,0); 
  shared_stuff = (struct shared_use_memory *)shared_memory;

  pdev->rq = guest_pfn_to_host(kvm, qaddr);
  shared_stuff->id = atoi(kvm->cfg.guest_name);
  shared_stuff->instance_name = kvm->cfg.guest_name;
  shared_stuff->prediction_delta= 0;
  shared_stuff->reclaim_delta = 0;

  pthread_create(&iocore, NULL, iocore_main, (void*)kvm);

  return true;
}

static void virtio_pvl_do_io(struct kvm *kvm, u32 qaddr)
{
  virtio_pvl_do_io_request(kvm, &pdev, qaddr);
}

static u8 *get_config(struct kvm *kvm, void *dev)
{
  struct pvl_dev *pdev = dev;

  return ((u8 *)(&pdev->config));
}

static u32 get_host_features(struct kvm *kvm, void *dev)
{
  return 0;
  //return 1 << VIRTIO_BALLOON_F_STATS_VQ;
}

static void set_guest_features(struct kvm *kvm, void *dev, u32 features)
{
  struct pvl_dev *pdev = dev;

  pdev->features = features;
}

static int init_vq(struct kvm *kvm, void *dev, u32 vq, u32 pfn)
{
  struct pvl_dev *pdev = dev;
  struct virt_queue *queue;
  void *p;

  compat__remove_message(compat_id);

  queue		= &pdev->vqs[vq];
  queue->pfn	= pfn;
  p		= guest_pfn_to_host(kvm, queue->pfn);

  vring_init(&queue->vring, VIRTIO_PVL_QUEUE_SIZE, p, VIRTIO_PCI_VRING_ALIGN);

  return 0;
}

static int make_pvlq(struct kvm *kvm, void *dev, u32 pvlq)
{
  virtio_pvl_do_io(kvm, pvlq);
  return 0;
}

static int notify_vq(struct kvm *kvm, void *dev, u32 vq)
{
  return 0;
}

static int get_pfn_vq(struct kvm *kvm, void *dev, u32 vq)
{
  struct pvl_dev *pdev = dev;

  return pdev->vqs[vq].pfn;
}

static int get_size_vq(struct kvm *kvm, void *dev, u32 vq)
{
  return VIRTIO_PVL_QUEUE_SIZE;
}

static int set_size_vq(struct kvm *kvm, void *dev, u32 vq, int size)
{
  /* FIXME: dynamic */
  return size;
}

struct virtio_ops pvl_dev_virtio_ops = (struct virtio_ops) {
  .get_config		= get_config,
    .get_host_features	= get_host_features,
    .set_guest_features	= set_guest_features,
    .init_vq		= init_vq,
    .notify_vq		= notify_vq,
    .make_pvlq		= make_pvlq,
    .get_pfn_vq		= get_pfn_vq,
    .get_size_vq		= get_size_vq,
    .set_size_vq            = set_size_vq,
};

int virtio_pvl__init(struct kvm *kvm)
{
  virtio_init(kvm, &pdev, &pdev.vdev, &pvl_dev_virtio_ops,
      VIRTIO_DEFAULT_TRANS, PCI_DEVICE_ID_VIRTIO_PVL,
      VIRTIO_ID_PVL, PCI_CLASS_PVL);

  if (compat_id == -1)
    compat_id = virtio_compat_add_message("virtio-pvl", "CONFIG_VIRTIO_PVL");

  return 0;
}
virtio_dev_init(virtio_pvl__init);

int virtio_pvl__exit(struct kvm *kvm)
{
  return 0;
}
virtio_dev_exit(virtio_pvl__exit);
