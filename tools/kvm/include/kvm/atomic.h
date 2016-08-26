#ifndef	__ATOMIC_H__
#define	__ATOMIC_H__


static inline void atomic_inc(unsigned long *v)
{
	asm volatile("lock\n\tincl %0\n\t"
		     : "+m" (*v));
}

static inline void atomic_dec(unsigned long *v)
{
	asm volatile("lock\n\tdecl %0\n\t"
		     : "+m" (*v));
}

static inline int atomic_cmpxchg(unsigned long *v, int old, int val)
{
	int retval;

	asm volatile("lock\n\tcmpxchgl %1,%2\n\t"
			: "=a"(retval)
			: "r"(val), "m"(*v), "0"(old)
			: "memory");

	return retval;
}

static inline int atomic_add_return(unsigned long *v, int i)
{
	asm volatile("lock\n\txaddl %0, %1"
		     : "+r" (i), "+m" (*v)
		     : : "memory");

	return i;
}

static inline int atomic_sub_return(unsigned long *v, int i)
{
	return atomic_add_return(v, -i);
}

#define atomic_inc_return(v)  (atomic_add_return(v, 1))
#define atomic_dec_return(v)  (atomic_sub_return(v, 1))


#endif
