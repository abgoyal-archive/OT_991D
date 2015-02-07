

#ifndef __ARM_PERF_EVENT_H__
#define __ARM_PERF_EVENT_H__

static inline void
set_perf_event_pending(void)
{
}

#define PERF_EVENT_INDEX_OFFSET 1

/* ARM perf PMU IDs for use by internal perf clients. */
enum arm_perf_pmu_ids {
	ARM_PERF_PMU_ID_XSCALE1	= 0,
	ARM_PERF_PMU_ID_XSCALE2,
	ARM_PERF_PMU_ID_V6,
	ARM_PERF_PMU_ID_V6MP,
	ARM_PERF_PMU_ID_CA8,
	ARM_PERF_PMU_ID_CA9,
	ARM_NUM_PMU_IDS,
};

extern enum arm_perf_pmu_ids
armpmu_get_pmu_id(void);

extern int
armpmu_get_max_events(void);

#endif /* __ARM_PERF_EVENT_H__ */
