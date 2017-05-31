/*
 * Copyright (C) 2016 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <android/hardware/tests/libhwbinder/1.0/IScheduleTest.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <tuple>
#include <vector>

#include <hidl/LegacySupport.h>
#include <pthread.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace std;
using namespace android;
using namespace android::hardware;

using android::hardware::tests::libhwbinder::V1_0::IScheduleTest;

#define ASSERT(cond)                                                                              \
    do {                                                                                          \
        if (!(cond)) {                                                                            \
            cerr << __func__ << ":" << __LINE__ << " condition:" << #cond << " failed\n" << endl; \
            exit(EXIT_FAILURE);                                                                   \
        }                                                                                         \
    } while (0)

vector<sp<IScheduleTest> > services;

// the ratio that the service is synced on the same cpu beyond
// GOOD_SYNC_MIN is considered as good
#define GOOD_SYNC_MIN (0.6)

#define DUMP_PRICISION 2

string trace_path = "/sys/kernel/debug/tracing";

// the default value
int no_pair = 1;
int iterations = 100;
int verbose = 0;
int is_tracing;
bool pass_through = false;
// the deadline latency that we are interested in
uint64_t deadline_us = 2500;

static bool traceIsOn() {
    fstream file;
    file.open(trace_path + "/tracing_on", ios::in);
    char on;
    file >> on;
    file.close();
    return on == '1';
}

static void traceStop() {
    ofstream file;
    file.open(trace_path + "/tracing_on", ios::out | ios::trunc);
    file << '0' << endl;
    file.close();
}

static int threadGetPri() {
    struct sched_param param;
    int policy;
    ASSERT(!pthread_getschedparam(pthread_self(), &policy, &param));
    return param.sched_priority;
}

static void threadDumpPri(const char* prefix) {
    struct sched_param param;
    int policy;
    if (!verbose) return;
    cout << "--------------------------------------------------" << endl;
    cout << setw(12) << left << prefix << " pid: " << getpid() << " tid: " << gettid()
         << " cpu: " << sched_getcpu() << endl;
    ASSERT(!pthread_getschedparam(pthread_self(), &policy, &param));
    string s =
        (policy == SCHED_OTHER)
            ? "SCHED_OTHER"
            : (policy == SCHED_FIFO) ? "SCHED_FIFO" : (policy == SCHED_RR) ? "SCHED_RR" : "???";
    cout << setw(12) << left << s << param.sched_priority << endl;
    return;
}

// This IPC class is widely used in binder/hwbinder tests.
// The common usage is the main process to create the Pipe and forks.
// Both parent and child hold a object and each wait() on parent
// needs a signal() on the child to wake up and vice versa.
class Pipe {
    int m_readFd;
    int m_writeFd;
    Pipe(int readFd, int writeFd) : m_readFd{readFd}, m_writeFd{writeFd} {}
    Pipe(const Pipe&) = delete;
    Pipe& operator=(const Pipe&) = delete;
    Pipe& operator=(const Pipe&&) = delete;

   public:
    Pipe(Pipe&& rval) noexcept {
        m_readFd = rval.m_readFd;
        m_writeFd = rval.m_writeFd;
        rval.m_readFd = 0;
        rval.m_writeFd = 0;
    }
    ~Pipe() {
        if (m_readFd) close(m_readFd);
        if (m_writeFd) close(m_writeFd);
    }
    void signal() {
        bool val = true;
        int error = write(m_writeFd, &val, sizeof(val));
        ASSERT(error >= 0);
    };
    void wait() {
        bool val = false;
        int error = read(m_readFd, &val, sizeof(val));
        ASSERT(error >= 0);
    }
    template <typename T>
    void send(const T& v) {
        int error = write(m_writeFd, &v, sizeof(T));
        ASSERT(error >= 0);
    }
    template <typename T>
    void recv(T& v) {
        int error = read(m_readFd, &v, sizeof(T));
        ASSERT(error >= 0);
    }
    static tuple<Pipe, Pipe> createPipePair() {
        int a[2];
        int b[2];

        int error1 = pipe(a);
        int error2 = pipe(b);
        ASSERT(error1 >= 0);
        ASSERT(error2 >= 0);

        return make_tuple(Pipe(a[0], b[1]), Pipe(b[0], a[1]));
    }
};

typedef chrono::time_point<chrono::high_resolution_clock> Tick;

static inline Tick tickNow() {
    return chrono::high_resolution_clock::now();
}

static inline uint64_t tickNano(Tick& sta, Tick& end) {
    return uint64_t(chrono::duration_cast<chrono::nanoseconds>(end - sta).count());
}

// statistics of latency
class Results {
    static const uint32_t num_buckets = 128;
    static const uint64_t max_time_bucket = 50ull * 1000000;
    static const uint64_t time_per_bucket = max_time_bucket / num_buckets;
    static constexpr float time_per_bucket_ms = time_per_bucket / 1.0E6;

    uint64_t m_best = 0xffffffffffffffffULL;
    uint64_t m_worst = 0;
    uint64_t m_transactions = 0;
    uint64_t m_total_time = 0;
    uint64_t m_miss = 0;
    uint32_t m_buckets[num_buckets] = {0};
    bool tracing = false;

   public:
    void setTrace(bool _tracing) { tracing = _tracing; }
    inline uint64_t getTransactions() { return m_transactions; }
    inline bool missDeadline(uint64_t nano) { return nano > deadline_us * 1000; }
    // Combine two sets of latency data points and update the aggregation info.
    static Results combine(const Results& a, const Results& b) {
        Results ret;
        for (uint32_t i = 0; i < num_buckets; i++) {
            ret.m_buckets[i] = a.m_buckets[i] + b.m_buckets[i];
        }
        ret.m_worst = max(a.m_worst, b.m_worst);
        ret.m_best = min(a.m_best, b.m_best);
        ret.m_transactions = a.m_transactions + b.m_transactions;
        ret.m_miss = a.m_miss + b.m_miss;
        ret.m_total_time = a.m_total_time + b.m_total_time;
        return ret;
    }
    void addTime(uint64_t nano) {
        m_buckets[min(nano, max_time_bucket - 1) / time_per_bucket] += 1;
        m_best = min(nano, m_best);
        m_worst = max(nano, m_worst);
        m_transactions += 1;
        m_total_time += nano;
        if (missDeadline(nano)) m_miss++;
        if (missDeadline(nano) && tracing) {
            // There might be multiple process pair running the test concurrently
            // each may execute following statements and only the first one actually
            // stop the trace and any traceStop() afterthen has no effect.
            traceStop();
            cout << endl;
            cout << "deadline triggered: halt & stop trace" << endl;
            cout << "log:" + trace_path + "/trace" << endl;
            cout << endl;
            exit(1);
        }
    }
    // dump average, best, worst latency in json
    void dump() {
        double best = (double)m_best / 1.0E6;
        double worst = (double)m_worst / 1.0E6;
        double average = (double)m_total_time / m_transactions / 1.0E6;
        int W = DUMP_PRICISION + 2;
        cout << std::setprecision(DUMP_PRICISION) << "{ \"avg\":" << setw(W) << left << average
             << ", \"wst\":" << setw(W) << left << worst << ", \"bst\":" << setw(W) << left << best
             << ", \"miss\":" << left << m_miss
             << ", \"meetR\":" << setprecision(DUMP_PRICISION + 3) << left
             << (1.0 - (double)m_miss / m_transactions) << "}";
    }
    // dump latency distribution in json
    void dumpDistribution() {
        uint64_t cur_total = 0;
        cout << "{ ";
        cout << std::setprecision(DUMP_PRICISION + 3);
        for (uint32_t i = 0; i < num_buckets; i++) {
            float cur_time = time_per_bucket_ms * i + 0.5f * time_per_bucket_ms;
            if ((cur_total < 0.5f * m_transactions) &&
                (cur_total + m_buckets[i] >= 0.5f * m_transactions)) {
                cout << "\"p50\":" << cur_time << ", ";
            }
            if ((cur_total < 0.9f * m_transactions) &&
                (cur_total + m_buckets[i] >= 0.9f * m_transactions)) {
                cout << "\"p90\":" << cur_time << ", ";
            }
            if ((cur_total < 0.95f * m_transactions) &&
                (cur_total + m_buckets[i] >= 0.95f * m_transactions)) {
                cout << "\"p95\":" << cur_time << ", ";
            }
            if ((cur_total < 0.99f * m_transactions) &&
                (cur_total + m_buckets[i] >= 0.99f * m_transactions)) {
                cout << "\"p99\": " << cur_time;
            }
            cur_total += m_buckets[i];
        }
        cout << "}";
    }
};
// statistics of a process pair
class PResults {
   public:
    int no_inherent = 0;
    int no_sync = 0;
    class Results other, fifo;
    PResults() { fifo.setTrace(is_tracing); }

    static PResults combine(const PResults& a, const PResults& b) {
        PResults ret;
        ret.no_inherent = a.no_inherent + b.no_inherent;
        ret.no_sync = a.no_sync + b.no_sync;
        ret.other = Results::combine(a.other, b.other);
        ret.fifo = Results::combine(a.fifo, b.fifo);
        return ret;
    }

    void dump(string name) {
        int no_trans = other.getTransactions() + fifo.getTransactions();
        double sync_ratio = (1.0 - (double)(no_sync) / no_trans);
        cout << "\"" << name << "\":{\"SYNC\":\""
             << ((sync_ratio > GOOD_SYNC_MIN) ? "GOOD" : "POOR") << "\","
             << "\"S\":" << (no_trans - no_sync) << ",\"I\":" << no_trans << ","
             << "\"R\":" << sync_ratio << "," << endl;
        cout << "  \"other_ms\":";
        other.dump();
        cout << "," << endl;
        cout << "  \"fifo_ms\": ";
        fifo.dump();
        cout << "," << endl;
        cout << "  \"otherdis\":";
        other.dumpDistribution();
        cout << "," << endl;
        cout << "  \"fifodis\": ";
        fifo.dumpDistribution();
        cout << endl;
        cout << "}," << endl;
    }
};

struct threadArg {
    void* result;  ///< pointer to PResults
    int target;    ///< the terget service number
};

static void* threadStart(void* p) {
    threadArg* priv = (threadArg*)p;
    int target = priv->target;
    PResults* presults = (PResults*)priv->result;
    Tick sta, end;

    threadDumpPri("fifo-caller");
    uint32_t call_sta = (threadGetPri() << 16) | sched_getcpu();
    sp<IScheduleTest> service = services[target];
    asm("" ::: "memory");
    sta = tickNow();
    uint32_t ret = service->send(verbose, call_sta);
    end = tickNow();
    asm("" ::: "memory");
    presults->fifo.addTime(tickNano(sta, end));

    presults->no_inherent += (ret >> 16) & 0xffff;
    presults->no_sync += ret & 0xffff;
    return 0;
}

// create a fifo thread to transact and wait it to finished
static void threadTransaction(int target, PResults* presults) {
    threadArg thread_arg;

    void* dummy;
    pthread_t thread;
    pthread_attr_t attr;
    struct sched_param param;
    thread_arg.target = target;
    thread_arg.result = presults;
    ASSERT(!pthread_attr_init(&attr));
    ASSERT(!pthread_attr_setschedpolicy(&attr, SCHED_FIFO));
    param.sched_priority = sched_get_priority_max(SCHED_FIFO);
    ASSERT(!pthread_attr_setschedparam(&attr, &param));
    ASSERT(!pthread_create(&thread, &attr, threadStart, &thread_arg));
    ASSERT(!pthread_join(thread, &dummy));
}

static void serviceFx(const string& serviceName, Pipe p) {
    // Start service.
    if (registerPassthroughServiceImplementation<IScheduleTest>(serviceName) != ::android::OK) {
        cout << "Failed to register service " << serviceName.c_str() << endl;
        exit(EXIT_FAILURE);
    }
    // tell main I'm init-ed
    p.signal();
    // wait for kill
    p.wait();
    exit(0);
}

static Pipe makeServiceProces(string service_name) {
    auto pipe_pair = Pipe::createPipePair();
    pid_t pid = fork();
    if (pid) {
        // parent
        return move(get<0>(pipe_pair));
    } else {
        threadDumpPri("service");
        // child
        serviceFx(service_name, move(get<1>(pipe_pair)));
        // never get here
        ASSERT(0);
        return move(get<0>(pipe_pair));
    }
}

static void clientFx(int num, int server_count, int iterations, Pipe p) {
    PResults presults;

    for (int i = 0; i < server_count; i++) {
        sp<IScheduleTest> service =
            IScheduleTest::getService("hwbinderService" + to_string(i), pass_through);
        ASSERT(service != nullptr);
        if (pass_through) {
            ASSERT(!service->isRemote());
        } else {
            ASSERT(service->isRemote());
        }
        services.push_back(service);
    }
    // tell main I'm init-ed
    p.signal();
    // wait for kick-off
    p.wait();

    // Client for each pair iterates here
    // each iterations contains exatcly 2 transactions
    for (int i = 0; i < iterations; i++) {
        Tick sta, end;
        // the target is paired to make it easier to diagnose
        int target = num;

        // 1. transaction by fifo thread
        threadTransaction(target, &presults);
        threadDumpPri("other-caller");

        uint32_t call_sta = (threadGetPri() << 16) | sched_getcpu();
        sp<IScheduleTest> service = services[target];
        // 2. transaction by other thread
        asm("" ::: "memory");
        sta = tickNow();
        uint32_t ret = service->send(verbose, call_sta);
        end = tickNow();
        asm("" ::: "memory");
        presults.other.addTime(tickNano(sta, end));
        presults.no_inherent += (ret >> 16) & 0xffff;
        presults.no_sync += ret & 0xffff;
    }
    // tell main i'm done
    p.signal();

    // wait to send result
    p.wait();
    p.send(presults);

    // wait for kill
    p.wait();
    exit(0);
}

static Pipe makeClientProcess(int num, int iterations, int no_pair) {
    auto pipe_pair = Pipe::createPipePair();
    pid_t pid = fork();
    if (pid) {
        // parent
        return move(get<0>(pipe_pair));
    } else {
        // child
        threadDumpPri("client");
        clientFx(num, no_pair, iterations, move(get<1>(pipe_pair)));
        // never get here
        ASSERT(0);
        return move(get<0>(pipe_pair));
    }
}

static void waitAll(vector<Pipe>& v) {
    for (size_t i = 0; i < v.size(); i++) {
        v[i].wait();
    }
}

static void signalAll(vector<Pipe>& v) {
    for (size_t i = 0; i < v.size(); i++) {
        v[i].signal();
    }
}

static void help() {
    cout << "usage:" << endl;
    cout << "-i 1              # number of iterations" << endl;
    cout << "-pair 4           # number of process pairs" << endl;
    cout << "-deadline_us 2500 # deadline in us" << endl;
    cout << "-v                # debug" << endl;
    cout << "-trace            # halt the trace on a dealine hit" << endl;
    exit(0);
}
// This test is modified from frameworks/native/libs/binder/tests/sch-dbg.cpp
// The difference is sch-dbg tests binder transaction and this one test
// HwBinder transaction.
// Test
//
//  libhwbinder_latency -i 1 -v
//  libhwbinder_latency -i 10000 -pair 4
//  atrace --async_start -c sched idle workq binder_driver freq && \
//    libhwbinder_latency -i 10000 -pair 4 -trace
int main(int argc, char** argv) {
    setenv("TREBLE_TESTING_OVERRIDE", "true", true);

    vector<Pipe> client_pipes;
    vector<Pipe> service_pipes;

    for (int i = 1; i < argc; i++) {
        if (string(argv[i]) == "-h") {
            help();
        }
        if (string(argv[i]) == "-m") {
            if (!strcmp(argv[i + 1], "PASSTHROUGH")) {
                pass_through = true;
            }
            i++;
            continue;
        }
        if (string(argv[i]) == "-i") {
            iterations = atoi(argv[i + 1]);
            i++;
            continue;
        }
        if (string(argv[i]) == "-pair" || string(argv[i]) == "-w") {
            no_pair = atoi(argv[i + 1]);
            i++;
            continue;
        }
        if (string(argv[i]) == "-deadline_us") {
            deadline_us = atoi(argv[i + 1]);
            i++;
            continue;
        }
        if (string(argv[i]) == "-v") {
            verbose = 1;
        }
        // The -trace argument is used like that:
        //
        // First start trace with atrace command as usual
        // >atrace --async_start sched freq
        //
        // then use the -trace arguments like
        // -trace -deadline_us 2500
        //
        // This makes the program to stop trace once it detects a transaction
        // duration over the deadline. By writing '0' to
        // /sys/kernel/debug/tracing and halt the process. The tracelog is
        // then available on /sys/kernel/debug/trace
        if (string(argv[i]) == "-trace") {
            is_tracing = 1;
        }
    }
    if (!pass_through) {
        // Create services.
        for (int i = 0; i < no_pair; i++) {
            service_pipes.push_back(makeServiceProces("hwbinderService" + to_string(i)));
        }
        // Wait until all services are up.
        waitAll(service_pipes);
    }
    if (is_tracing && !traceIsOn()) {
        cout << "trace is not running" << endl;
        cout << "check " << trace_path + "/tracing_on" << endl;
        cout << "use atrace --async_start first" << endl;
        exit(EXIT_FAILURE);
    }
    threadDumpPri("main");
    cout << "{" << endl;
    cout << "\"cfg\":{\"pair\":" << (no_pair) << ",\"iterations\":" << iterations
         << ",\"deadline_us\":" << deadline_us << ",\"passthrough\":" << pass_through << "},"
         << endl;

    // the main process fork 2 processes for each pairs
    // 1 server + 1 client
    // each has a pipe to communicate with
    for (int i = 0; i < no_pair; i++) {
        client_pipes.push_back(makeClientProcess(i, iterations, no_pair));
    }
    // wait client to init
    waitAll(client_pipes);

    // kick off clients
    signalAll(client_pipes);

    // wait client to finished
    waitAll(client_pipes);

    // collect all results
    signalAll(client_pipes);
    PResults total, presults[no_pair];
    for (int i = 0; i < no_pair; i++) {
        client_pipes[i].recv(presults[i]);
        total = PResults::combine(total, presults[i]);
    }
    total.dump("ALL");
    for (int i = 0; i < no_pair; i++) {
        presults[i].dump("P" + to_string(i));
    }

    if (!pass_through) {
        signalAll(service_pipes);
    }
    int no_inherent = 0;
    for (int i = 0; i < no_pair; i++) {
        no_inherent += presults[i].no_inherent;
    }
    cout << "\"inheritance\": " << (no_inherent == 0 ? "\"PASS\"" : "\"FAIL\"") << endl;
    cout << "}" << endl;
    // kill all
    signalAll(client_pipes);
    return -no_inherent;
}
