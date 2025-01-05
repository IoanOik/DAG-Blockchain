#include "peer.h"
#include <iostream>
#include <signal.h>
#include <sys/wait.h>
#define BACKLOG 1024

void sighandler(int signal);
void signalHandler(int signum, siginfo_t *info, void *context);

std::vector<Peer> peers;

int main(int argc, char const *argv[])
{

    if (argc != 4)
    {
        std::cout << "Execution template:  ./main.exe <file.txt> < peers quantity> <termination round>" << std::endl;
        exit(EXIT_SUCCESS);
    }
    // srand(time(NULL));
    //  signal(SIGABRT, sighandler);
    //  signal(SIGINT, sighandler);

    int num_peers = atoi(argv[2]);
    int termination_round = atoi(argv[3]);
    bool equal_sets = true;
    bool fail = false;
    std::string filename = argv[1];
    peers.reserve(num_peers);

    int counter = num_peers;
    int status;
    int current_status;
    pid_t child_exited;
    std::vector<pid_t> children;
    std::map<pid_t, int> exit_status_map;
    std::vector<std::string> dags;
    bool equal = true;
    int fd[2];
    time_t start;

    if (pipe(fd) != 0)
    {
        std::cout << "Pipe error: " << strerror(errno) << std::endl;
        exit(EXIT_FAILURE);
    }
    else
    {
        std::cout << "Pipe created successfuly" << std::endl;
    }

    for (int k = 0; k < num_peers; k++)
    {
        pid_t child = fork();

        if (child < 0)
        {
            printf("Fork failed!\n");
            exit(EXIT_FAILURE);
        }
        else if (child == 0)
        {
            /* child process */
            setpgid(getpid(), getppid());

            // if (k <= 6)
            //     fail = true;

            Peer p(AF_INET, SOCK_STREAM, IPPROTO_TCP, 4999 + k, BACKLOG, num_peers, termination_round, fail);
            close(fd[0]);
            raise(SIGSTOP);

            p.setup_network(filename.c_str());
            p.start_thread();
            p.join_threads();
            if (!fail)
            {
                std::string hashed_dag;
                p.hash_dag(hashed_dag);
                write(fd[1], hashed_dag.c_str(), hashed_dag.size());
            }
            close(fd[1]);
            exit(EXIT_SUCCESS);
        }
        else
        {
            waitpid(child, nullptr, WUNTRACED);
            children.push_back(child);
        }
    }

    killpg(getpid(), SIGCONT);
    close(fd[1]);
    time(&start);
    char buffer[MAX_STREAM_SIZE];
    std::memset(buffer, 0, sizeof(buffer));

    while (counter > 0)
    {
        /* waiting for all the children to finish*/
        child_exited = wait(&status);
        read(fd[0], buffer, HEX_HASH);
        dags.emplace_back(buffer);
        exit_status_map.emplace(child_exited, status);
        counter--;
    }
    close(fd[0]);

    for (int i = 0; i < dags.size() - 1; i++)
    {
        if (dags[i] != dags[i + 1])
        {
            equal = false;
            break;
        }
    }

    std::cout << "PID\tSTATUS\tEXIT STATUS" << std::endl;
    for (auto i = exit_status_map.begin(); i != exit_status_map.end(); i++)
    {
        std::cout << (*i).first << "\t";
        current_status = WIFEXITED((*i).second);
        if (current_status)
        {
            std::cout << "EXITED\t";
            std::cout << WEXITSTATUS(current_status) << "\n";
        }
        else
        {
            current_status = WIFSIGNALED((*i).second);
            if (current_status)
            {
                std::cout << "SIGNALED\t";
                std::cout << WTERMSIG(current_status) << "\n";
            }
        }
    }

    printf("PARENT WITH ID %d FINISHED AFTER %ld CHILD PROCESSES!\n", getpid(), exit_status_map.size());
    printf("Time took %f\n", std::difftime(time(NULL), start));
    std::cout << "By the end, all peers think of the same DAG: " << std::boolalpha << equal << std::endl;
    return 0;
}

void sighandler(int signal)
{
    std::ofstream file;
    std::string name = "Action.txt";
    file.open(name, std::ios::app);
    file << getppid() << "\n";
    file.close();
    raise(signal);
}

void signalHandler(int signum, siginfo_t *info, void *context)
{
    // printf("Signal %d received from process with PID %d\n", signum, info->si_pid);
    std::ofstream file;
    std::string name = "Action.txt";
    file.open(name, std::ios::app);
    file << info->si_pid << getppid() << "\n";
    file.close();
    raise(signum);
}
