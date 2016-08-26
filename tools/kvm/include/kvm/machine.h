#ifndef _MACHINE_H_
#define _MACHINE_H_
#define CFG_IA32_OPTIMIZATION 0
#define __cacheline_aligned __attribute__ ((aligned (64)))
#define __doubleword_aligned __attribute__ ((aligned (sizeof(void *) * 2)))

#define cmb() __asm__ __volatile__("":::"memory")
#define mb() __asm__ __volatile__("mfence":::"memory")
#define rmb() __asm__ __volatile__("lfence":::"memory")
#define wmb() __asm__ __volatile__("sfence":::"memory")

#define atomic_inc(p) atomic_add_return((volatile int *)p, 1)
#define atomic_dec(p) atomic_add_return((volatile int *)p, -1)
static inline int atomic_add_return(volatile int *p, int inc)
{
	int __i; 

	__i = inc; 
	__asm__ __volatile__(
		"lock\n\t xaddl %0,%1"
		: "+r"(inc), "+m"(*p)
		: : "memory"
	); 
	return inc + __i; 	
}

#define fas_w(p, v) __fas_w((volatile unsigned long *)p, (unsigned long)v)
static inline unsigned long __fas_w(volatile unsigned long *p, unsigned long v)
{
	unsigned long r; 

	__asm__ __volatile__(
		"lock\n\t xchgl %0,%1" 
		: "=r"(r), "=m"(*p)
		: "0"(v), "m"(*p)
		: "memory"
	); 
	return r; 
}

#define cas_w(p, o, v) __cas_w((volatile unsigned long *)p, (unsigned long)(o), (unsigned long)(v))
static inline int __cas_w(volatile unsigned long *p, unsigned long o, unsigned long v)
{
	unsigned long r; 

	__asm__ __volatile__(
		"lock\n\t cmpxchgl %2,%1"
		: "=a"(r), "=m"(*p)
		: "r"(v), "m"(*p), "0"(o)
		: "memory"
	);
	return r == o; 
}

#define cas_2w(p, o, v) __cas_2w((volatile unsigned long long *)p, ((unsigned long long *)&(o))[0], ((unsigned long long *)&(v))[0])
static inline int __cas_2w(volatile unsigned long long *p, unsigned long long o, unsigned long long v)
{
	unsigned long long r; 
	unsigned long *n32 = (unsigned long *)&v; 
	unsigned long tmp; 

	__asm__ __volatile__(
		"movl %%ebx,%2\n\t"
		"movl %5,%%ebx\n\t"
		"lock\n\t cmpxchg8b %1\n\t"
		"movl %2,%%ebx\n\t"
		: "=A"(r), "=m"(*p), "=m"(tmp)
		: "m"(*p), "0"(o), "g"(n32[0]), "c"(n32[1])
		: "memory"
	);
	return r == o; 
}

#define atomic_read_2w(p, v) __atomic_read_2w((volatile unsigned long long *)p, (unsigned long long *)v)
static inline void __atomic_read_2w(volatile unsigned long long *p, unsigned long long *v)
{
	unsigned long long r; 

#if CFG_IA32_OPTIMIZATION
	/* aligned access */ 
	if (((unsigned long)p&7u) == 0) {
		__asm__ __volatile__(
			"fildq %1\n\t"
			"fistpq %0"
			: "=m"(r)
			: "m"(*p)
			: "memory"
		); 
	}
	/* unaligned access */ 
	else 
#endif /* CFG_IA32_OPTIMIZATION */ 
	{
		unsigned long *v32 = (unsigned long *)&r;
		unsigned long tmp; 
		r = *v; 
		__asm__ __volatile__(
			"movl %%ebx,%2\n\t"
			"movl %5,%%ebx\n\t"
			"lock\n\t cmpxchg8b %1\n\t"
			"movl %2,%%ebx\n\t"
			: "=A"(r), "=m"(*p), "=m"(tmp)
			: "m"(*p), "0"(r), "g"(v32[0]), "c"(v32[1])
			: "memory"
		);
	}

	/* passing the result */ 
	*v = r; 
}

#define atomic_write_2w(p, v) __atomic_write_2w((volatile unsigned long long *)p, (unsigned long long)v)
static inline void __atomic_write_2w(volatile unsigned long long *p, unsigned long long v)
{
#if CFG_IA32_OPTIMIZATION
	/* aligned access */ 
	if (((unsigned long)p&7u) == 0) {
		__asm__ __volatile__(
			"fildq %1\n\t"
			"fistpq %0"
			: "=m"(*p)
			: "m"(v)
			: "memory"
		); 
	}
	/* unaligned access */ 
	else 
#endif /* CFG_IA32_OPTIMIZATION */ 
	{
		volatile unsigned long *p32 = (volatile unsigned long *)p;  
		unsigned long *v32 = (unsigned long *)&v; 
		unsigned long tmp; 

		__asm__ __volatile__(
			"movl %%ebx,%1\n\t"
			"movl %5,%%ebx\n\t"
			"retry:\n\t"
			"movl %3,%%eax\n\t"
			"movl %4,%%edx\n\t"
			"lock\n\t cmpxchg8b %0\n\t"
			"jnz retry"
			"movl %1,%%ebx\n\t"
			: "=m"(*p), "=m"(tmp)
			: "m"(*p), "g"(p32[0]), "g"(p32[1]), 
			  "g"(v32[0]), "c"(v32[1])
			: "eax", "edx", "memory"
		);
	}
}

#endif /* _MACHINE_H_ */ 
