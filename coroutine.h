
#ifndef COROUTINE_H
#define COROUTINE_H

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h> 
#include <fcntl.h>
#include <ucontext.h>

#include <vector>
#include <set>
#include <map>
#include <queue>
#include <functional>

using namespace std;


namespace coroutine {
class Schedule;
class Coroutine;

typedef std::function<void(void*)> Function;

struct Coroutine {
    
public:
    enum Status{
        eREADY,
        eRUNNING,
        eSUSPEND,
    };
    
    enum inStatus{
        inHang,
        inReady,
        inWait,
        inSleep
    };
    friend class Schedule;
    friend void wait(int fd, unsigned short events, uint64_t timeout);

public:
    Coroutine(Function func, void *params);
    int id() const { return this->id_; }
    void exec();
    void resume();
    void yield();
    void cancel();
    void wait(int fd, unsigned short events, uint64_t timeout);
    void sleep(int ms);
    void expire(int timeout_);
    bool isExpire();

private:
    char* stack_top() { return &*stack_.begin(); }
    void swap_mem();

private:
    std::vector<char> stack_;
    Function func_;
    void *params_;
    ucontext_t ctx_;
    int status_;
    int instatus_;
    int id_;
    int fd;
    bool dead;
    uint64_t timer_; // for sleep
    uint64_t timeout_; // for expire
    Schedule* schedule_;
};

const int  STACK_SIZE  = 1024*1024;

class Schedule {
    friend struct Coroutine;
public:
    static const int MAX_EVENTS = 1 << 20;
    
    Coroutine* current() const { return cur_;  }
    bool empty() const;

    void pushReady(Coroutine* co);
    void popReady();
    Coroutine* topReady();

    void addWait(Coroutine* co);
    Coroutine*  removeWait(int);
    void addSleep(Coroutine* co);
    Coroutine*  removeSleep(uint64_t timer);
    void doWaits();
    uint64_t doSleeps();
    void epoll(uint64_t usec);
    void run();
    void addEvent(int fd, int events);
    void deleteEvent(int fd, int events);
    void modifyEvent(int fd, int events);

    static Schedule* instance() {
        if (instance_ == nullptr)
            instance_ = new Schedule();
        return instance_;
    }

private:
    Schedule();
    ~Schedule();
    static Schedule *instance_;

    std::size_t stack_size() const { return STACK_SIZE; }
    char* stack_top() { return stack_; }
    char* stack_bottom() { return stack_top() + stack_size() ; }

    std::queue<Coroutine*> readys;
    std::map<int, Coroutine*> waits;
    std::map<uint64_t, Coroutine*> sleeps;
    char stack_[STACK_SIZE];
    ucontext_t main_ctx_;
    Coroutine* cur_;
    int max_id_;

    // epoll
    int epollfd;
	struct epoll_event eventslist[MAX_EVENTS];
	int num_events;
};


int co_id();
void yield();
void co_sleep(uint64_t ms);
void co_cancel();
void wait(int fd, unsigned short events, uint64_t timeout); 

int co_socket(int domain, int type, int protocol);
int co_accept(int fd, struct sockaddr *addr, socklen_t *len);
int co_connect(int fd, struct sockaddr *name, socklen_t namelen);
ssize_t co_send(int fd, const void *buf, size_t len, int flags);
ssize_t co_recv(int fd, void *buf, size_t len, int flags);

Coroutine* go(Function func, void *params);

} // namespace coroutine

#endif // COROUTINE_H


