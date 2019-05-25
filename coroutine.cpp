#include <cassert>
#include <cerrno>
#include <cstring>
#include <chrono>

#include "coroutine.h"


namespace coroutine {

uint64_t now_us() {
    auto time_now = std::chrono::system_clock::now();
    auto duration_in_us = std::chrono::duration_cast<std::chrono::microseconds>(time_now.time_since_epoch());
    return duration_in_us.count();
}


Schedule* Schedule::instance_ = nullptr;

Schedule::Schedule() :
    cur_(nullptr),
    max_id_(0)
{
    epollfd = epoll_create(MAX_EVENTS);
}

Schedule::~Schedule() {
}

void Schedule::epoll(uint64_t usec) {
    if (!usec || !readys.empty()) return;
    int nready = 0;
    while ((nready = epoll_wait(epollfd, eventslist, MAX_EVENTS, usec / 1000)) == -1) {
        if (errno != EINTR) assert(0);
    }
    num_events = nready;
}

void Schedule::addEvent(int fd, int events) {
    struct epoll_event ev;
    ev.events = events;
    ev.data.fd = fd;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev);
}

void Schedule::deleteEvent(int fd, int events) {
    struct epoll_event ev;
    ev.events = events;
    ev.data.fd = fd;
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, &ev);
}

void Schedule::modifyEvent(int fd, int events) {
    struct epoll_event ev;
    ev.events = events;
    ev.data.fd = fd;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &ev);
}

bool Schedule::empty() const {
     return readys.empty() && waits.empty() && sleeps.empty(); 
}

void Schedule::pushReady(Coroutine* co) {
    co->instatus_ = Coroutine::inReady;
    readys.push(co);
}

void Schedule::popReady() {
    readys.front()->instatus_ = Coroutine::inHang;
    readys.pop();
}

Coroutine* Schedule::topReady() {
    return readys.front();
}

void Schedule::addWait(Coroutine* co) {
    assert(co->fd > 0);
    co->instatus_ = Coroutine::inWait;
    waits.insert(std::make_pair(co->fd, co));
}

Coroutine* Schedule::removeWait(int fd) {
    Coroutine *res;
    auto iter = waits.find(fd);
    assert(iter != waits.end());
    iter->second->instatus_ = Coroutine::inHang;
    res = iter->second;
    waits.erase(iter);
    return res;
}

uint64_t Schedule::doSleeps() {
    if (sleeps.empty()) {
        if (readys.empty()) return 1000000u;
        return 0;
    }
    uint64_t now = now_us();
    int64_t diff = sleeps.begin()->first - now;
    if (diff > 0) return diff;
    std::map<uint64_t, Coroutine*>::iterator it;
    while ((it = sleeps.begin()) != sleeps.end() && it->first < now) { // todo
        removeSleep(it->first);
        pushReady(it->second);
    }
    return 0;
}

void Schedule::addSleep(Coroutine* co) {
    assert(co->timer_ > 0);
    co->instatus_ = Coroutine::inSleep;
    sleeps.insert(std::make_pair(co->timer_, co));
}

Coroutine* Schedule::removeSleep(uint64_t timer) {
    Coroutine* res;
    auto iter = sleeps.find(timer);
    assert(iter != sleeps.end());
    iter->second->instatus_ = Coroutine::inHang;
    res = iter->second;
    sleeps.erase(iter);
    return res;
    
}

void Schedule::run() {
    while (!empty()) {
        Coroutine *last = readys.back();
        while (!readys.empty()) {
            Coroutine *cur = topReady();
            popReady();
            if (cur->dead || cur->isExpire()) 
                delete cur;
            else
                cur->resume();
        }
        uint64_t usec = doSleeps(); // wait time
        epoll(usec); // blocking

        for (int i = 0; i < num_events; ++i) {
            struct epoll_event *ev = eventslist + i;
            int fd = ev->data.fd;
            Coroutine *c = removeWait(fd);
            pushReady(c);
        }
    }
}


Coroutine::Coroutine(Function func, void * params) : func_(func), params_(params), status_(eREADY), instatus_(0), fd(-1), timer_(0), timeout_(0), dead(false) {
    schedule_ = Schedule::instance();
    id_ = schedule_->max_id_++;
    schedule_->pushReady(this); // this 泄漏
}

void Coroutine::exec() { // co
    printf("coroutine %d start\n", id_);
    func_(params_);
    dead = true;
    schedule_->cur_ = nullptr;
    printf("coroutine %d exit\n", id_);
}

