

#include <linux/init.h>
#include <asm/cacheflush.h>
#include <asm/kmap_types.h>
#include <asm/fixmap.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>
#include <plat/cache-feroceon-l2.h>
#include "mm.h"


static inline unsigned long l2_start_va(unsigned long paddr)
{
#ifdef CONFIG_HIGHMEM
	/*
	 * Let's do our own fixmap stuff in a minimal way here.
	 * Because range ops can't be done on physical addresses,
	 * we simply install a virtual mapping for it only for the
	 * TLB lookup to occur, hence no need to flush the untouched
	 * memory mapping.  This is protected with the disabling of
	 * interrupts by the caller.
	 */
	unsigned long idx = KM_L2_CACHE + KM_TYPE_NR * smp_processor_id();
	unsigned long vaddr = __fix_to_virt(FIX_KMAP_BEGIN + idx);
	set_pte_ext(TOP_PTE(vaddr), pfn_pte(paddr >> PAGE_SHIFT, PAGE_KERNEL), 0);
	local_flush_tlb_kernel_page(vaddr);
	return vaddr + (paddr & ~PAGE_MASK);
#else
	return __phys_to_virt(paddr);
#endif
}

static inline void l2_clean_pa(unsigned long addr)
{
	__asm__("mcr p15, 1, %0, c15, c9, 3" : : "r" (addr));
}

static inline void l2_clean_pa_range(unsigned long start, unsigned long end)
{
	unsigned long va_start, va_end, flags;

	/*
	 * Make sure 'start' and 'end' reference the same page, as
	 * L2 is PIPT and range operations only do a TLB lookup on
	 * the start address.
	 */
	BUG_ON((start ^ end) >> PAGE_SHIFT);

	raw_local_irq_save(flags);
	va_start = l2_start_va(start);
	va_end = va_start + (end - start);
	__asm__("mcr p15, 1, %0, c15, c9, 4\n\t"
		"mcr p15, 1, %1, c15, c9, 5"
		: : "r" (va_start), "r" (va_end));
	raw_local_irq_restore(flags);
}

static inline void l2_clean_inv_pa(unsigned long addr)
{
	__asm__("mcr p15, 1, %0, c15, c10, 3" : : "r" (addr));
}

static inline void l2_inv_pa(unsigned long addr)
{
	__asm__("mcr p15, 1, %0, c15, c11, 3" : : "r" (addr));
}

static inline void l2_inv_pa_range(unsigned long start, unsigned long end)
{
	unsigned long va_start, va_end, flags;

	/*
	 * Make sure 'start' and 'end' reference the same page, as
	 * L2 is PIPT and range operations only do a TLB lookup on
	 * the start address.
	 */
	BUG_ON((start ^ end) >> PAGE_SHIFT);

	raw_local_irq_save(flags);
	va_start = l2_start_va(start);
	va_end = va_start + (end - start);
	__asm__("mcr p15, 1, %0, c15, c11, 4\n\t"
		"mcr p15, 1, %1, c15, c11, 5"
		: : "r" (va_start), "r" (va_end));
	raw_local_irq_restore(flags);
}

static inline void l2_inv_all(void)
{
	__asm__("mcr p15, 1, %0, c15, c11, 0" : : "r" (0));
}

#define CACHE_LINE_SIZE		32
#define MAX_RANGE_SIZE		1024

static int l2_wt_override;

static unsigned long calc_range_end(unsigned long start, unsigned long end)
{
	unsigned long range_end;

	BUG_ON(start & (CACHE_LINE_SIZE - 1));
	BUG_ON(end & (CACHE_LINE_SIZE - 1));

	/*
	 * Try to process all cache lines between 'start' and 'end'.
	 */
	range_end = end;

	/*
	 * Limit the number of cache lines processed at once,
	 * since cache range operations stall the CPU pipeline
	 * until completion.
	 */
	if (range_end > start + MAX_RANGE_SIZE)
		range_end = start + MAX_RANGE_SIZE;

	/*
	 * Cache range operations can't straddle a page boundary.
	 */
	if (range_end > (start | (PAGE_SIZE - 1)) + 1)
		range_end = (start | (PAGE_SIZE - 1)) + 1;

	return range_end;
}

