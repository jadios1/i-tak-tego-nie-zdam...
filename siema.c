#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

void msleep(int ms) {
    struct timespec ts = { ms/1000, (ms%1000)*1000000L };
    nanosleep(&ts, NULL);
}
void strip_nl(char *s) { s[strcspn(s, "\n")] = '\0'; }

// strtok_r — zmienna liczba tokenow, bezpieczne w watkach
// char *save, buf[256]; strcpy(buf, raw); strip_nl(buf);
// char *type = strtok_r(buf, ";", &save);
// char *tok;
// while ((tok = strtok_r(NULL, ";", &save))) { ... }

int tokenize(char *buf, const char *delim, char **out, int max) {
    char *save; int n = 0;
    for (char *t = strtok_r(buf, delim, &save); t && n < max; t = strtok_r(NULL, delim, &save))
        out[n++] = t;
    return n;
}

// CIRCULAR BUFFER — sem blokuje pop, mutex chroni indeksy
// push: jesli pelny -> drop
// pop:  sem_wait -> mutex -> wez element

#define MAX_QUEUE 10
typedef struct { int data; } item_t;

typedef struct {
    item_t          buf[MAX_QUEUE];
    pthread_mutex_t mutex;
    sem_t           sem;
    int             head, tail, count;
} queue_t;

void queue_init(queue_t *q) {
    pthread_mutex_init(&q->mutex, NULL);
    sem_init(&q->sem, 0, 0);
    q->head = q->tail = q->count = 0;
}
void queue_destroy(queue_t *q) {
    pthread_mutex_destroy(&q->mutex);
    sem_destroy(&q->sem);
}
void queue_push(queue_t *q, item_t item) {
    pthread_mutex_lock(&q->mutex);
    if (q->count < MAX_QUEUE) {
        q->buf[q->tail] = item;
        q->tail = (q->tail + 1) % MAX_QUEUE;
        q->count++;
        sem_post(&q->sem);
    } else {
        printf("[ERROR] queue full, dropping\n");
    }
    pthread_mutex_unlock(&q->mutex);
}
item_t queue_pop(queue_t *q) {
    sem_wait(&q->sem);
    pthread_mutex_lock(&q->mutex);
    item_t item = q->buf[q->head];
    q->head = (q->head + 1) % MAX_QUEUE;
    q->count--;
    pthread_mutex_unlock(&q->mutex);
    return item;
}

// SORTED LINKED LIST — mailbox z priorytetem
// head = najwyzszy priorytet (najnizszy numer), FIFO w obrebie tej samej wartosci

typedef struct sl_node { int priority; int data; struct sl_node *next; } sl_node_t;
typedef struct { sl_node_t *head; int count; pthread_mutex_t mtx; } sl_t;

void sl_init(sl_t *l) {
    l->head = NULL; l->count = 0;
    pthread_mutex_init(&l->mtx, NULL);
}
void sl_insert(sl_t *l, int priority, int data) {
    sl_node_t *n = malloc(sizeof(*n));
    n->priority = priority; n->data = data; n->next = NULL;
    pthread_mutex_lock(&l->mtx);
    sl_node_t **c = &l->head;
    while (*c && (*c)->priority <= priority) c = &(*c)->next;
    n->next = *c; *c = n; l->count++;
    pthread_mutex_unlock(&l->mtx);
}
int sl_pop(sl_t *l, int *out) {
    pthread_mutex_lock(&l->mtx);
    if (!l->head) { pthread_mutex_unlock(&l->mtx); return -1; }
    sl_node_t *n = l->head;
    *out = n->data; l->head = n->next; l->count--;
    free(n);
    pthread_mutex_unlock(&l->mtx);
    return 0;
}
void sl_clear(sl_t *l) {
    pthread_mutex_lock(&l->mtx);
    for (sl_node_t *c = l->head, *nx; c; c = nx) { nx = c->next; free(c); }
    l->head = NULL; l->count = 0;
    pthread_mutex_unlock(&l->mtx);
}

// SEMAPHORE jako token bucket
// sem_init(&s, 0, N)
// sem_trywait(&s)  — nieblokujace, -1 jesli brak
// sem_post(&s)     — oddaj token

// SYGNALY — TODO: wklej wzorzec z labow

// UDP
int udp_bind(int port) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) { perror("socket"); exit(1); }
    struct sockaddr_in a = {
        .sin_family      = AF_INET,
        .sin_port        = htons(port),
        .sin_addr.s_addr = INADDR_ANY
    };
    if (bind(fd, (struct sockaddr*)&a, sizeof(a))) { perror("bind"); exit(1); }
    return fd;
}

// WORKER THREAD
// void *worker(void *arg) {
//     gamestate_t *game = arg;
//     while (1) {
//         item_t item = queue_pop(&game->queue);
//         msleep(DELAY_MS);
//         pthread_mutex_lock(&game->mutex);
//         // logika
//         pthread_mutex_unlock(&game->mutex);
//     }
//     return NULL;
// }

// JUDGE THREAD
// void *judge(void *arg) {
//     gamestate_t *game = arg;
//     while (1) {
//         sleep(INTERVAL_S);
//         pthread_mutex_lock(&game->mutex);
//         // modyfikuj stan, jeden mutex na raz
//         pthread_mutex_unlock(&game->mutex);
//     }
//     return NULL;
// }

int main(int argc, char *argv[]) {
    if (argc < 2) { fprintf(stderr, "usage: %s <port>\n", argv[0]); return 1; }

    int fd = udp_bind(atoi(argv[1]));
    queue_t q; queue_init(&q);

    char buf[512];
    struct sockaddr_in src; socklen_t slen = sizeof(src);

    while (1) {
        int r = recvfrom(fd, buf, sizeof(buf)-1, 0, (struct sockaddr*)&src, &slen);
        if (r <= 0) continue;
        buf[r] = '\0'; strip_nl(buf);

        char tmp[512]; strcpy(tmp, buf);
        char *fields[16];
        int nf = tokenize(tmp, ";", fields, 16);
        if (!nf) continue;
        // fields[0] = type, fields[1..nf-1] = reszta
        (void)nf;
    }

    queue_destroy(&q);
    close(fd);
    return 0;
}





//POMOC DO LINKED LISTS JEZELI BEDA :(

// zamiast int data, twoj typ:
typedef struct {
    char sender[32];
    char contents[64];
    int  priority;
} parcel_t;

typedef struct sl_node {
    int        priority;
    parcel_t   data;      // <-- twoj typ tutaj
    struct sl_node *next;
} sl_node_t;

sl_t mailbox;
sl_init(&mailbox);

// wstawiasz paczke
parcel_t p = { .sender = "alice", .contents = "hello", .priority = 2 };
sl_insert(&mailbox, 2, p);  // wchodzi za wszystkimi z priority <= 2

parcel_t p2 = { .sender = "bob", .contents = "urgent", .priority = 1 };
sl_insert(&mailbox, 1, p2); // wchodzi na head bo priority 1 < 2



// pobierasz najwazniejsza (head)
parcel_t out;
sl_pop(&mailbox, &out);  // dostaniesz boba, priority 1



// jesli masz sem jako token bucket
pthread_mutex_lock(&mailbox.mtx);
for (sl_node_t *c = mailbox.head, *nx; c; c = nx) {
    nx = c->next;
    sem_post(&sem);   // oddaj token za kazda paczke
    free(c);
}
mailbox.head = NULL; mailbox.count = 0;
pthread_mutex_unlock(&mailbox.mtx);
// albo po prostu sl_clear jesli nie masz sema
