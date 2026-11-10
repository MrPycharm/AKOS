#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAXS 100  // amount of students

enum
{
    ROCK,
    SCISSORS,
    PAPER,
    NGEST
};
const char* gname[] = {"Rock", "Scissors", "Paper"};

enum
{
    RANDOM,
    CYCLE,
    FIXED
};  // fixed - choose by number of stud

typedef struct
{
    int id;
    char name[32];
    int score;
    int strategy;
    int delay;
    int moves;
    int gesture;
    unsigned rnd;

    sem_t start;  // st can do a move
    sem_t ready;  // gesture is chose
} Student;

typedef struct
{
    Student st[MAXS];
    int n;
    int stop;
} Shared;

typedef struct
{
    int a, b;  // my players
} Pair;

volatile sig_atomic_t stop_requested = 0;

static void on_signal(int sig)
{
    (void)sig;
    stop_requested = 1;
}

static int who_wins(int a, int b)
{
    if(a == b)
        return 0;

    if((a == ROCK) && (b == SCISSORS))
        return 1;
    if((a == SCISSORS) && (b == PAPER))
        return 1;
    if((a == PAPER) && (b == ROCK))
        return 1;

    return -1;
}

static void print_table(Shared* sh)
{
    printf("\nCurrent table:\n");
    for(int i = 0; i < sh->n; i++)
    {
        printf("  %2d) %-10s %3d\n", sh->st[i].id, sh->st[i].name,
               sh->st[i].score);
    }
}

static void print_final(Shared* sh, int tie_rule)
{
    int idx[MAXS];

    for(int i = 0; i < sh->n; i++)
        idx[i] = i;

    for(int i = 0; i < sh->n; i++)
    {
        for(int j = i + 1; j < sh->n; j++)
        {
            if(sh->st[idx[i]].score < sh->st[idx[j]].score)
            {
                int t = idx[i];
                idx[i] = idx[j];
                idx[j] = t;
            }
        }
    }

    printf("\nFinal ranking:\n");

    int place = 1;
    for(int i = 0; i < sh->n; i++)
    {
        if(tie_rule == 0 && i > 0 &&
           sh->st[idx[i]].score != sh->st[idx[i - 1]].score)
        {
            place = i + 1;
        }
        if(tie_rule != 0)
            place = i + 1;

        printf("  %2d place  %-10s %d points\n", place, sh->st[idx[i]].name,
               sh->st[idx[i]].score);
    }
}

static int wait_ready(sem_t* sem, Shared* sh)
{
    while(sem_wait(sem) == -1)
    {
        if(errno == EINTR)
        {
            if(stop_requested || sh->stop)
                return -1;
            continue;
        }
        perror("sem_wait");
        return -1;
    }
    return 0;
}

static void student_process(Shared* sh, int idx)
{
    Student* s = &sh->st[idx];

    signal(SIGINT, SIG_IGN);
    signal(SIGTERM, SIG_IGN);

    for(;;)
    {
        while(sem_wait(&s->start) == -1)
        {
            if(errno == EINTR)
                continue;
            _exit(1);
        }

        if(sh->stop)
            break;

        for(int k = 0; k < s->delay && !sh->stop; k++)
            sleep(1);

        if(sh->stop)
            break;

        if(s->strategy == RANDOM)
            s->gesture = rand_r(&s->rnd) % NGEST;
        else if(s->strategy == CYCLE)
            s->gesture = s->moves % NGEST;
        else
            s->gesture = (s->id - 1) % NGEST;

        s->moves++;

        sem_post(&s->ready);
    }

    _exit(0);
}

static void cleanup(Shared* sh, int n, pid_t* pids, int created, int shm_fd,
                    const char* shm_name, int shm_created, int sem_ok)
{
    if(sh != NULL)
    {
        sh->stop = 1;

        for(int i = 0; i < n; i++)
            sem_post(&sh->st[i].start);

        for(int i = 0; i < created; i++)
            waitpid(pids[i], NULL, 0);

        if(sem_ok)
        {
            for(int i = 0; i < n; i++)
            {
                sem_destroy(&sh->st[i].start);
                sem_destroy(&sh->st[i].ready);
            }
        }

        munmap(sh, sizeof(Shared));
    }

    if(shm_fd >= 0)
        close(shm_fd);

    if(shm_created)
        shm_unlink(shm_name);
}

