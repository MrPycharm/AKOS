#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#define MAXS 100
#define MAX_OBS 50
#define PORT 8080
#define BUF_SIZE 1024

int observer_socks[MAX_OBS];
int observer_count = 0;
int player_socks[MAXS];
int total_students = 0;
volatile sig_atomic_t stop_requested = 0;

void on_signal(int sig)
{
    (void)sig;
    stop_requested = 1;
}

void broadcast_to_observers(const char* msg)
{
    for(int i = 0; i < observer_count; i++)
        if(observer_socks[i] != -1)
            send(observer_socks[i], msg, strlen(msg), MSG_NOSIGNAL);
}

void remove_observer(int idx)
{
    close(observer_socks[idx]);
    observer_socks[idx] = observer_socks[observer_count - 1];
    observer_count--;
}

int who_wins(int a, int b)
{
    if(a == b)
        return 0;
    if((a == 0 && b == 1) || (a == 1 && b == 2) || (a == 2 && b == 0))
        return 1;
    return -1;
}

const char* gesture_name(int g)
{
    const char* names[] = {"Rock", "Scissors", "Paper"};
    return names[g];
}

void print_and_broadcast(const char* fmt, ...)
{
    char buf[BUF_SIZE];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    printf("%s", buf);
    broadcast_to_observers(buf);
}

