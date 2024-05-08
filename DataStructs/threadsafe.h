#include <condition_variable>
#include <cstring>
#include <functional>
#include <iostream>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>
#include <unordered_set>

// UNORDERED MAP CLASS

template <typename Key, typename Value>
class Unordered_map
{
private:
    std::mutex mtx;
    std::unordered_map<Key, Value> map;

public:
    Unordered_map() = default;

    void insert(const Key &key, Value &value)
    {
        std::scoped_lock lock(this->mtx);
        map[key] = value;
    }

    bool find(const Key &key, Value *value)
    {
        std::scoped_lock lock(this->mtx);
        if (map.find(key) != map.end())
        {
            if (value != nullptr)
                *value = map[key];
            return true;
        }
        return false;
    }

    int size()
    {
        std::scoped_lock lock(this->mtx);
        return map.size();
    }

    typename std::unordered_map<Key, Value>::iterator begin()
    {
        std::scoped_lock lock(this->mtx);
        return this->map.begin();
    }

    typename std::unordered_map<Key, Value>::iterator end()
    {
        std::scoped_lock lock(this->mtx);
        return this->map.end();
    }
};

// UNORDERED SET CLASS

template <typename Value>
class Unordered_set
{
private:
    std::mutex mtx;
    std::unordered_set<Value> set;

public:
    Unordered_set() = default;

    void emplace(Value element)
    {
        std::scoped_lock lock(this->mtx);
        this->set.emplace(element);
    }

    bool find(Value element)
    {
        std::scoped_lock(this->mtx);
        if (this->set.find(element) != this->set.end())
        {
            return true;
        }
        return false;
    }

    int size()
    {
        std::scoped_lock(this->mtx);
        return this->set.size();
    }
};

// THREADPOOL OPERATIONS

// struct Task
// {
//     int sockfd;
//     char *buff = nullptr;
//     size_t len;
//     int flags;
//     void (*send_fun)(int, char *, size_t, int) = nullptr;

//     // Move constructor
//     Task(Task &&source) noexcept : sockfd(std::move(source.sockfd)), len(std::move(source.len)), flags(std::move(source.flags)), buff(std::move(source.buff)), send_fun(std::move(source.send_fun))
//     {
//         source.buff = nullptr;
//         source.sockfd = -1;
//         source.len = 0;
//         source.flags = 0;
//         source.send_fun = nullptr;
//     }
//     Task() = default;

//     // Move operator
//     Task &operator=(Task &&source) noexcept
//     {
//         if (this != &source)
//         {
//             if (buff != nullptr)
//             {
//                 free(buff);
//             }

//             sockfd = std::move(source.sockfd);
//             buff = std::move(source.buff);
//             len = std::move(source.len);
//             flags = std::move(source.flags);
//             send_fun = std::move(source.send_fun);

//             source.buff = nullptr;
//             source.sockfd = -1;
//             source.len = 0;
//             source.flags = 0;
//             source.send_fun = nullptr;
//         }
//         return *this;
//     }
// };

// class Threadpool
// {
// private:
//     int num_threads;
//     std::mutex qmtx;
//     std::condition_variable qcv;
//     std::queue<Task> task_queue;
//     std::vector<std::thread> threads;
//     bool stop = false;

//     void worker_thread()
//     {
//         std::unique_lock<std::mutex> lock(this->qmtx, std::defer_lock);
//         while (true)
//         {
//             // Task task;

//             lock.lock();
//             qcv.wait(lock, [this]
//                      { return !task_queue.empty() || stop; });

//             if (stop && task_queue.empty())
//             {
//                 return;
//             }

//             Task task(std::move(task_queue.front()));
//             task_queue.pop();
//             lock.unlock();

//             task.send_fun(task.sockfd, task.buff, task.len, task.flags);
//             free(task.buff);
//         }
//     }

// public:
//     Threadpool(int num_threads) : num_threads(num_threads)
//     {
//         for (int i = 0; i < num_threads; i++)
//         {
//             this->threads.emplace_back(std::bind(&Threadpool::worker_thread, this));
//         }
//     }

//     Threadpool() = default;

//     ~Threadpool()
//     {
//         std::unique_lock<std::mutex> lock(this->qmtx, std::defer_lock);
//         lock.lock();
//         this->stop = true;
//         lock.unlock();

//         this->qcv.notify_all();
//         for (int i = 0; i < this->threads.size(); i++)
//         {
//             this->threads[i].join();
//         }
//     }

//     void enqueue(void (*send_fun)(int socket, char *buffer, size_t len, int flags), int socket, char *buffer, size_t len, int flags)
//     {
//         Task task;
//         std::unique_lock<std::mutex> lock(this->qmtx, std::defer_lock);

//         task.buff = (char *)malloc(len * sizeof(char));
//         std::memcpy(task.buff, buffer, len);

//         task.send_fun = send_fun;
//         task.len = len;
//         task.sockfd = socket;
//         task.flags = flags;

//         lock.lock();
//         this->task_queue.emplace(std::move(task));
//         lock.unlock();
//         this->qcv.notify_one();
//     }
// };

struct Task
{
    std::function<void()> fun;

    Task(std::function<void()> function) : fun(function) {}

    void execute()
    {
        if (fun)
        {
            fun();
        }
    }
};

class Threadpool
{
private:
    int num_threads;
    std::mutex qmtx;
    std::condition_variable qcv;
    std::queue<Task> task_queue;
    std::vector<std::thread> threads;
    bool stop = false;

    void worker_thread()
    {
        std::unique_lock<std::mutex> lock(this->qmtx, std::defer_lock);
        while (true)
        {
            // Task task;

            lock.lock();
            qcv.wait(lock, [this]
                     { return !task_queue.empty() || stop; });

            if (stop && task_queue.empty())
            {
                return;
            }

            Task task(std::move(task_queue.front()));
            task_queue.pop();
            lock.unlock();

            task.execute();
        }
    }

public:
    Threadpool(int num_threads) : num_threads(num_threads)
    {
        for (int i = 0; i < num_threads; i++)
        {
            this->threads.emplace_back(std::bind(&Threadpool::worker_thread, this));
        }
    }

    Threadpool() = default;

    ~Threadpool()
    {
        std::unique_lock<std::mutex> lock(this->qmtx, std::defer_lock);
        lock.lock();
        this->stop = true;
        lock.unlock();

        this->qcv.notify_all();
        for (int i = 0; i < this->threads.size(); i++)
        {
            this->threads[i].join();
        }
    }

    void enqueue(Task task)
    {
        std::unique_lock<std::mutex> lock(this->qmtx, std::defer_lock);

        lock.lock();
        this->task_queue.emplace(std::move(task));
        lock.unlock();
        this->qcv.notify_one();
    }
};