int main(int argc, char** argv)
{
    int n = 4;
    unsigned seed = 42;
    int strategy = RANDOM;
    int par = 1;
    int delay = 0;
    int tie_rule = 0;

    if(argc == 7)
    {
        n = atoi(argv[1]);
        seed = (unsigned)atoi(argv[2]);
        strategy = atoi(argv[3]);
        par = atoi(argv[4]);
        delay = atoi(argv[5]);
        tie_rule = atoi(argv[6]);
    }
    else if(argc != 1)
    {
        printf("Usage: %s [n seed strategy par delay tie_rule]\n", argv[0]);
        return 1;
    }

    if(n < 2 || n > MAXS)
    {
        printf("Number of students must be between 2 and %d\n", MAXS);
        return 1;
    }

    if(strategy < 0 || strategy > 2)
        strategy = RANDOM;

    if(par < 1)
        par = 1;

    if(delay < 0)
        delay = 0;

    if((tie_rule < 0) || (tie_rule > 1))
        tie_rule = 0;

    setvbuf(stdout, NULL, _IONBF, 0);

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    char shm_name[64];
    snprintf(shm_name, sizeof(shm_name), "/rsp_tour_%d", getpid());

    int shm_fd = -1;
    Shared* sh = NULL;
    int shm_created = 0;
    int sem_ok = 0;
    pid_t pids[MAXS];
    int created = 0;

    shm_fd = shm_open(shm_name, O_CREAT | O_EXCL | O_RDWR, 0600);
    if(shm_fd < 0)
    {
        perror("shm_open");
        return 1;
    }
    shm_created = 1;

    if(ftruncate(shm_fd, sizeof(Shared)) < 0)
    {
        perror("ftruncate");
        cleanup(NULL, n, pids, created, shm_fd, shm_name, shm_created, sem_ok);
        return 1;
    }

    sh = mmap(NULL, sizeof(Shared), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd,
              0);
    if(sh == MAP_FAILED)
    {
        perror("mmap");
        sh = NULL;
        cleanup(NULL, n, pids, created, shm_fd, shm_name, shm_created, sem_ok);
        return 1;
    }

    memset(sh, 0, sizeof(Shared));
    sh->n = n;
    sh->stop = 0;

    for(int i = 0; i < n; i++)
    {
        sh->st[i].id = i + 1;
        snprintf(sh->st[i].name, sizeof(sh->st[i].name), "Student%d", i + 1);
        sh->st[i].score = 0;
        sh->st[i].strategy = strategy;
        sh->st[i].delay = delay;
        sh->st[i].moves = 0;
        sh->st[i].gesture = ROCK;
        sh->st[i].rnd = seed + i;

        if(sem_init(&sh->st[i].start, 1, 0) != 0)
        {
            perror("sem_init start");
            cleanup(sh, n, pids, created, shm_fd, shm_name, shm_created,
                    sem_ok);
            return 1;
        }

        if(sem_init(&sh->st[i].ready, 1, 0) != 0)
        {
            perror("sem_init ready");
            cleanup(sh, n, pids, created, shm_fd, shm_name, shm_created,
                    sem_ok);
            return 1;
        }
    }
    sem_ok = 1;

    printf("Tournament: rock, scissors, paper\n");
    printf("Students: %d, seed=%u, strategy=%d, pairs simultaneously=%d, "
           "delay=%d\n",
           n, seed, strategy, par, delay);
    print_table(sh);

    for(int i = 0; i < n; i++)
    {
        pid_t pid = fork();

        if(pid < 0)
        {
            perror("fork");
            cleanup(sh, n, pids, created, shm_fd, shm_name, shm_created,
                    sem_ok);
            return 1;
        }

        if(pid == 0)
            student_process(sh, i);

        pids[i] = pid;
        created++;
    }

    int played[MAXS][MAXS] = {{0}};
    int total = n * (n - 1) / 2;
    int done = 0;

    while(done < total && !stop_requested && !sh->stop)
    {
        Pair m[MAXS];
        int busy[MAXS] = {0};
        int cnt = 0;

        for(int i = 0; i < n && cnt < par; i++)
        {
            for(int j = i + 1; j < n && cnt < par; j++)
            {
                if(!played[i][j] && !busy[i] && !busy[j])
                {
                    m[cnt].a = i;
                    m[cnt].b = j;
                    cnt++;

                    busy[i] = busy[j] = 1;
                    played[i][j] = 1;

                    printf("\nPair: %s - %s\n", sh->st[i].name, sh->st[j].name);
                }
            }
        }

        if(cnt == 0)
            break;

        for(int i = 0; i < cnt; i++)
        {
            sem_post(&sh->st[m[i].a].start);
            sem_post(&sh->st[m[i].b].start);
        }

        int interrupted = 0;

        for(int i = 0; i < cnt; i++)
        {
            int a = m[i].a;
            int b = m[i].b;

            if(wait_ready(&sh->st[a].ready, sh) != 0)
            {
                interrupted = 1;
                break;
            }

            if(wait_ready(&sh->st[b].ready, sh) != 0)
            {
                interrupted = 1;
                break;
            }

            done++;

            printf("\nGame %d of %d\n", done, total);
            printf("  %s: %s\n", sh->st[a].name, gname[sh->st[a].gesture]);
            printf("  %s: %s\n", sh->st[b].name, gname[sh->st[b].gesture]);

            int w = who_wins(sh->st[a].gesture, sh->st[b].gesture);

            if(w == 1)
            {
                sh->st[a].score += 2;
                printf("  %s wins (+2)\n", sh->st[a].name);
            }
            else if(w == -1)
            {
                sh->st[b].score += 2;
                printf("  %s wins (+2)\n", sh->st[b].name);
            }
            else
            {
                sh->st[a].score++;
                sh->st[b].score++;
                printf("  Draw (+1 each)\n");
            }

            print_table(sh);
        }

        if(interrupted)
            break;
    }

    if((stop_requested) || (sh->stop))
    {
        printf("\nTournament interrupted.\n");
    }
    else
    {
        print_final(sh, tie_rule);
        printf("\nGames played: %d of %d\n", done, total);
    }

    cleanup(sh, n, pids, created, shm_fd, shm_name, shm_created, sem_ok);

    return 0;
}
