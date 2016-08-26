#include <linux/virtio.h>
#include <linux/virtio_balloon.h>
#include <linux/swap.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/uaccess.h>

#define QUEUE_SIZE 100

#define K(x) ((x) << (PAGE_SHIFT - 10))
#define TIME_STEP (1*HZ)

struct timer_list *timer;
struct sysinfo i;

static int lru;
unsigned long pages[NR_LRU_LISTS];

void kerneltimer_timeover();
void kerneltimer_registertimer(struct timer_list* ptimer, unsigned long timeover );

struct pvl_info
{
	struct virtio_pvl *vp;
	unsigned long vaddr;
	unsigned long queue;
};

struct virtio_pvl
{
	struct virtio_device *vdev;
	struct virtqueue *command_vq;
	struct task_struct *iocore;
};

struct pvl_command_t {
    unsigned short command;
    unsigned short undone;
    long args[6];
    unsigned long result;
};

struct pvl_queue_t {
    int head;
    int tail;
    struct pvl_command_t pages[QUEUE_SIZE];
};

struct pvl_queue_t *pvl_queue;

struct pvl_info info;

static struct virtio_device_id id_table[] = {
	{ 6, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

static void balloon_ack(struct virtqueue *vq){}

static void tell_host(struct virtio_pvl *vp, struct virtqueue *vq)
{
	virtqueue_makequeue(vq, virt_to_phys(info.queue));
}

static void init_vqs(struct virtio_pvl *vp)
{
	struct virtqueue *vqs[1];
	vq_callback_t *callbacks[] = { balloon_ack };
	const char *names[] = { "pvl-command" };

	vp->vdev->config->find_vqs(vp->vdev, 1, vqs, callbacks, names);
	vp->command_vq = vqs[0];
}

//////////////////////////////////////////////////////////////////////////
static inline int atomic_cmpxch(unsigned long *v, int old, int val)
{
    int retval;

    __asm__ __volatile__ ("lock\n\tcmpxchgl %1,%2\n\t"
                        : "=a"(retval)
                        : "r"(val), "m"(*v), "0"(old)
                        : "memory");

    return retval;
}

struct pvl_command_t * pvl_enqueue(struct pvl_queue_t *q, int command, unsigned long * args) {
    int index;
    int next;

    do {
        if ((q->tail + 1) % QUEUE_SIZE == q->head)
            return NULL;

        index = q->tail;
        next = (index + 1) % QUEUE_SIZE;

        if (q->pages[index].undone == 1)
            return NULL;

    } while(atomic_cmpxch((unsigned long*)&q->tail, index, next) != index);

    q->pages[index].command = command;
    q->pages[index].undone = 1;
    q->pages[index].args[0] = args[0];
    q->pages[index].args[1] = args[1];
    q->pages[index].args[2] = args[2];

    __asm__ __volatile__("sfence":::"memory");

    return &q->pages[index];
}

void send(void)
{
    struct pvl_command_t* cmd;
    unsigned long args[5];
    si_meminfo(&i);
    si_swapinfo(&i);

  for(lru = LRU_BASE; lru < NR_LRU_LISTS; lru++)
    pages[lru] = global_page_state(NR_LRU_BASE + lru);

    args[0] = K((pages[LRU_ACTIVE_FILE] + pages[LRU_ACTIVE_ANON]));
    args[1] = K((pages[LRU_INACTIVE_FILE] + i.freeram));
    args[2] = K((i.totalswap - i.freeswap));
    cmd = pvl_enqueue((void *)info.queue, 1, args);
}

void kerneltimer_registertimer(struct timer_list* ptimer, unsigned long timeover)

{
  init_timer( ptimer );
  ptimer->expires  = get_jiffies_64() + timeover;
  ptimer->function = kerneltimer_timeover;
  add_timer( ptimer);
}

void kerneltimer_timeover()
{
  send();
  kerneltimer_registertimer( timer, TIME_STEP );
}

/////////////////////////////////////////////////////////////////////////////


static int virtpvl_probe(struct virtio_device *vdev)
{
	struct virtio_pvl *vp;
	vdev->priv = vp = kmalloc(sizeof(*vp), GFP_KERNEL);

	info.vp = vp;
	info.queue = (unsigned long)__get_free_pages(GFP_KERNEL | __GFP_ZERO, 5);
	vp->vdev = vdev;
    timer= kmalloc( sizeof( struct timer_list ), GFP_KERNEL );

    init_vqs(vp);
    tell_host(info.vp, info.vp->command_vq);	
    memset( timer, 0, sizeof( struct timer_list) );
    kerneltimer_registertimer( timer,TIME_STEP );
	return 0;
}


static void __devexit virtpvl_remove(struct virtio_device *vdev)
{
	struct virtio_pvl *vp = vdev->priv;
	kfree(vp);
}

static unsigned int features[] = {
	VIRTIO_BALLOON_F_MUST_TELL_HOST,
};

static struct virtio_driver virtio_pvl_driver = {
	.feature_table = features,
	.feature_table_size = ARRAY_SIZE(features),
	.driver.name =	KBUILD_MODNAME,
	.driver.owner =	THIS_MODULE,
	.id_table =	id_table,
	.probe =	virtpvl_probe,
	.remove =	__devexit_p(virtpvl_remove),
};

static int __init init(void)
{
	return register_virtio_driver(&virtio_pvl_driver);
}

static void __exit fini(void)
{
	unregister_virtio_driver(&virtio_pvl_driver);
}
module_init(init);
module_exit(fini);

MODULE_DEVICE_TABLE(virtio, id_table);
MODULE_DESCRIPTION("Virtio pvl driver");
MODULE_LICENSE("GPL");
