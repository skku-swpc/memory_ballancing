/*
 * Virtio balloon implementation, inspired by Dor Laor and Marcelo
 * Tosatti's implementations.
 *
 *  Copyright 2008 Rusty Russell IBM Corporation
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/virtio.h>
#include <linux/virtio_balloon.h>
#include <linux/swap.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/uaccess.h>

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

struct pvl_info info;

static struct virtio_device_id id_table[] = {
	{ 6, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

static void balloon_ack(struct virtqueue *vq)
{
	//NULL
}


static void tell_host(struct virtio_pvl *vp, struct virtqueue *vq)
{
	virtqueue_makequeue(vq, virt_to_phys(info.queue));
}


static int init_vqs(struct virtio_pvl *vp)
{
	struct virtqueue *vqs[1];
	vq_callback_t *callbacks[] = { balloon_ack };
	const char *names[] = { "pvl-command" };
	int err;

	err = vp->vdev->config->find_vqs(vp->vdev, 1, vqs, callbacks, names);
	if (err)
		return err;

	vp->command_vq = vqs[0];

	return 0;
}

static int virtpvl_probe(struct virtio_device *vdev)
{
	struct virtio_pvl *vp;
	struct page *page;
	int err;

	printk(KERN_INFO"PVL: in helper probe\n");

	vdev->priv = vp = kmalloc(sizeof(*vp), GFP_KERNEL);
	if (!vp) {
		err = -ENOMEM;
		goto out;
	}

	info.vp = vp;
	info.queue = (unsigned long)__get_free_pages(GFP_KERNEL | __GFP_ZERO, 5); //32pages
	if (!info.queue) {
		goto out_free_vp;
	}
	vp->vdev = vdev;

	err = init_vqs(vp);
	tell_host(info.vp, info.vp->command_vq);	
	if (err)
		goto out_free_vp;

	return 0;

out_free_vp:
	free_pages(vp, 5);
out:
	return err;
}


static void __devexit virtpvl_remove(struct virtio_device *vdev)
{
	struct virtio_pvl *vp = vdev->priv;
	//kthread_stop(vp->iocore);
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

static int bad_address(void *p)
{
	unsigned long dummy;
	return probe_kernel_address((unsigned long*)p, dummy);
}

static unsigned long uva_to_pa(unsigned long vaddr)
{
	pgd_t *pgd = pgd_offset(current->mm, vaddr);
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;

	struct page *pg;
	unsigned long paddr;

	if (bad_address(pgd)) {
		goto bad;
	}

	if (!pgd_present(*pgd)) {
		goto out;
	}

	pud = pud_offset(pgd, vaddr);

	if (bad_address(pud)) {
		goto bad;
	}

	if (!pud_present(*pud) || pud_large(*pud)) {
		goto out;
	}

	pmd = pmd_offset(pud, vaddr);
	if (bad_address(pmd)) {
		goto bad;
	}

	if(!pmd_present(*pmd) || pmd_large(*pmd)) {
		goto out;
	}

	pte = pte_offset_kernel(pmd, vaddr);
	if (bad_address(pte)) {
		goto bad;
	}

	if(!pte_present(*pte)) {
		goto out;
	}

	pg = pte_page(*pte);
	paddr = (pte_val(*pte) & PHYSICAL_PAGE_MASK) | (vaddr&(PAGE_SIZE-1));

out:
	return paddr;
bad:
	return 0;
}

#define PVL_MAJOR 234

#define PVL_IOC_INIT 		_IO(PVL_MAJOR, 1)
#define PVL_IOC_GET_GPA		_IOWR(PVL_MAJOR, 2, unsigned long)
#define PVL_IOC_SYNC 		_IO(PVL_MAJOR, 3)

static long pvl_ioctl(struct file *filp, unsigned int ctl, unsigned long param)
{
	unsigned long in, out;
	int size;

	size = _IOC_SIZE(ctl);

	switch(ctl) {
		case PVL_IOC_INIT:
            printk("test\n");
			tell_host(info.vp, info.vp->command_vq);	
			break;
		case PVL_IOC_GET_GPA:
			copy_from_user(&in, (void*)param, 4);
			out = uva_to_pa(in);
			copy_to_user((void*)param, &out, 4); 
			break;
		default:
			break;
	}

	return 0;
}

int pvl_mmap_fault(struct vm_area_struct *vma, struct vm_fault* vmf)
{
	struct page *page;
	int ret = 0;
	//printk("memory was successfully allocated - ");

	if((unsigned long)vmf->virtual_address == info.vaddr) {
		//printk("request queue allocated\n");
		page = virt_to_page(info.queue);
		get_page(page);
		vmf->page = page;
	} else {
		ret = -EINVAL;
	}
		return ret;
}

struct vm_operations_struct pvl_mmap_vm_ops = {
	.fault = pvl_mmap_fault,	
};

int pvl_mmap(struct file *filp, struct vm_area_struct *vma)
{
	vma->vm_ops = &pvl_mmap_vm_ops;
	vma->vm_flags |= VM_IO;

	info.vaddr = vma->vm_start;

	return 0;
}

struct file_operations pvl_fops = {
	.unlocked_ioctl = pvl_ioctl,
	.mmap = pvl_mmap,
};

static int __init init(void)
{
		
	printk(KERN_INFO"pvl-helper initialized\n");
/*
	if(register_chrdev(PVL_MAJOR, "pvl-helper", &pvl_fops)) {
		printk(KERN_EMERG"pvl-helper initialize fail.\n");
		return -1;
	}
*/
	return register_virtio_driver(&virtio_pvl_driver);
}

static void __exit fini(void)
{
//	unregister_chrdev(PVL_MAJOR, "pvl-helper");
	unregister_virtio_driver(&virtio_pvl_driver);
}
module_init(init);
module_exit(fini);

MODULE_DEVICE_TABLE(virtio, id_table);
MODULE_DESCRIPTION("Virtio pvl driver");
MODULE_LICENSE("GPL");
