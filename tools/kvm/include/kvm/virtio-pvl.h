#ifndef KVM__PVL_VIRTIO_H
#define KVM__PVL_VIRTIO_H

struct kvm;

int virtio_pvl__init(struct kvm *kvm);
int virtio_pvl__exit(struct kvm *kvm);

#endif /* KVM__BLN_VIRTIO_H */