void Coroutine::resume() { // main
    switch (status_) {
    case eREADY :
        getcontext(&ctx_);
        ctx_.uc_stack.ss_sp = schedule_->stack_top();
        ctx_.uc_stack.ss_size = schedule_->stack_size();
        ctx_.uc_link = &schedule_->main_ctx_;
        makecontext(&ctx_, (void (*)())(&Coroutine::exec), 1, this);
        break;
    case eSUSPEND :
        memcpy(schedule_->stack_bottom() - stack_.size(), stack_top(), stack_.size());
        break;
    default:
        assert(0);
        break;
    }
    status_ = eRUNNING;
    assert(!schedule_->cur_);
    schedule_->cur_ = this;        
    swapcontext(&schedule_->main_ctx_, &ctx_);
}

void Coroutine::swap_mem() {
    char addr;
    assert(schedule_->stack_bottom() > &addr);
    uint64_t distance = schedule_->stack_bottom() - &addr;
    assert(distance < schedule_->stack_size());
    stack_.resize(distance);
    memcpy(stack_top(), &addr, stack_.size());
}

void Coroutine::wait(int fd, unsigned short events, uint64_t timeout) { // co todo timeout
    this->fd = fd;
    schedule_->addWait(this);
    schedule_->cur_ = nullptr;
    schedule_->addEvent(fd, events);
    status_ = eSUSPEND;
	
    swap_mem();
    swapcontext(&ctx_ , &schedule_->main_ctx_);
    
    schedule_->deleteEvent(fd, events);
    fd = -1;
}

void Coroutine::yield() { // co
    schedule_->pushReady(this);
    schedule_->cur_ = nullptr;
    status_ = eSUSPEND;
    swap_mem();
    swapcontext(&ctx_ , &schedule_->main_ctx_);
}

void Coroutine::cancel() { // co
    dead = true;
    schedule_->cur_ = nullptr;
    setcontext(&schedule_->main_ctx_);
}

void Coroutine::sleep(int ms) { //co
    timer_ = now_us() + 1000 * ms;
    schedule_->addSleep(this);
    schedule_->cur_ = nullptr;
    status_ = eSUSPEND;
    swap_mem();
    swapcontext(&ctx_ , &schedule_->main_ctx_);
}

void Coroutine::expire(int ms) {
    timeout_ = now_us() + 1000 * ms;
}

bool Coroutine::isExpire() {
    return timeout_  && (now_us() > timeout_);
}

int co_id() {
    Schedule *s = Schedule::instance();
    assert(s->current());
    return s->current()->id();
}

void yield() {
    Schedule *s = Schedule::instance();
    assert(s->current());
    s->current()->yield();
}

void co_sleep(uint64_t ms) {
    Schedule *s = Schedule::instance();
    assert(s->current());
    s->current()->sleep(ms);
}

void co_cancel() {
    Schedule *s = Schedule::instance();
    assert(s->current());
    s->current()->cancel();
}


void wait(int fd, unsigned short events, uint64_t timeout) { // co
    Schedule *s = Schedule::instance();
    Coroutine *c = s->current();
    assert(c);
    c->wait(fd, events, timeout);
}

int co_socket(int domain, int type, int protocol) { // non blocking fd
	int fd = socket(domain, type|SOCK_NONBLOCK, protocol);
	if (fd < 0) return -1;
	int reuse = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, sizeof(reuse));
	return fd;
}

int co_accept(int fd, struct sockaddr *addr, socklen_t *len) {
	int sockfd;
	while (1) {
        wait(fd,  EPOLLIN, 1);
		sockfd = accept(fd, addr, len);
		if (sockfd > 0) break;
        if (errno != EAGAIN) return -1;
	}
	int ret = fcntl(sockfd, F_SETFL, O_NONBLOCK);
	if (ret < 0) { close(sockfd); return -1;}
	return sockfd;
}


int co_connect(int fd, struct sockaddr *name, socklen_t namelen) {
	int ret;
	while (1) {
        wait(fd, EPOLLOUT, 1);
		ret = connect(fd, name, namelen);
        if (ret < 0 && (errno == EAGAIN ||
			errno == EWOULDBLOCK || errno == EINPROGRESS)) 
            continue;
		break;
	}
	return ret;
}

ssize_t co_send(int fd, const void *buf, size_t len, int flags) {
	int n = 0;
	while (n < len) {
		wait(fd, EPOLLOUT, 1);
		int ret = send(fd, (char*)buf+n, len-n, flags);
		if (ret <= 0) continue;
		n += ret;
	}
	return n;
}

ssize_t co_recv(int fd, void *buf, size_t len, int flags) {
	int ret;
	while (1) {
		wait(fd, EPOLLIN, 1);
		ret = recv(fd, buf, len, flags);
		if (ret < 0 && (errno == EINTR || errno == EWOULDBLOCK || errno == EAGAIN)) continue;
		break;
	}
	return ret;
}

Coroutine* go(Function func, void *params) {
    return new Coroutine(func, params);
}

} // namespace coroutine