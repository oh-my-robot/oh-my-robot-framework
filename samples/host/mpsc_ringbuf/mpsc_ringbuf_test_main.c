#include "data_struct/mpsc_ringbuf.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
typedef HANDLE HostThread;
typedef DWORD HostThreadRet;
#define HOST_THREAD_CALL WINAPI
static void host_thread_yield(void)
{
    (void)SwitchToThread();
}
#else
#include <pthread.h>
#include <sched.h>
typedef pthread_t HostThread;
typedef void *HostThreadRet;
#define HOST_THREAD_CALL
static void host_thread_yield(void)
{
    (void)sched_yield();
}
#endif

#define MPSCRB_TEST_CAPACITY     (64U)
#define MPSCRB_CONCURRENCY_TOTAL (2000000U)

/* ---- thread helpers (same as ringbuf test) ---- */

#ifdef _WIN32
static int host_thread_create(HostThread *thread, LPTHREAD_START_ROUTINE entry, void *arg)
{
    *thread = CreateThread(NULL, 0, entry, arg, 0, NULL);
    return (*thread == NULL) ? -1 : 0;
}
static int host_thread_join(HostThread thread)
{
    DWORD r = WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    return (r == WAIT_OBJECT_0) ? 0 : -1;
}
#else
static int host_thread_create(HostThread *thread, void *(*entry)(void *), void *arg)
{
    return pthread_create(thread, NULL, entry, arg);
}
static int host_thread_join(HostThread thread)
{
    return pthread_join(thread, NULL);
}
#endif

/* ---- basic test ---- */

static int run_mpscrb_basic_test(void)
{
    /* layout: 8 slots, 4-byte items */
    uint8_t buf[8 * 4];
    OmAtomicU8 ready[8];
    MpscRingbuf rb;

    uint32_t in_val;
    uint32_t out_val;

    memset(&rb, 0, sizeof(rb));
    if (!mpscrb_init(&rb, buf, ready, sizeof(uint32_t), 8))
        return 0;

    /* write / read single */
    in_val = 42;
    if (!mpscrb_in(&rb, &in_val))
        return 0;
    if (mpscrb_len(&rb) != 1)
        return 0;

    out_val = 0;
    if (!mpscrb_out(&rb, &out_val))
        return 0;
    if (out_val != 42)
        return 0;
    if (!mpscrb_is_empty(&rb))
        return 0;

    /* fill to capacity */
    for (uint32_t i = 0; i < 8; i++) {
        if (!mpscrb_in(&rb, &i))
            return 0;
    }
    if (!mpscrb_is_full(&rb))
        return 0;
    if (mpscrb_in(&rb, &in_val))
        return 0; /* should fail when full */

    /* peek first element */
    out_val = 99;
    if (!mpscrb_out_peek(&rb, &out_val))
        return 0;
    if (out_val != 0)
        return 0;
    if (mpscrb_len(&rb) != 8)
        return 0; /* peek does not consume */

    /* drain and verify FIFO */
    for (uint32_t i = 0; i < 8; i++) {
        if (!mpscrb_out(&rb, &out_val))
            return 0;
        if (out_val != i)
            return 0;
    }
    if (!mpscrb_is_empty(&rb))
        return 0;

    /* read from empty should fail */
    if (mpscrb_out(&rb, &out_val))
        return 0;

    return 1;
}

/* ---- concurrency test ---- */

typedef struct
{
    uint32_t producer_id;
    uint32_t seq;
} MpscTestItem;

typedef struct
{
    MpscRingbuf rb;
    uint32_t totalPerProducer;
    OmAtomicUint produced;
    OmAtomicUint consumed;
    OmAtomicUint writeRetry;
    OmAtomicUint readRetry;
    OmAtomicUint errors;
    OmAtomicUint stop;
} MpscConcurrencyCtx;

typedef struct
{
    MpscConcurrencyCtx *ctx;
    uint32_t producer_id;
} ProducerArg;

static HostThreadRet HOST_THREAD_CALL producer_thread(void *arg)
{
    ProducerArg *parg       = (ProducerArg *)arg;
    MpscConcurrencyCtx *ctx = parg->ctx;
    uint32_t id             = parg->producer_id;
    uint32_t seq            = 0;
    uint32_t total          = ctx->totalPerProducer;

    while (seq < total && OM_LOAD_ACQ(&ctx->stop) == 0) {
        MpscTestItem item;
        item.producer_id = id;
        item.seq         = seq;

        if (mpscrb_in(&ctx->rb, &item)) {
            OM_INC_AR(&ctx->produced);
            seq++;
            continue;
        }

        OM_INC_AR(&ctx->writeRetry);
        host_thread_yield();
    }
#ifndef _WIN32
    return NULL;
#else
    return 0;
#endif
}

