#pragma once
#define _CRT_SECURE_NO_WARNINGS 1
#include <queue>
#include <map>
#include <string>
#include <fstream>
#include <set>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <chrono>
#include <exception>
#include <random>
#include <string.h>
using namespace std;

using int32 = int;
using uint32 =  unsigned;
using byte = unsigned char;
using int64 = long long;
using uint64 = unsigned long long;
using bytevector = vector<byte>;

struct ErrorCode {
    enum {
        OK, NotExpectedType, ReadAttemptOutOfBounds, ObjectIsNULL,
        ResourceNotFound, ResourceInUse, PrematureEndOfStream,
        SizeTooBig, YetNotImplemented, DuplicateItems,
        ItemNotFound, QueueIsEmpty, SocketError,
        ConnectionFailed, ConnectionInUse, TimeOut,
    };
};

class MessageArg {
public:
    enum {
        IntType='A', Int64Type, StringType, VectorIntType, EOFType
    };
    MessageArg(int64 q) {
        uint64 pq = (uint64)q;
        body.push_back((byte)Int64Type);
        for (int i = 0; i < 8; i++) {
            body.push_back((byte)(pq & 0xFF));
            pq >>= 8;
        }
    }
    MessageArg(int q) {
        body.push_back((byte)IntType);
        for (int i = 0; i <= 24; i+=8) {
            body.push_back((byte)((q >> i) & 0xFF));
        }
    }
    MessageArg(string const &s) {
        MessageArg(s.c_str());
    }
    MessageArg(const char *s) {
        body.push_back((byte)StringType);
        for (size_t i = 0; s[i] != 0; i++) {
            body.push_back((unsigned char)s[i]);
        }
        body.push_back((byte)0);
    }
    bytevector body;
};

class Message {
public:
    Message(int from, int to, bytevector const &body) {
        this->from = from; this->to = to; this->body = body; 
    }
    Message(MessageArg const &a1) {
        append(a1);
    }
    Message(MessageArg const &a1, MessageArg const &a2) {
        append(a1); append(a2);
    }
    Message(MessageArg const &a1, MessageArg const &a2, MessageArg const &a3) {
        append(a1); append(a2); append(a3);
    }
    Message(MessageArg const &a1, MessageArg const &a2, MessageArg const &a3, MessageArg const &a4) {
        append(a1); append(a2); append(a3); append(a4);
    }
    int64 sendTime = 0, deliveryTime = 0;
    int from = -1, to = -1, ptr = 0;
    bytevector body;
    string getString() {
        if (ptr < (int)body.size() && body[ptr] == (byte)MessageArg::StringType) {
            string ret; ptr++;
            while (ptr < (int)body.size() && body[ptr] != 0) {
                ret.push_back((char)body[ptr++]);
            }
            ptr++; return ret;
        }
        throw std::logic_error("Expected string");
    }
    int getInt() {
        if (ptr+4 < (int)body.size() && body[ptr] == (byte)MessageArg::IntType) {
            unsigned ret = 0;
            ptr++;
            for (int i = 0; i <= 24; i+=8) {
                ret |= (body[ptr++] & 0xFF) << i;
            }
            return (int)ret;
        }
        throw std::logic_error("Expected int");
    }
    int64 getInt64() {
        if (ptr+8 < (int)body.size() && body[ptr] == (byte)MessageArg::Int64Type) {
            uint64 ret = 0; ptr++;
            for (int i = 0; i < 8; i++) {
                ret = (ret <<= 8) | (body[ptr+7-i] & 0xFF);
            }
            ptr += 8;
            return (int64)ret;
        }
        throw std::logic_error("expected int64");
    }
    bool operator>(Message const &oth) const {
        return deliveryTime > oth.deliveryTime;
    }
private:
    void append(MessageArg const &a) {
        for (byte b:  a.body) body.push_back(b);
    }
};

class MessageQueue {
public:
    Message dequeue() {
        lock_guard<recursive_mutex> ar(_mutex);
        Message ret = queue.top();
        queue.pop();
        return ret;
    }
    void enqueue(Message const &msg) {
        lock_guard<recursive_mutex> ar(_mutex);
        queue.push(msg);
    }
    Message peek() const { return queue.top(); }
    int size() const { return (int)queue.size(); }
private:
    priority_queue<Message, vector<Message>, greater<Message> > queue;
    recursive_mutex _mutex;
};