void run_server(int port, int n, int seed, int strategy, int par, int delay,
                int tie_rule)
{
    total_students = n;
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    bind(server_fd, (struct sockaddr*)&address, sizeof(address));
    listen(server_fd, 10);

    for(int i = 0; i < MAXS; i++)
        player_socks[i] = -1;
    for(int i = 0; i < MAX_OBS; i++)
        observer_socks[i] = -1;

    printf("Координатор запущен на порту %d. Ожидание игроков\n", port);
    broadcast_to_observers("Сервер запущен. Ожидание игроков\n");

    int scores[MAXS] = {0};
    int played[MAXS][MAXS] = {0};
    int total_games = n * (n - 1) / 2;
    int done = 0;

    while(done < total_games && !stop_requested)
    {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(server_fd, &readfds);
        int max_fd = server_fd;

        for(int i = 0; i < n; i++)
        {
            if(player_socks[i] != -1)
            {
                FD_SET(player_socks[i], &readfds);
                if(player_socks[i] > max_fd)
                    max_fd = player_socks[i];
            }
        }
        for(int i = 0; i < observer_count; i++)
        {
            if(observer_socks[i] != -1)
            {
                FD_SET(observer_socks[i], &readfds);
                if(observer_socks[i] > max_fd)
                    max_fd = observer_socks[i];
            }
        }

        struct timeval tv = {1, 0};
        int activity = select(max_fd + 1, &readfds, NULL, NULL, &tv);

        if(activity < 0 && errno != EINTR)
        {
            perror("select");
            break;
        }

        if(FD_ISSET(server_fd, &readfds))
        {
            int new_sock = accept(server_fd, NULL, NULL);
            if(new_sock >= 0)
            {
                char role_buf[32] = {0};
                recv(new_sock, role_buf, sizeof(role_buf), 0);

                if(strncmp(role_buf, "OBSERVER", 8) == 0)
                {
                    if(observer_count < MAX_OBS)
                    {
                        observer_socks[observer_count++] = new_sock;
                        send(new_sock, "Добро пожаловать, Наблюдатель!\n", 32,
                             0);
                        printf("Подключился наблюдатель.\n");
                    }
                    else
                    {
                        close(new_sock);
                    }
                }
                else if(strncmp(role_buf, "PLAYER", 6) == 0)
                {
                    int id = atoi(role_buf + 7);
                    if(id >= 0 && id < n)
                    {
                        player_socks[id] = new_sock;
                        char init_msg[100];
                        sprintf(init_msg, "INIT %d %d %d %d\n", id, strategy,
                                seed, delay);
                        send(new_sock, init_msg, strlen(init_msg), 0);
                        printf("Подключился игрок %d\n", id);
                    }
                }
                else
                {
                    close(new_sock);
                }
            }
        }

        for(int i = observer_count - 1; i >= 0; i--)
        {
            if(FD_ISSET(observer_socks[i], &readfds))
            {
                char buf[10];
                int res = recv(observer_socks[i], buf, sizeof(buf), 0);
                if(res <= 0)
                {
                    printf("Наблюдатель отключился.\n");
                    remove_observer(i);
                }
            }
        }

        int all_connected = 1;
        for(int i = 0; i < n; i++)
            if(player_socks[i] == -1)
                all_connected = 0;

        if(all_connected && done == 0)
        {
            printf("\nВсе игроки на месте. Турнир начинается!\n");
            broadcast_to_observers("\nТУРНИР НАЧАЛСЯ\n");
        }

        if(all_connected)
        {
            int pairs = 0;
            int p_a[MAXS], p_b[MAXS];
            int busy[MAXS] = {0};

            for(int i = 0; i < n && pairs < par; i++)
            {
                for(int j = i + 1; j < n && pairs < par; j++)
                {
                    if(!played[i][j] && !busy[i] && !busy[j])
                    {
                        p_a[pairs] = i;
                        p_b[pairs] = j;
                        busy[i] = busy[j] = 1;
                        played[i][j] = 1;
                        pairs++;
                    }
                }
            }

            if(pairs > 0)
            {
                for(int k = 0; k < pairs; k++)
                {
                    send(player_socks[p_a[k]], "PLAY\n", 5, 0);
                    send(player_socks[p_b[k]], "PLAY\n", 5, 0);
                    print_and_broadcast("\nПара: Студент %d - Студент %d\n",
                                        p_a[k], p_b[k]);
                }

                int moves_received[MAXS] = {0};
                int moves[MAXS] = {0};
                int pending = pairs * 2;

                while(pending > 0 && !stop_requested)
                {
                    fd_set move_fds;
                    FD_ZERO(&move_fds);
                    int max_move_fd = 0;
                    for(int k = 0; k < pairs; k++)
                    {
                        int a = p_a[k], b = p_b[k];
                        if(!moves_received[a])
                        {
                            FD_SET(player_socks[a], &move_fds);
                            if(player_socks[a] > max_move_fd)
                                max_move_fd = player_socks[a];
                        }
                        if(!moves_received[b])
                        {
                            FD_SET(player_socks[b], &move_fds);
                            if(player_socks[b] > max_move_fd)
                                max_move_fd = player_socks[b];
                        }
                    }

                    struct timeval tv_move = {1, 0};
                    select(max_move_fd + 1, &move_fds, NULL, NULL, &tv_move);

                    for(int k = 0; k < pairs; k++)
                    {
                        int a = p_a[k], b = p_b[k];
                        if(!moves_received[a] &&
                           FD_ISSET(player_socks[a], &move_fds))
                        {
                            char mbuf[32] = {0};
                            recv(player_socks[a], mbuf, sizeof(mbuf), 0);
                            moves[a] = atoi(mbuf);
                            moves_received[a] = 1;
                            pending--;
                        }
                        if(!moves_received[b] &&
                           FD_ISSET(player_socks[b], &move_fds))
                        {
                            char mbuf[32] = {0};
                            recv(player_socks[b], mbuf, sizeof(mbuf), 0);
                            moves[b] = atoi(mbuf);
                            moves_received[b] = 1;
                            pending--;
                        }
                    }
                }

                if(stop_requested)
                    break;

                for(int k = 0; k < pairs; k++)
                {
                    int a = p_a[k], b = p_b[k];
                    int g_a = moves[a], g_b = moves[b];
                    int w = who_wins(g_a, g_b);
                    int pts_a = 0, pts_b = 0;

                    print_and_broadcast(
                        "Раскрытие: Студент %d (%s) vs Студент %d (%s). ", a,
                        gesture_name(g_a), b, gesture_name(g_b));

                    if(w == 1)
                    {
                        scores[a] += 2;
                        pts_a = 2;
                        print_and_broadcast("Победил Студент %d!\n", a);
                    }
                    else if(w == -1)
                    {
                        scores[b] += 2;
                        pts_b = 2;
                        print_and_broadcast("Победил Студент %d!\n", b);
                    }
                    else
                    {
                        scores[a] += 1;
                        scores[b] += 1;
                        pts_a = 1;
                        pts_b = 1;
                        print_and_broadcast("Ничья!\n");
                    }

                    char res_a[100], res_b[100];
                    sprintf(res_a, "RESULT %d %d %d %d\n", g_a, g_b, pts_a,
                            scores[a]);
                    sprintf(res_b, "RESULT %d %d %d %d\n", g_b, g_a, pts_b,
                            scores[b]);
                    send(player_socks[a], res_a, strlen(res_a), 0);
                    send(player_socks[b], res_b, strlen(res_b), 0);
                }
                done += pairs;

                print_and_broadcast("\nТекущая таблица\n");
                for(int i = 0; i < n; i++)
                    print_and_broadcast("  Студент %2d: %d очков\n", i,
                                        scores[i]);
                print_and_broadcast("-----------------------\n");
            }
        }
    }

    if(stop_requested)
    {
        printf("\nСервер принудительно завершает работу. Оповещаем клиентов\n");
        for(int i = 0; i < n; i++)
            if(player_socks[i] != -1)
                send(player_socks[i], "QUIT\n", 5, 0);
        for(int i = 0; i < observer_count; i++)
            if(observer_socks[i] != -1)
                send(observer_socks[i], "QUIT\n", 5, 0);
    }
    else
    {
        print_and_broadcast("\nИТОГОВЫЙ РЕЙТИНГ\n");
        int idx[MAXS];
        for(int i = 0; i < n; i++)
            idx[i] = i;
        for(int i = 0; i < n; i++)
        {
            for(int j = i + 1; j < n; j++)
            {
                if(scores[idx[i]] < scores[idx[j]])
                {
                    int t = idx[i];
                    idx[i] = idx[j];
                    idx[j] = t;
                }
            }
        }
        int place = 1;
        for(int i = 0; i < n; i++)
        {
            if(tie_rule == 0 && i > 0 && scores[idx[i]] != scores[idx[i - 1]])
                place = i + 1;
            if(tie_rule != 0)
                place = i + 1;
            print_and_broadcast("%2d место: Студент %d (%d очков)\n", place,
                                idx[i], scores[idx[i]]);
        }

        for(int i = 0; i < n; i++)
            if(player_socks[i] != -1)
                send(player_socks[i], "QUIT\n", 5, 0);
        for(int i = 0; i < observer_count; i++)
            if(observer_socks[i] != -1)
                send(observer_socks[i], "QUIT\n", 5, 0);
    }

    for(int i = 0; i < n; i++)
        if(player_socks[i] != -1)
            close(player_socks[i]);
    for(int i = 0; i < observer_count; i++)
        if(observer_socks[i] != -1)
            close(observer_socks[i]);
    close(server_fd);
    printf("Сервер завершил работу.\n");
}