static HostThreadRet HOST_THREAD_CALL consumer_thread(void *arg)
{
    MpscConcurrencyCtx *ctx  = (MpscConcurrencyCtx *)arg;
    uint32_t expected_seq[2] = {0, 0}; /* per-producer next expected seq */
    uint32_t total           = ctx->totalPerProducer * 2;

    while (OM_LOAD_ACQ(&ctx->consumed) < total && OM_LOAD_ACQ(&ctx->stop) == 0) {
        MpscTestItem item;

        if (mpscrb_out(&ctx->rb, &item)) {
            uint32_t pid = item.producer_id;
            if (pid > 1 || item.seq != expected_seq[pid]) {
                OM_INC_AR(&ctx->errors);
                OM_STORE_REL(&ctx->stop, 1);
                break;
            }
            expected_seq[pid]++;
            OM_INC_AR(&ctx->consumed);
            continue;
        }

        OM_INC_AR(&ctx->readRetry);
        host_thread_yield();
    }
#ifndef _WIN32
    return NULL;
#else
    return 0;
#endif
}

static int run_mpscrb_concurrency_test(void)
{
    MpscConcurrencyCtx ctx;
    ProducerArg parg0, parg1;
    HostThread prod0, prod1, cons;
    clock_t t0, t1;
    double elapsed;

    memset(&ctx, 0, sizeof(ctx));
    ctx.totalPerProducer = MPSCRB_CONCURRENCY_TOTAL / 2;

    OM_STORE_RLX(&ctx.produced, 0);
    OM_STORE_RLX(&ctx.consumed, 0);
    OM_STORE_RLX(&ctx.writeRetry, 0);
    OM_STORE_RLX(&ctx.readRetry, 0);
    OM_STORE_RLX(&ctx.errors, 0);
    OM_STORE_RLX(&ctx.stop, 0);

    if (!mpscrb_alloc(&ctx.rb, sizeof(MpscTestItem), MPSCRB_TEST_CAPACITY, NULL)) {
        printf("[FAIL] mpscrb_alloc failed\n");
        return 0;
    }

    parg0.ctx         = &ctx;
    parg0.producer_id = 0;
    parg1.ctx         = &ctx;
    parg1.producer_id = 1;

    t0 = clock();

    if (host_thread_create(&prod0, producer_thread, &parg0) != 0 ||
        host_thread_create(&prod1, producer_thread, &parg1) != 0 ||
        host_thread_create(&cons, consumer_thread, &ctx) != 0) {
        printf("[FAIL] thread create failed\n");
        mpscrb_free(&ctx.rb, NULL);
        return 0;
    }

    host_thread_join(prod0);
    host_thread_join(prod1);
    host_thread_join(cons);

    t1      = clock();
    elapsed = (double)(t1 - t0) / (double)CLOCKS_PER_SEC;

    printf("produced=%u consumed=%u write_retry=%u read_retry=%u errors=%u elapsed=%.3fs\n",
           OM_LOAD_ACQ(&ctx.produced), OM_LOAD_ACQ(&ctx.consumed), OM_LOAD_ACQ(&ctx.writeRetry),
           OM_LOAD_ACQ(&ctx.readRetry), OM_LOAD_ACQ(&ctx.errors), elapsed);

    if (OM_LOAD_ACQ(&ctx.errors) != 0 || OM_LOAD_ACQ(&ctx.produced) != MPSCRB_CONCURRENCY_TOTAL ||
        OM_LOAD_ACQ(&ctx.consumed) != MPSCRB_CONCURRENCY_TOTAL || !mpscrb_is_empty(&ctx.rb)) {
        mpscrb_free(&ctx.rb, NULL);
        return 0;
    }

    mpscrb_free(&ctx.rb, NULL);
    return 1;
}

/* ---- main ---- */

int main(void)
{
    int basic  = run_mpscrb_basic_test();
    int concur = run_mpscrb_concurrency_test();

    if (!basic) {
        printf("[FAIL] mpscrb basic test failed\n");
        return 1;
    }
    printf("[PASS] mpscrb basic test passed\n");

    if (!concur) {
        printf("[FAIL] mpscrb concurrency test failed\n");
        return 2;
    }
    printf("[PASS] mpscrb concurrency test passed\n");

    return 0;
}
