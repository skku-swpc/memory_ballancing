./lkvm run -c 2 -m 512 -k /home/kim/Desktop/linux-3.7.10/arch/x86/boot/bzImage \
  -d test1.img \
  --balloon \
  --sdl \
  --vidmode 0x301