class Process;
// Сетевая инфраструктура. Каждый процесс должен зарегистрироваться в ней.
// Она также регистрирует связи между процессами и посылает сообшения процессам.
class NetworkLayer
{
public:
    ~NetworkLayer() {
        stopFlag = true; globalTimer.join();
    }
    // Моделируется асинхронный режим. Сообщения посылаются процессу немедленно и
    // доставляются через время, указанное в свойствах связи. Процесс принимает 
    // сообщения независимо от показания глобальных часов и от других процессов. 
    void setErrorRate(double rate) { errorRate = rate; }
    void createLink(int from, int to, bool bidirectional = true, int cost = 0) {
        if (from == to) return;
        networkMap[from][to] = cost;
        if (bidirectional) networkMap[to][from] = cost;
    }
    int getLink(int p1, int p2) {
        if (p1 < 0 || p1 == p2) return 0;
        if (networkMap.find(p1) != networkMap.end() &&
          networkMap[p1].find(p2) != networkMap[p1].end())
            return networkMap[p1][p2];
        return -1;
    }
    int send(int fromProcess, int toProcess, Message const &msg) {
        if (toProcess >= 0) return send(fromProcess, toProcess, msg.body);
        for (size_t i = 0; i < queueMap.size(); i++) 
            if (queueMap[i] != nullptr) send(fromProcess, (int)i, msg.body);
        return ErrorCode::OK;
    }
    int send(int fromProcess, int toProcess, bytevector const &msg) {
        if (toProcess >= networkSize) return ErrorCode::SizeTooBig;
        Message m(fromProcess, toProcess, msg);
        if (errorRate > 0 && distrib(rng) < errorRate) return ErrorCode::TimeOut;
        if (queueMap[toProcess] == nullptr) return ErrorCode::ItemNotFound;
        int p = getLink(fromProcess, toProcess);
        if (p < 0) return ErrorCode::ItemNotFound;
        m.sendTime = tick;
        m.deliveryTime = tick + p;
        queueMap[toProcess]->enqueue(m);
        return ErrorCode::OK;
    }
    int registerProcess(int node, Process *dp);
    vector<MessageQueue *>  queueMap;
    double errorRate = 0.;
    mt19937 rng;
    uniform_real_distribution<> distrib = uniform_real_distribution<>(0.0, 1.0);
    volatile long long tick = 0;
    void addLinksToAll(int from, bool bidirectional = true, int latency = 0) {
        for (int i = 0; i < networkSize; i++) 
            if (from != i) networkMap[from][i] = latency;
        if (bidirectional) {
            for (int i = 0; i < networkSize; i++) 
                if (from != i) networkMap[i][from] = latency;
        }
    }
    void addLinksFromAll(int to, bool bidirectional = true, int latency = 0) {
        for (int i = 0; i < networkSize; i++) 
            if (i != to) networkMap[i][to] = latency;
        if (bidirectional) {
            for (int i = 0; i < networkSize; i++) 
                if (to != i) networkMap[to][i] = latency;
        }
    }
    void addLinksAllToAll(bool bidirectional = true, int latency = 0) {
        for (int i = 0; i < networkSize; i++) 
            addLinksFromAll(i, bidirectional, latency);
    }
    set<int> neibs(int from) {
        set<int> ret;
        for (const auto n : networkMap[from]) ret.insert(n.first);
        return ret;
    }
//    void clear() {
//        networkMap.clear();
//    }
    bool stopFlag = false;
private:
    int networkSize;
    using mii = map<int, int>;
    map< int, mii> networkMap;
    thread globalTimer = thread(globalTimerExecutor, this);
    static void globalTimerExecutor(NetworkLayer *nl) {
        auto start = std::chrono::system_clock::now();
        while (!nl->stopFlag) {
            auto cl = std::chrono::system_clock::now() - start;
            nl->tick = chrono::duration_cast<chrono::milliseconds>(cl).count() / 1000;
            this_thread::sleep_for(chrono::milliseconds(100));
        }
    }
    recursive_mutex globalTimerMutex;
};

using workFunction = int (*)(Process *context, Message m);

class Process {
public:
    Process(int _node) : node(_node), stopFlag(false) {
        workerThread = thread(workerThreadExecutor, this);
    }
    ~Process() {
        stopFlag = true; workerThread.join();
    }
    MessageQueue workerMessagesQueue;
    set<int> neibs() {
        return networkLayer->neibs(node);
    }
    // Распределённый процесс может исполнять различные рабочие функции в зависимости от пришедшего сообщения
    // Каждая рабочая функция поддерживает свой контекст исполнения, для разных функций он может быть разным 
    void registerWorkFunction(string const &/*prefix*/, workFunction wf) {
        workers.push_back(wf); 
    }
    // Исполнительный поток пробует вызвать зарегистрированные рабочие функции. 
    // Если рабочая функция распознала сообщение, как предназначенное ей, она возвращает true.
    // Возможна ситуация, когда ни одна из рабочих функций не обработает сообщение, тогда оно пропадает.
    NetworkLayer *networkLayer;
    int         node;
    static bool isMyMessage(string const &prefix, string const &message) {
        if (message.size() > 0 && message[0] == '*') return true;
        if (prefix.size()+1 >= message.size()) return false;
        for (size_t i = 0; i < prefix.size(); i++) 
            if (prefix[i] != message[i]) return false;
        return message[prefix.size()] == '_';
    }
// Контексты рабочих функций
#include "contextes.h"
private:
    thread  workerThread;
    static void workerThreadExecutor(Process *dp) {
        while (!dp->stopFlag) {
            // Поток обнаруживает появление сообщений в очереди принятых сообщений 
            // и что-то делает в зависимости от самого сообщения
            if (dp->workerMessagesQueue.size() > 0 && dp->networkLayer->tick >= dp->workerMessagesQueue.peek().deliveryTime) {
                // Пришло новое сообщение в рабочую очередь
                Message m = dp->workerMessagesQueue.dequeue();
                for (auto worker: dp->workers) 
                    if (worker(dp, m)) break;
            }
            this_thread::sleep_for(chrono::milliseconds(1));
        }
    }
    bool        stopFlag;
    vector<workFunction> workers;
};

