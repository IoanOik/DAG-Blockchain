#include <iostream>
#include <fstream>

int main(int argc, char const *argv[])
{

    if (argc != 3)
    {
        printf("Give the number of validators and the starting port number\n");
        exit(0);
    }

    int validators = atoi(argv[1]);
    int starting_port = atoi(argv[2]);

    std::ofstream file;
    std::string filename = "testfile.txt";
    file.open(filename, std::ios::trunc);

    for (int i = 0; i < validators; i++)
    {

        file << "127.0.0.1 " << starting_port << "\n";
        starting_port++;
    }

    return 0;
}
