/*
 *
 * Process load indicator
 *
 * Author  : Per Hallsmark <per.hallsmark@windriver.com>
 *
 * License : GPLv2
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#define CLOCK CLOCK_MONOTONIC
//#define BASE_CPUTIME
#define BASE_PERIODTIME

static struct timespec period;

static pid_t pid = 0;
static pid_t tid = 0;
static int ticks_per_second;
static int ns_per_tick;
static int updated = 0;

static FILE *fcpustat;
static FILE *fprocpidstat;

#ifdef BASE_CPUTIME
/* support for up to 32 cores... */
#define NUM_CORES 32
static struct cpustat {
	uint64_t cpu_total_time;
} lcs[NUM_CORES];
#endif

static struct procpidstat {
	uint64_t utime;
	uint64_t stime;
	uint64_t cutime;
	uint64_t cstime;
	uint64_t jiffies;
} lps;

static void open_cpustat(void)
{
	fcpustat = fopen("/proc/stat", "r");
	if (fcpustat == NULL) {
		perror("fopen cpustat ");
		exit(-1);
	}
}

static void open_procpidstat(pid_t pid, pid_t tid)
{
	char str[128];
	if (tid) {
		sprintf(str, "/proc/%d/task/%d/stat", pid, tid);
	} else {
		sprintf(str, "/proc/%d/stat", pid);
	}
	fprocpidstat = fopen(str, "r");
	if (fprocpidstat == NULL) {
		perror("fopen procpidstat");
		exit(-1);
	}
}

static void error(void)
{
	printf("please enter pid number as arg1 and optionally tid number as arg2\n");
	exit(-1);
}

static void ts_add(const struct timespec *t1, const struct timespec *t2, struct timespec *t)
{
	t->tv_sec = t1->tv_sec + t2->tv_sec;
	t->tv_nsec = t1->tv_nsec + t2->tv_nsec;
	if (t->tv_nsec >= 1000000000L) {
		t->tv_sec++ ;
		t->tv_nsec -= 1000000000L;
	}
}

static void ts_sub(struct timespec *t1, struct timespec *t2, struct timespec *result)
{
	result->tv_nsec = t2->tv_nsec - t1->tv_nsec;
	if (result->tv_nsec < 0 ) {
		result->tv_nsec += 1000000000;
		result->tv_sec = t2->tv_sec - t1->tv_sec - 1;
	} else {
		result->tv_sec = t2->tv_sec - t1->tv_sec;
	}
}


/*
 * helper calling a function at a specifid periodic rate, which should be below 1 sec.
 * work time is expected to be guaranteed less than (period time - clock setup time)
 */
static void periodic_loop(const struct timespec *period, void (*work)(struct timespec *, struct timespec *))
{
	struct timespec tbase;
	struct timespec tbefore, tafter, tactual;
	int status;

	clock_gettime(CLOCK, &tbase);

	while (1) {
		// calculate start of next period
		ts_add(&tbase, period, &tbase);

		clock_gettime(CLOCK, &tbefore);
		// wait for start of next period
		status = clock_nanosleep(CLOCK, TIMER_ABSTIME, &tbase, NULL);
		clock_gettime(CLOCK, &tafter);
		if (status)
			printf("status = %d\n", status);

		// execute loop work load
		ts_sub(&tbefore, &tafter, &tactual);
		if ((tactual.tv_sec == 0) && (tactual.tv_nsec < 900000000)) {
			// disregard of corner case were we calculated start of next period,
			// got interrupted and that for quite some time so that when kernel
			// get time to schedule this in, we are already past our period!
			updated=0;
			continue;
		}
		work(&tbefore, &tactual);
	}
}