static void feroceon_l2_inv_range(unsigned long start, unsigned long end)
{
	/*
	 * Clean and invalidate partial first cache line.
	 */
	if (start & (CACHE_LINE_SIZE - 1)) {
		l2_clean_inv_pa(start & ~(CACHE_LINE_SIZE - 1));
		start = (start | (CACHE_LINE_SIZE - 1)) + 1;
	}

	/*
	 * Clean and invalidate partial last cache line.
	 */
	if (start < end && end & (CACHE_LINE_SIZE - 1)) {
		l2_clean_inv_pa(end & ~(CACHE_LINE_SIZE - 1));
		end &= ~(CACHE_LINE_SIZE - 1);
	}

	/*
	 * Invalidate all full cache lines between 'start' and 'end'.
	 */
	while (start < end) {
		unsigned long range_end = calc_range_end(start, end);
		l2_inv_pa_range(start, range_end - CACHE_LINE_SIZE);
		start = range_end;
	}

	dsb();
}

static void feroceon_l2_clean_range(unsigned long start, unsigned long end)
{
	/*
	 * If L2 is forced to WT, the L2 will always be clean and we
	 * don't need to do anything here.
	 */
	if (!l2_wt_override) {
		start &= ~(CACHE_LINE_SIZE - 1);
		end = (end + CACHE_LINE_SIZE - 1) & ~(CACHE_LINE_SIZE - 1);
		while (start != end) {
			unsigned long range_end = calc_range_end(start, end);
			l2_clean_pa_range(start, range_end - CACHE_LINE_SIZE);
			start = range_end;
		}
	}

	dsb();
}

static void feroceon_l2_flush_range(unsigned long start, unsigned long end)
{
	start &= ~(CACHE_LINE_SIZE - 1);
	end = (end + CACHE_LINE_SIZE - 1) & ~(CACHE_LINE_SIZE - 1);
	while (start != end) {
		unsigned long range_end = calc_range_end(start, end);
		if (!l2_wt_override)
			l2_clean_pa_range(start, range_end - CACHE_LINE_SIZE);
		l2_inv_pa_range(start, range_end - CACHE_LINE_SIZE);
		start = range_end;
	}

	dsb();
}


static int __init flush_and_disable_dcache(void)
{
	u32 cr;

	cr = get_cr();
	if (cr & CR_C) {
		unsigned long flags;

		raw_local_irq_save(flags);
		flush_cache_all();
		set_cr(cr & ~CR_C);
		raw_local_irq_restore(flags);
		return 1;
	}
	return 0;
}

static void __init enable_dcache(void)
{
	u32 cr;

	cr = get_cr();
	set_cr(cr | CR_C);
}

static void __init __invalidate_icache(void)
{
	__asm__("mcr p15, 0, %0, c7, c5, 0" : : "r" (0));
}

static int __init invalidate_and_disable_icache(void)
{
	u32 cr;

	cr = get_cr();
	if (cr & CR_I) {
		set_cr(cr & ~CR_I);
		__invalidate_icache();
		return 1;
	}
	return 0;
}

static void __init enable_icache(void)
{
	u32 cr;

	cr = get_cr();
	set_cr(cr | CR_I);
}

static inline u32 read_extra_features(void)
{
	u32 u;

	__asm__("mrc p15, 1, %0, c15, c1, 0" : "=r" (u));

	return u;
}

static inline void write_extra_features(u32 u)
{
	__asm__("mcr p15, 1, %0, c15, c1, 0" : : "r" (u));
}

static void __init disable_l2_prefetch(void)
{
	u32 u;

	/*
	 * Read the CPU Extra Features register and verify that the
	 * Disable L2 Prefetch bit is set.
	 */
	u = read_extra_features();
	if (!(u & 0x01000000)) {
		printk(KERN_INFO "Feroceon L2: Disabling L2 prefetch.\n");
		write_extra_features(u | 0x01000000);
	}
}

static void __init enable_l2(void)
{
	u32 u;

	u = read_extra_features();
	if (!(u & 0x00400000)) {
		int i, d;

		printk(KERN_INFO "Feroceon L2: Enabling L2\n");

		d = flush_and_disable_dcache();
		i = invalidate_and_disable_icache();
		l2_inv_all();
		write_extra_features(u | 0x00400000);
		if (i)
			enable_icache();
		if (d)
			enable_dcache();
	}
}

void __init feroceon_l2_init(int __l2_wt_override)
{
	l2_wt_override = __l2_wt_override;

	disable_l2_prefetch();

	outer_cache.inv_range = feroceon_l2_inv_range;
	outer_cache.clean_range = feroceon_l2_clean_range;
	outer_cache.flush_range = feroceon_l2_flush_range;

	enable_l2();

	printk(KERN_INFO "Feroceon L2: Cache support initialised%s.\n",
			 l2_wt_override ? ", in WT override mode" : "");
}
