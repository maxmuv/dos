
struct context_common_s {
};

struct context_common_s context_common;

// ATTN context
struct context_attn_s {
  int  ready;
  context_attn_s() {
     ready = 0;
  }
};

struct context_attn_s context_attn;

struct context_bully_s {
  int coord_id;
  int start_time;
  bool is_started;
  bool got_alive_message;
  context_bully_s() {
    coord_id = -1;
    start_time = -1;
    is_started = false;
    got_alive_message = false;
  }
};

struct context_bully_s context_bully;
