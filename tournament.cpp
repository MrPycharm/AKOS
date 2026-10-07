#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <unistd.h>

#include <random>

// Gestures
enum Gesture
{
    ROCK = 0,
    SCISSORS = 1,
    PAPER = 2,
    NUM_GESTURES = 3
};

static const char* gesture_names[] = {"Rock", "Scissors", "Paper"};

// Student
struct Student
{
    int id;
    char name[50];
    int score;
};

// Define thw winner: 1 - first player, -1 - second, 0 - draw
int determine_winner(Gesture g1, Gesture g2)
{
    if(g1 == g2)
        return 0;
    if((g1 == ROCK && g2 == SCISSORS) || (g1 == SCISSORS && g2 == PAPER) ||
       (g1 == PAPER && g2 == ROCK))
    {
        return 1;
    }
    return -1;
}

// Printing table
void print_table(const Student* students, int n)
{
    std::printf("\nCurrent table:\n");
    std::printf("| ID |       Name      | Points  |\n");
    for(int idx = 0; idx < n; idx++)
    {
        std::printf("| %2d |    %-14s   | %5d |\n", students[idx].id,
                    students[idx].name, students[idx].score);
    }
}

int main(int argc, char* argv[])
{
    int n = 4;
    unsigned seed = 42u;
    int delay = 0;

    // argv[1] - amount of students
    // argv[2] - seed
    // argv[3] - delay
    if(argc > 1)
        n = std::atoi(argv[1]);
    if(argc > 2)
        seed = static_cast<unsigned>(std::stoul(argv[2]));
    if(argc > 3)
        delay = std::atoi(argv[3]);

    if(n < 2)
    {
        std::fprintf(stderr, "Error: number of students must be >= 2\n");
        return 1;
    }
    if(delay < 0)
    {
        std::fprintf(stderr, "Error: delay cannot be negative\n");
        return 1;
    }

    std::mt19937 gen(seed);
    std::uniform_int_distribution<int> dist(0, NUM_GESTURES - 1);

    auto* students = static_cast<Student*>(std::malloc(n * sizeof(Student)));
    if(!students)
    {
        std::perror("malloc");
        return 1;
    }

    for(int idx = 0; idx < n; idx++)
    {
        students[idx].id = idx + 1;
        std::snprintf(students[idx].name, sizeof(students[idx].name),
                      "Student %d", idx + 1);
        students[idx].score = 0;
    }

    std::printf("Tournament 'Rock, Scissors, Paper'\n");
    std::printf("Participants: %d | Seed: %u | Delay: %d sec\n", n, seed,
                delay);
    print_table(students, n);

    const int total_matches = n * (n - 1) / 2;
    int match_count = 0;

    for(int idx = 0; idx < n; idx++)
    {
        for(int jdx = idx + 1; jdx < n; jdx++)
        {
            match_count++;
            std::printf("\nMatch %d of %d\n", match_count, total_matches);
            std::printf("Playing: %s and %s\n", students[idx].name,
                        students[jdx].name);

            Gesture g1 = static_cast<Gesture>(dist(gen));
            Gesture g2 = static_cast<Gesture>(dist(gen));

            std::printf("%s chooses: %s\n", students[idx].name,
                        gesture_names[g1]);
            std::printf("%s chooses: %s\n", students[jdx].name,
                        gesture_names[g2]);

            sleep(delay);

            std::printf("Reveal: %s versus %s\n", gesture_names[g1],
                        gesture_names[g2]);

            int winner = determine_winner(g1, g2);
            switch(winner)
            {
            case 1:
                students[idx].score += 2;
                std::printf("%s wins. +2 points.\n", students[idx].name);
                break;
            case -1:
                students[jdx].score += 2;
                std::printf("%s wins. +2 points.\n", students[jdx].name);
                break;
            default:
                students[idx].score += 1;
                students[jdx].score += 1;
                std::printf("Draw. 1 point each.\n");
                break;
            }

            print_table(students, n);
        }
    }

    for(int idx = 0; idx < n - 1; idx++)
    {
        for(int jdx = idx + 1; jdx < n; jdx++)
        {
            if(students[idx].score < students[jdx].score)
            {
                Student tmp = students[idx];
                students[idx] = students[jdx];
                students[jdx] = tmp;
            }
        }
    }

    std::printf("\n FINAL RESULTS \n");
    std::printf("Place | ID |    Name    | Points\n");
    for(int idx = 0; idx < n; idx++)
    {
        std::printf("%5d | %2d | %-14s | %4d\n", idx + 1, students[idx].id,
                    students[idx].name, students[idx].score);
    }

    std::free(students);
    return 0;
}
