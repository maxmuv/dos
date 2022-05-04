#include "DSSimul.h"
//#include <unistd.h>
#include <assert.h>
#include <sstream>

int workFunction_BULLY(Process *dp, Message m){
    int ELECTION_TIME = 10;
    string s = m.getString();
    NetworkLayer *nl = dp->networkLayer;
    set<int> neibs = dp->neibs();
    if (s == "BULLY_ELECTION"){
        dp->context_bully.is_started = true;
        printf("BULLY[%d]: ELECTION message received from %d\n", dp->node, m.from);
        auto start = neibs.upper_bound(dp->node);
        if (start == neibs.end()) {
            for(auto n: neibs){
                nl->send(dp->node, n, Message("BULLY_VICTORY"));
                dp->context_bully.is_started = false;
            }
        } else {
            for (auto i = start; i != neibs.end(); ++i) {
                nl->send(dp->node, *i, Message("BULLY_ELECTION"));
            }
            if (m.from == -1)
                return true;
            nl->send(dp->node, m.from, Message("BULLY_ALIVE")); 
        }
    } else if (s == "BULLY_ALIVE"){
        dp->context_bully.got_alive_message = true;
        printf("BULLY[%d]: ALIVE message received from %d\n", dp->node, m.from);
    } else if (s == "BULLY_VICTORY"){
        printf("BULLY[%d]: VICTORY message received from %d\n", dp->node, m.from);
        if (m.from < dp->node){
            for (auto n:neibs) 
                nl->send(dp->node, n, Message("BULLY_VICTORY"));
            dp->context_bully.coord_id = dp->node;
        } else {
            dp->context_bully.coord_id = m.from; 
        }
        printf("BULLY[%d]: Coordinator is %d\n", dp->node, dp->context_bully.coord_id);
        dp->context_bully.start_time = -1;
        dp->context_bully.got_alive_message = false;
        dp->context_bully.is_started = false;         
    } else if (s == "*TIME") {
        int val = m.getInt();
        if (dp->context_bully.is_started) {
            if (dp->context_bully.start_time != -1)
                dp->context_bully.start_time = val;
            if ((dp->context_bully.got_alive_message == false) && (val > dp->context_bully.start_time + ELECTION_TIME)){
                for (auto n: neibs)
                    nl->send(dp->node, n, Message("BULLY_VICTORY"));
                dp->context_bully.is_started = false;
                dp->context_bully.start_time = -1;
                dp->context_bully.coord_id = dp->node;
                printf("BULLY[%d]: Wait too long! Coordinator is me.\n", dp->node);
            } else if ((dp->context_bully.got_alive_message == true) && (val > dp->context_bully.start_time + ELECTION_TIME)) {
                auto start = neibs.upper_bound(dp->node);
                for (auto i = start; i != neibs.end(); ++i) {
                    nl->send(dp->node, *i, Message("BULLY_ELECTION"));
                }
                printf("BULLY[%d]: Wait too long! Start new elections.\n", dp->node);
                dp->context_bully.start_time = -1;
            }
        }
    }
    return true;    
}

int workFunction_TEST(Process *dp, Message m)
{
    string s = m.getString();
    NetworkLayer *nl = dp->networkLayer;
    if (!dp->isMyMessage("TEST", s)) return false;
    set<int> neibs = dp->neibs(); 
    if (s == "TEST_HELLO") {
        int val = m.getInt();
        printf("TEST[%d]: HELLO %d message received from %d\n", dp->node, val, m.from);
        // Рассылаем сообщение соседям
        if (val < 2) {
            for (auto n: neibs) {
                nl->send(dp->node, n, Message("TEST_HELLO", val+1));
            }
        } else {
            for (auto n: neibs) {
                nl->send(dp->node, n, Message("TEST_BYE"));
            }
        }
    } else if (s == "TEST_BYE") {
        printf("TEST[%d]: BYE message received from %d\n", dp->node, m.from);
    }
    return true;
}

int main(int argc, char **argv)
{
    string configFile = argc > 1 ? argv[1] : "config.data";
    World w; 
    w.registerWorkFunction("BULLY", workFunction_BULLY);
    if (w.parseConfig(configFile)) {
        this_thread::sleep_for(chrono::milliseconds(3000000));
	} else {
        printf("can't open file '%s'\n", configFile.c_str());
    }
}

