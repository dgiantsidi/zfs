#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/time.h>

#define NUM_THREADS 64
#define WRITE_SIZE 4096
#define NUM_WRITES 200000

double thread_times[NUM_THREADS];

void* write_worker(void* arg) {
    int thread_id = *(int*)arg;
    char filename[64];
    snprintf(filename, sizeof(filename), "/tpool/thread_output_%d.dat", thread_id);

    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC | O_SYNC, 0644);
    if (fd < 0) {
        perror("open");
        pthread_exit(NULL);
    }

    char* buffer = malloc(WRITE_SIZE);
    memset(buffer, 'A' + thread_id, WRITE_SIZE);

    struct timeval start, end;
    gettimeofday(&start, NULL);

    for (int i = 0; i < NUM_WRITES; ++i) {
        if (write(fd, buffer, WRITE_SIZE) != WRITE_SIZE) {
            perror("write");
            break;
        }
    }

    gettimeofday(&end, NULL);
    close(fd);
    free(buffer);

    double elapsed = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1e6;
    thread_times[thread_id] = elapsed;

    printf("Thread %d completed %d writes in %.4f seconds\n",  thread_id, NUM_WRITES, elapsed);
    pthread_exit(NULL);
}

int main() {
    pthread_t threads[NUM_THREADS];
    int thread_ids[NUM_THREADS];

    struct timeval total_start, total_end;
    gettimeofday(&total_start, NULL);

    for (int i = 0; i < NUM_THREADS; ++i) {
        thread_ids[i] = i;
        pthread_create(&threads[i], NULL, write_worker, &thread_ids[i]);
    }

    for (int i = 0; i < NUM_THREADS; ++i) {
        pthread_join(threads[i], NULL);
    }

    gettimeofday(&total_end, NULL);
    double total_elapsed = (total_end.tv_sec - total_start.tv_sec) +
                           (total_end.tv_usec - total_start.tv_usec) / 1e6;

    printf("\nBenchmark Results:\n");
    double sum = 0.0;
    double avg_latency = 0.0;
    for (int i = 0; i < NUM_THREADS; ++i) {
        double latency = 1000.0*(thread_times[i]*1.0 / (1.0 * NUM_WRITES)); // make it ms
        printf("Thread %d: %.4f seconds, latency=%.4f ms\n", i, thread_times[i], latency);
        sum += thread_times[i];
        avg_latency += latency;
    }
    printf("Total time across all threads: %.4f seconds, avg_latency= %.4f ms\n", sum, avg_latency / (1.0 * NUM_THREADS));
    printf("Wall-clock time: %.4f seconds\n", total_elapsed);

    return 0;
}