inline int NetworkLayer::registerProcess(int node, Process *dp) {
    dp->networkLayer = this;
    if (node >= (int)queueMap.size()) queueMap.resize(node+1);
    if (queueMap[node] != nullptr) return ErrorCode::DuplicateItems;
    queueMap[node] = &dp->workerMessagesQueue;
    networkSize = (int)queueMap.size();
    return ErrorCode::OK;
}

void timerSender(NetworkLayer *nl, int time) {
    int current = 0;
    while (!nl->stopFlag) {
        nl->send(-1, -1, Message("*TIME", current++));
        this_thread::sleep_for(chrono::seconds(time));
    }
}

class World {
public:
    ~World() {
        for (auto &p: processesList) {
            if (p != nullptr) {
                delete p; p = nullptr;
            }
        }
    }
    int createProcess(int node) {
        Process *p = new Process(node);
        if (node >= (int)processesList.size())  processesList.resize(node+1);
        processesList[node] = p;
        nl.registerProcess(node, p); 
        return node;
    }
    int assignWorkFunction(int node, string const &func) {
        if (node < 0 || node >= (int)processesList.size()) return ErrorCode::ItemNotFound;
        Process *dp = this->processesList[node];
        auto it = associates.find(func);
        if (it == associates.end()) return ErrorCode::ItemNotFound;
        if (dp == nullptr) return ErrorCode::ItemNotFound;
        dp->registerWorkFunction(func, it->second);
        return ErrorCode::OK;
    }
    void registerWorkFunction(string const &func, workFunction wf) {
        associates[func] = wf; 
    }
    vector<Process *> processesList;
    map<string, workFunction> associates;
    bool parseConfig(string const &name) {
        ifstream f(name.c_str());
        if (!f) return false;
        char    s[1024];
        int bidirected = 1, timeout = 0;
        while (!f.eof()) {
            f.getline(s, sizeof s - 1);
            if (strlen(s) == 0 || s[0] == ';') continue;
            char id[1024], msg[1024];
            double errorRate;
            int startprocess, endprocess, from, to, latency = 1, timer = 0, arg;
            if (sscanf(s, "bidirected %d", &bidirected) == 1) continue;
            else if (sscanf(s, "errorRate %lf", &errorRate) == 1) nl.setErrorRate(errorRate);
            else if (sscanf(s, "processes %d %d", &startprocess, &endprocess) == 2) {
                for (int i = startprocess; i <= endprocess; i++)
                    createProcess(i);
            } else if (sscanf(s, "link from %d to %d latency %d", &from, &to, &latency) == 3 || 
                sscanf(s, "link from %d to %d", &from, &to) == 2) { 
                    nl.createLink(from, to, bidirected != 0, latency);
            } else if (sscanf(s, "link from %d to all latency %d", &from, &latency) == 2 ||
                sscanf(s, "link from %d to all", &from) == 1) {
                    nl.addLinksToAll(from, bidirected != 0, latency);
            } else if (sscanf(s, "link from all to %d latency %d", &from, &latency) == 2 || 
                sscanf(s, "link from all to %d", &to) == 1) {
                    nl.addLinksFromAll(from, bidirected != 0, latency);
            } else if (strcmp(s, "link from all to all") == 0 || 
                sscanf(s, "link from all to all latency %d", &latency) == 1) {
                    nl.addLinksAllToAll(bidirected != 0, latency);
            } else if (sscanf(s, "setprocesses %d %d %s", &startprocess, &endprocess, id) == 3) {
                for (int i = startprocess; i <= endprocess; i++) assignWorkFunction(i, id);
            } else if (sscanf(s, "send from %d to %d %s %d", &from, &to, msg, &arg) == 4) {
                nl.send(from, to, Message(msg, arg));
            } else if (sscanf(s, "send from %d to %d %s", &from, &to, msg) == 3) {
                nl.send(from, to, Message(msg));
            } else if (sscanf(s, "wait %d", &timeout)) {
                this_thread::sleep_for(chrono::microseconds(1000000*timeout));
            } else if (sscanf(s, "launch timer %d", &timer) == 1) {
                thread(timerSender, &nl, timer).detach(); 
            } else {
                printf("unknown directive in input file: '%s'\n", s);
            }
        }
        f.close();
        return true;
    }
    NetworkLayer nl;
};