static void info_via_proc_pid_stat(struct timespec *ts, uint64_t deltans)
{
#ifdef BASE_CPUTIME
	struct cpustat ccs;
#endif
	struct procpidstat cps;
	float uload, sload, load;
	uint64_t cpu_time[10];
	uint64_t total_time_diff;
	int i, corenum;
	int status;
	char buf[1024];
	struct timespec tnow, tdelta;

#ifdef BASE_CPUTIME
	memset(cpu_time, 0, sizeof(cpu_time));
	open_cpustat();
	// get stats for all cores
	fgets(buf, sizeof(buf), fcpustat);
	//printf("  buf \"%s\"\n", buf);
	if (sscanf(buf, "cpu %Lu %Lu %Lu %Lu %Lu %Lu %Lu %Lu %Lu %Lu",
		&cpu_time[0], &cpu_time[1], &cpu_time[2], &cpu_time[3],
		&cpu_time[4], &cpu_time[5], &cpu_time[6], &cpu_time[7],
		&cpu_time[8], &cpu_time[9]) == EOF) {
		printf("error getting cpu stat\n");
	}
	// TODO: get stats per core
	fclose(fcpustat);


	ccs.cpu_total_time = cpu_time[0];
	for (i=1; i<10; i++)
		ccs.cpu_total_time += cpu_time[i];
	total_time_diff = ccs.cpu_total_time - lcs.cpu_total_time;
	for (corenum=0; corenume<10; corenume++)

#elif defined(BASE_PERIODTIME)
	total_time_diff = deltans / ns_per_tick;
#endif

	memset(&cps, 0, sizeof(cps));
	open_procpidstat(pid, tid);
	fgets(buf, sizeof(buf), fprocpidstat);
	status = sscanf(buf, "%*d %*s %*c %*Ld %*Ld %*Ld %*Ld %*Ld"
		" %*Lu %*Lu %*Lu %*Lu %*Lu %Lu %Lu %Ld %Ld"
		" %*Lu %*Lu %*Lu %*Lu %*Lu %*Lu %*Lu %*Lu %*Lu %*Lu"
		" %*Lu %*Lu %*Lu %*Lu %*Lu %*Lu %*Lu %*Lu %*Lu %*Lu"
		" %*Lu %*Lu %*Lu %*Lu %*Lu %*Lu %*Lu %*Lu %*Lu %*Lu"
		" %*Lu %*Lu %*Lu %*Lu %*Lu %Lu",
		&cps.utime, &cps.stime, &cps.cutime, &cps.cstime, &cps.jiffies);
	fclose(fprocpidstat);

	//printf("total_time_diff %Lu\n", total_time_diff);

	if (updated) {
		uload = 100.0 * ((float)(cps.utime-lps.utime) / (float) total_time_diff);
		sload = 100.0 * ((float)(cps.stime-lps.stime) / (float) total_time_diff);
		load = uload + sload;
		// disregard of corner case were we calculated duration of last period,
		// got interrupted and that for quite some time so that when kernel
		// get time to schedule this in, we will not have relevant data!
		clock_gettime(CLOCK, &tnow);
		ts_sub(ts, &tnow, &tdelta);
		deltans = tdelta.tv_sec*1000000000+tdelta.tv_nsec;
		if (deltans > 1100000000) {
			printf("load avg not calculated, this process slipped too much\n");
			updated=0;
		} else {
			printf("load avg %000.0f%\n", load);
		}
		fflush(stdout);
	}
	
#ifdef BASE_CPUTIME
	memcpy(&lcs, &ccs, sizeof(struct cpustat));
#endif
	memcpy(&lps, &cps, sizeof(struct procpidstat));
	updated = 1;
}

static void worker(struct timespec *ts, struct timespec *actual)
{
	uint64_t deltans;

	deltans = actual->tv_sec*1000000000+actual->tv_nsec;
	//printf("deltans %Ld\n", deltans);

	info_via_proc_pid_stat(ts, deltans);
}

int main(int argc, char *argv[])
{
	struct timespec resolution;

	if (argc >= 2)
		pid = atoi(argv[1]);

	if (pid == 0)
		error();

	if (argc >= 3) {
		tid = atoi(argv[2]);

		if (tid == 0)
			error();
	}

	ticks_per_second = sysconf(_SC_CLK_TCK);
	ns_per_tick = 1000000000/ticks_per_second;

	clock_getres(CLOCK, &resolution);
	//printf("clock resolution is %d s %d ns\n", resolution.tv_sec, resolution.tv_nsec);

	period.tv_sec = 1;
	period.tv_nsec = 0;
	//printf("period set to %d s %d ns\n", period.tv_sec, period.tv_nsec);


	periodic_loop(&period, worker);
}