void run_player(const char* ip, int port, int id)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &serv_addr.sin_addr);

    if(connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("Ошибка подключения");
        return;
    }

    char role_msg[32];
    sprintf(role_msg, "PLAYER %d\n", id);
    send(sock, role_msg, strlen(role_msg), 0);

    char buf[BUF_SIZE];
    int strategy = 0, seed = 42, delay = 0;
    int moves = 0;

    while(1)
    {
        memset(buf, 0, sizeof(buf));
        int bytes = recv(sock, buf, sizeof(buf) - 1, 0);
        if(bytes <= 0)
            break;

        if(strncmp(buf, "INIT", 4) == 0)
        {
            int my_id;
            sscanf(buf, "INIT %d %d %d %d", &my_id, &strategy, &seed, &delay);
            srand(seed + id);
            printf("[Игрок %d] Готов к турниру (стратегия: %d, задержка: %d)\n",
                   my_id, strategy, delay);
        }
        else if(strncmp(buf, "PLAY", 4) == 0)
        {
            if(delay > 0)
                sleep(delay);
            int gesture = 0;
            if(strategy == 0)
                gesture = rand() % 3;
            else if(strategy == 1)
                gesture = moves % 3;
            else
                gesture = id % 3;
            moves++;

            char move_buf[16];
            sprintf(move_buf, "%d\n", gesture);
            send(sock, move_buf, strlen(move_buf), 0);
        }
        else if(strncmp(buf, "RESULT", 6) == 0)
        {
            int my_g, opp_g, pts, total;
            sscanf(buf, "RESULT %d %d %d %d", &my_g, &opp_g, &pts, &total);
            printf("[Игрок %d] Раскрытие: Мой %s vs Соперник %s. Очки за "
                   "партию: %d. Всего: %d\n",
                   id, gesture_name(my_g), gesture_name(opp_g), pts, total);
        }
        else if(strncmp(buf, "QUIT", 4) == 0)
        {
            break;
        }
    }
    close(sock);
    printf("[Игрок %d] Завершил работу.\n", id);
}

void run_observer(const char* ip, int port)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &serv_addr.sin_addr);

    if(connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("Ошибка подключения");
        return;
    }

    send(sock, "OBSERVER\n", 9, 0);
    char buf[BUF_SIZE];

    while(1)
    {
        memset(buf, 0, sizeof(buf));
        int bytes = recv(sock, buf, sizeof(buf) - 1, 0);
        if(bytes <= 0)
            break;
        printf("%s", buf);
    }
    close(sock);
    printf("\n[Наблюдатель] Отключен от сервера.\n");
}

int main(int argc, char* argv[])
{
    if(argc < 2)
    {
        printf("Использование:\n");
        printf("  %s server <port> <n> <seed> <strategy> <par> <delay> "
               "<tie_rule>\n",
               argv[0]);
        printf("  %s player <ip> <port> <id>\n", argv[0]);
        printf("  %s observer <ip> <port>\n", argv[0]);
        return 1;
    }

    if(strcmp(argv[1], "server") == 0)
    {
        if(argc != 9)
            return 1;
        signal(SIGINT, on_signal);
        signal(SIGTERM, on_signal);
        run_server(atoi(argv[2]), atoi(argv[3]), atoi(argv[4]), atoi(argv[5]),
                   atoi(argv[6]), atoi(argv[7]), atoi(argv[8]));
    }
    else if(strcmp(argv[1], "player") == 0)
    {
        if(argc != 5)
            return 1;
        run_player(argv[2], atoi(argv[3]), atoi(argv[4]));
    }
    else if(strcmp(argv[1], "observer") == 0)
    {
        if(argc != 4)
            return 1;
        run_observer(argv[2], atoi(argv[3]));
    }
    return 0;
}
